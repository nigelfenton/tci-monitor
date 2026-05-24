#pragma once

// TciClient — minimal WebSocket client for the TCI protocol.
//
// Unlike a logger or controller's TCI client, this one is intentionally
// dumb: it does not track current freq/mode, does not interpret events,
// does not filter anything.  Every message line received from the server
// is emitted verbatim via rawMessageReceived() so the monitor's UI can
// show / parse / save the full stream.
//
// Auto-reconnect with exponential backoff (1, 2, 5, 10, 30 s) until the
// caller explicitly disconnects.

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

class QWebSocket;
class QTimer;

namespace TciMon {

// TCI binary frame, as documented in ExpertSDR3 TCI spec v2.0 Stream struct
// and mirrored by AetherSDR's TciServer (src/core/TciServer.cpp). Header is
// 16 × uint32 = 64 bytes, followed by `length × channels × format-sized`
// sample payload. We keep raw access so the Signal tab can display payload
// however it wants without us re-interpreting on each ingest.
struct TciBinaryFrame {
    quint32 receiver{0};      // receiver/TRX number (0..7)
    quint32 sampleRate{0};    // Hz
    quint32 format{0};        // 0=int16, 1=int24, 2=int32, 3=float32
    quint32 codec{0};         // 0 (uncompressed in current spec)
    quint32 crc{0};           // unused
    quint32 length{0};        // number of real samples in payload (per channel)
    quint32 type{0};          // 0=IQ, 1=RX_AUDIO, 2=TX_AUDIO, 3=TX_CHRONO, 4=SPECTRUM (AetherSDR ext)
    quint32 channels{0};      // 1 or 2
    quint32 reserved[8]{};    // for SPECTRUM (AetherSDR): [0]=low Hz, [1]=high Hz
    QByteArray payload;       // header-stripped raw bytes; sample interpretation per `format`
    qint64    wallClockNs{0}; // monotonic ns at reception (filled by client, not on the wire)
    quint32   counter{0};     // monotonic per-stream counter (filled by client, not on the wire)
};

class TciClient : public QObject {
    Q_OBJECT
public:
    explicit TciClient(QObject* parent = nullptr);
    ~TciClient() override;

    void connectToServer(const QString& host, quint16 port);
    void disconnectFromServer();

    void send(const QString& cmd);   // surface for ad-hoc command testing
    void sendBinary(const QByteArray& frame);   // TCI TX-audio frames

    bool    connected() const { return m_connected; }
    QUrl    currentUrl() const { return m_url; }
    QString lastError() const { return m_lastError; }

signals:
    void connectionChanged(bool connected);
    void rawMessageReceived(const QString& line);
    void binaryFrameReceived(const TciMon::TciBinaryFrame& frame);
    void binaryFrameRejected(const QString& reason, int size);  // diagnostic: too-short / corrupt
    void errorTextReceived(const QString& text);   // descriptive socket error

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessage(const QString& message);
    void onBinaryMessage(const QByteArray& message);
    void onErrorOccurred();
    void onReconnectTimeout();

private:
    void scheduleReconnect();
    void cancelReconnect();
    void setConnected(bool c);

    QWebSocket* m_socket{nullptr};
    QTimer*     m_reconnectTimer{nullptr};

    QUrl    m_url;
    bool    m_userInitiatedDisconnect{false};
    bool    m_connected{false};
    int     m_reconnectAttempts{0};
    QString m_lastError;
    quint32 m_binaryCounter{0};   // monotonic, stamped on each successful frame
};

} // namespace TciMon
