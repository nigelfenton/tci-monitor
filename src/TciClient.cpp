#include "TciClient.h"

#include <QWebSocket>
#include <QAbstractSocket>
#include <QElapsedTimer>
#include <QTimer>
#include <QStringList>
#include <cstring>

namespace TciMon {

namespace {
constexpr int kBackoff[] = { 1, 2, 5, 10, 30 };
int backoffSeconds(int attempt) {
    const int n = static_cast<int>(sizeof(kBackoff) / sizeof(kBackoff[0]));
    if (attempt < 0) attempt = 0;
    return kBackoff[attempt < n ? attempt : n - 1];
}

// Process-wide monotonic clock used to stamp wallClockNs on inbound frames.
// QElapsedTimer is monotonic (TimerType auto-selected per platform). We
// start it lazily on first call so static-init order doesn't matter.
qint64 monotonicNs() {
    static QElapsedTimer t;
    static bool started = false;
    if (!started) { t.start(); started = true; }
    return t.nsecsElapsed();
}
} // namespace

TciClient::TciClient(QObject* parent)
    : QObject(parent),
      m_socket(new QWebSocket(QString{}, QWebSocketProtocol::VersionLatest, this)),
      m_reconnectTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);

    connect(m_socket, &QWebSocket::connected,             this, &TciClient::onConnected);
    connect(m_socket, &QWebSocket::disconnected,          this, &TciClient::onDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived,   this, &TciClient::onTextMessage);
    connect(m_socket, &QWebSocket::binaryMessageReceived, this, &TciClient::onBinaryMessage);
    // QWebSocket::errorOccurred was added in Qt 6.5; on 6.2–6.4 the signal is
    // the (overloaded) error(QAbstractSocket::SocketError).
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(m_socket, &QWebSocket::errorOccurred,       this, &TciClient::onErrorOccurred);
#else
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &TciClient::onErrorOccurred);
#endif

    connect(m_reconnectTimer, &QTimer::timeout, this, &TciClient::onReconnectTimeout);
}

TciClient::~TciClient()
{
    cancelReconnect();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
    }
}

void TciClient::connectToServer(const QString& host, quint16 port)
{
    m_userInitiatedDisconnect = false;
    m_reconnectAttempts = 0;

    QUrl url;
    url.setScheme("ws");
    url.setHost(host);
    url.setPort(port);
    m_url = url;

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
    m_socket->open(m_url);
}

void TciClient::disconnectFromServer()
{
    m_userInitiatedDisconnect = true;
    cancelReconnect();
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->close();
    }
    setConnected(false);
}

void TciClient::send(const QString& cmd)
{
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->sendTextMessage(cmd);
    }
}

void TciClient::sendBinary(const QByteArray& frame)
{
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->sendBinaryMessage(frame);
    }
}

void TciClient::onConnected()
{
    m_reconnectAttempts = 0;
    setConnected(true);
    // Send `start;` to request streaming events.  Servers ignore unknown
    // commands so this is safe across TCI dialects.
    send("start;");
    // Opt in to sensor telemetry. tx_sensors carries SWR (and fwd/peak
    // watts, mic dBm) while transmitting — needed to observe an antenna
    // SWR sweep over TCI. rx_sensors adds the per-channel S-meter dBm.
    // Both are opt-in on AetherSDR (default off); unknown elsewhere = safely
    // ignored, same as `start;`.
    send("tx_sensors_enable:true;");
    send("rx_sensors_enable:true;");
}

void TciClient::onDisconnected()
{
    setConnected(false);
    if (!m_userInitiatedDisconnect) scheduleReconnect();
}

void TciClient::onErrorOccurred()
{
    if (m_socket) {
        m_lastError = m_socket->errorString();
        emit errorTextReceived(m_lastError);
    }
}

void TciClient::onTextMessage(const QString& message)
{
    // Servers may concatenate multiple commands into one frame separated
    // by ';' — split so each line is reported individually.
    const QStringList lines = message.split(';', Qt::SkipEmptyParts);
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (!line.isEmpty()) emit rawMessageReceived(line);
    }
}

void TciClient::onBinaryMessage(const QByteArray& message)
{
    // Per ExpertSDR3 TCI spec v2.0 Stream struct + AetherSDR's TciServer:
    // header is 16 × uint32 = 64 bytes, written by direct memcpy of a
    // little-endian-packed struct. Anything shorter is malformed.
    constexpr int kHeaderBytes = 64;
    if (message.size() < kHeaderBytes) {
        emit binaryFrameRejected(
            QStringLiteral("frame shorter than 64-byte header"),
            message.size());
        return;
    }

    TciBinaryFrame frame;
    // Read header fields as little-endian uint32 from the raw bytes rather
    // than memcpy'ing into a struct — keeps us safe against alignment and
    // host-endianness assumptions if someone builds on a big-endian box.
    const auto* p = reinterpret_cast<const unsigned char*>(message.constData());
    auto readU32 = [&](int byteOffset) -> quint32 {
        return  static_cast<quint32>(p[byteOffset])
             | (static_cast<quint32>(p[byteOffset + 1]) <<  8)
             | (static_cast<quint32>(p[byteOffset + 2]) << 16)
             | (static_cast<quint32>(p[byteOffset + 3]) << 24);
    };
    frame.receiver   = readU32( 0);
    frame.sampleRate = readU32( 4);
    frame.format     = readU32( 8);
    frame.codec      = readU32(12);
    frame.crc        = readU32(16);
    frame.length     = readU32(20);
    frame.type       = readU32(24);
    frame.channels   = readU32(28);
    for (int i = 0; i < 8; ++i)
        frame.reserved[i] = readU32(32 + 4 * i);

    frame.payload    = message.mid(kHeaderBytes);
    frame.wallClockNs = monotonicNs();
    frame.counter    = ++m_binaryCounter;

    emit binaryFrameReceived(frame);
}

void TciClient::scheduleReconnect()
{
    if (m_userInitiatedDisconnect) return;
    const int secs = backoffSeconds(m_reconnectAttempts);
    ++m_reconnectAttempts;
    m_reconnectTimer->start(secs * 1000);
}

void TciClient::cancelReconnect()
{
    if (m_reconnectTimer) m_reconnectTimer->stop();
}

void TciClient::onReconnectTimeout()
{
    if (m_userInitiatedDisconnect || m_url.isEmpty()) return;
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
    m_socket->open(m_url);
}

void TciClient::setConnected(bool c)
{
    if (m_connected == c) return;
    m_connected = c;
    emit connectionChanged(c);
}

} // namespace TciMon
