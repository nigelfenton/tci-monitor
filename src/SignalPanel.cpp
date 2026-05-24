#include "SignalPanel.h"

#include "TciClient.h"

#include <QComboBox>
#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPushButton>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace TciMon {

// ──────────────────────────────────────────────────────────────────────────
// SignalSpectrumView — single-frame line plot.
//
// Renders the float32 samples we extract from a TciBinaryFrame as a line
// graph filling the widget.  Y-axis auto-ranges between data min and max
// (with a floor floor of 1.0 to avoid divide-by-zero on silence), with a
// small label in the corner reporting the current range so the operator
// can read off levels.  No Q_OBJECT — pure paint widget.
// ──────────────────────────────────────────────────────────────────────────
class SignalSpectrumView : public QWidget {
public:
    explicit SignalSpectrumView(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(140);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAutoFillBackground(true);
        QPalette p = palette();
        p.setColor(QPalette::Window, QColor(20, 22, 28));
        setPalette(p);
    }

    void setFrame(const QVector<float>& samples, const QString& yUnit) {
        m_samples = samples;
        m_yUnit = yUnit;
        update();
    }

    void clearFrame() {
        m_samples.clear();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter g(this);
        g.fillRect(rect(), QColor(20, 22, 28));

        if (m_samples.isEmpty()) {
            g.setPen(QColor(120, 120, 130));
            g.drawText(rect(), Qt::AlignCenter,
                       QStringLiteral("Waiting for frames…"));
            return;
        }

        // Find min/max in this frame for auto-Y range.
        float lo = m_samples[0], hi = m_samples[0];
        for (float v : m_samples) { lo = std::min(lo, v); hi = std::max(hi, v); }
        if (hi - lo < 1.0f) { hi = lo + 1.0f; }   // floor to avoid divide-by-zero on silence
        const float yRange = hi - lo;

        // Grid lines (faint) every 25% of plot height.
        g.setPen(QColor(45, 50, 60));
        const int W = width(), H = height();
        for (int i = 1; i < 4; ++i) {
            const int y = H * i / 4;
            g.drawLine(0, y, W, y);
        }

        // Trace
        g.setPen(QPen(QColor(120, 220, 160), 1.2));
        QPainterPath path;
        const int N = m_samples.size();
        for (int i = 0; i < N; ++i) {
            const float x = (N == 1) ? 0.0f : (float)i * (W - 1) / (N - 1);
            const float yNorm = (m_samples[i] - lo) / yRange;  // 0 = min, 1 = max
            const float y = (1.0f - yNorm) * (H - 1);          // flip Y
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        g.drawPath(path);

        // Range readout (top-left)
        g.setPen(QColor(180, 190, 200));
        g.drawText(QRect(6, 4, W - 12, 18),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("Y: %1 … %2 %3   bins: %4")
                       .arg(lo, 0, 'g', 4)
                       .arg(hi, 0, 'g', 4)
                       .arg(m_yUnit)
                       .arg(N));
    }

private:
    QVector<float> m_samples;
    QString        m_yUnit;
};

// ──────────────────────────────────────────────────────────────────────────
// SignalWaterfallView — scrolling heat-map of recent frames.
//
// One QImage row per ingested frame; on each new frame, scroll the image
// up by 1 row and draw the new row at the bottom.  Colour map is a simple
// magma-ish ramp from dark to bright.  We auto-range per row so the map
// works for any of spectrum-in-dBm, audio-amplitude, IQ-magnitude without
// needing a fixed range.
// ──────────────────────────────────────────────────────────────────────────
class SignalWaterfallView : public QWidget {
public:
    explicit SignalWaterfallView(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(160);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAutoFillBackground(true);
        QPalette p = palette();
        p.setColor(QPalette::Window, QColor(15, 16, 22));
        setPalette(p);
    }

    void pushRow(const QVector<float>& samples) {
        if (samples.isEmpty()) return;

        // (Re)allocate the backing image whenever the widget resizes or
        // the row width changes.  Width = number of bins (we don't resample
        // to widget pixels — the paintEvent stretches whatever image we have).
        const int W = samples.size();
        const int H = std::max(60, height());
        if (m_img.isNull() || m_img.width() != W || m_img.height() != H) {
            m_img = QImage(W, H, QImage::Format_RGB32);
            m_img.fill(QColor(15, 16, 22));
        }

        // Scroll up by 1 px
        std::memmove(m_img.scanLine(0), m_img.scanLine(1),
                     m_img.bytesPerLine() * (m_img.height() - 1));

        // Range-normalise this row
        float lo = samples[0], hi = samples[0];
        for (float v : samples) { lo = std::min(lo, v); hi = std::max(hi, v); }
        if (hi - lo < 1.0f) hi = lo + 1.0f;
        const float range = hi - lo;

        // Render to the new bottom row
        auto* line = reinterpret_cast<QRgb*>(m_img.scanLine(m_img.height() - 1));
        for (int x = 0; x < W; ++x) {
            const float n = std::clamp((samples[x] - lo) / range, 0.0f, 1.0f);
            line[x] = colourMap(n);
        }
        update();
    }

    void clearImage() {
        if (!m_img.isNull()) m_img.fill(QColor(15, 16, 22));
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter g(this);
        g.fillRect(rect(), QColor(15, 16, 22));
        if (m_img.isNull()) {
            g.setPen(QColor(110, 110, 120));
            g.drawText(rect(), Qt::AlignCenter,
                       QStringLiteral("Waterfall — waiting for frames…"));
            return;
        }
        // Stretch the bin image across the widget; Qt's smooth transform
        // gives us bilinear interpolation for free.
        g.setRenderHint(QPainter::SmoothPixmapTransform, true);
        g.drawImage(rect(), m_img);
    }

    void resizeEvent(QResizeEvent*) override {
        // Force re-alloc on next push so vertical history matches widget size.
        m_img = QImage{};
        update();
    }

private:
    static QRgb colourMap(float n) {
        // Simple dark→bright ramp through blue→magenta→amber→white.
        // Hand-tuned so background noise stays near-black and peaks pop.
        n = std::clamp(n, 0.0f, 1.0f);
        const int r = (int)std::round(255.0f * std::pow(n, 0.55f));
        const int gC = (int)std::round(255.0f * std::pow(n, 1.30f));
        const int bC = (int)std::round(255.0f * (1.0f - std::pow(1.0f - n, 2.0f)) * 0.55f);
        return qRgb(std::clamp(r, 0, 255),
                    std::clamp(gC, 0, 255),
                    std::clamp(bC, 0, 255));
    }

    QImage m_img;
};

// ──────────────────────────────────────────────────────────────────────────
// SignalPanel
// ──────────────────────────────────────────────────────────────────────────

SignalPanel::SignalPanel(TciClient* tci, QWidget* parent)
    : QWidget(parent), m_tci(tci)
{
    m_ring.reserve(kRingCap);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    // ── Top control row ─────────────────────────────────────────────────
    auto* top = new QHBoxLayout;
    top->setSpacing(8);

    top->addWidget(new QLabel(QStringLiteral("Stream:")));
    m_streamSel = new QComboBox;
    m_streamSel->addItem(QStringLiteral("Spectrum (type=4, AetherSDR ext)"),
                         (int)Stream::Spectrum);
    m_streamSel->addItem(QStringLiteral("IQ (type=0)"),       (int)Stream::Iq);
    m_streamSel->addItem(QStringLiteral("RX audio (type=1)"), (int)Stream::RxAudio);
    top->addWidget(m_streamSel);

    top->addWidget(new QLabel(QStringLiteral("TRX:")));
    m_trxSel = new QSpinBox;
    m_trxSel->setRange(0, 7);   // TCI spec allows up to 8 receivers
    m_trxSel->setValue(0);
    top->addWidget(m_trxSel);

    m_startStop = new QPushButton(QStringLiteral("Start"));
    m_startStop->setCheckable(false);
    top->addWidget(m_startStop);

    m_freezeBtn = new QPushButton(QStringLiteral("Freeze"));
    m_freezeBtn->setCheckable(true);
    top->addWidget(m_freezeBtn);

    top->addStretch(1);

    m_status = new QLabel(QStringLiteral("idle"));
    m_status->setStyleSheet(QStringLiteral("color:#aab;"));
    top->addWidget(m_status);

    outer->addLayout(top);

    // ── Main plot area: views on the left, header readout on the right ──
    auto* mid = new QHBoxLayout;
    mid->setSpacing(8);

    auto* viewsCol = new QVBoxLayout;
    viewsCol->setSpacing(4);
    m_spectrumView = new SignalSpectrumView;
    m_waterfall    = new SignalWaterfallView;
    viewsCol->addWidget(m_spectrumView, /*stretch*/ 3);
    viewsCol->addWidget(m_waterfall,    /*stretch*/ 4);
    mid->addLayout(viewsCol, /*stretch*/ 1);

    // Header readout panel
    auto* hdrBox = new QGroupBox(QStringLiteral("Last frame header"));
    hdrBox->setMinimumWidth(220);
    hdrBox->setMaximumWidth(280);
    m_hdrGrid = new QGridLayout(hdrBox);
    m_hdrGrid->setContentsMargins(8, 8, 8, 8);
    m_hdrGrid->setHorizontalSpacing(8);
    m_hdrGrid->setVerticalSpacing(2);

    auto addField = [this](int row, const QString& label, QLabel*& tgt) {
        auto* lbl = new QLabel(label);
        lbl->setStyleSheet(QStringLiteral("color:#9ab; font-weight:bold;"));
        tgt = new QLabel(QStringLiteral("—"));
        tgt->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_hdrGrid->addWidget(lbl, row, 0);
        m_hdrGrid->addWidget(tgt, row, 1);
    };
    int r = 0;
    addField(r++, QStringLiteral("receiver"),    m_hdrReceiver);
    addField(r++, QStringLiteral("sampleRate"),  m_hdrSampleRate);
    addField(r++, QStringLiteral("format"),      m_hdrFormat);
    addField(r++, QStringLiteral("codec"),       m_hdrCodec);
    addField(r++, QStringLiteral("crc"),         m_hdrCrc);
    addField(r++, QStringLiteral("length"),      m_hdrLength);
    addField(r++, QStringLiteral("type"),        m_hdrType);
    addField(r++, QStringLiteral("channels"),    m_hdrChannels);
    for (int i = 0; i < 8; ++i)
        addField(r++, QStringLiteral("reserved[%1]").arg(i), m_hdrReserved[i]);
    addField(r++, QStringLiteral("counter"),     m_hdrCounter);
    addField(r++, QStringLiteral("wall clock"),  m_hdrWallClock);
    m_hdrGrid->setRowStretch(r, 1);
    mid->addWidget(hdrBox, /*stretch*/ 0);

    outer->addLayout(mid, /*stretch*/ 1);

    // ── Scrub strip (only meaningful when frozen) ───────────────────────
    auto* scrubRow = new QHBoxLayout;
    m_scrubLabel = new QLabel(QStringLiteral("Scrub:"));
    m_scrub = new QSlider(Qt::Horizontal);
    m_scrub->setRange(0, 0);
    m_scrub->setEnabled(false);
    scrubRow->addWidget(m_scrubLabel);
    scrubRow->addWidget(m_scrub, 1);
    outer->addLayout(scrubRow);

    // ── Signals ─────────────────────────────────────────────────────────
    connect(m_streamSel, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SignalPanel::onStreamChanged);
    connect(m_trxSel, qOverload<int>(&QSpinBox::valueChanged),
            this, &SignalPanel::onTrxChanged);
    connect(m_startStop, &QPushButton::clicked,
            this, &SignalPanel::onStartStopClicked);
    connect(m_freezeBtn, &QPushButton::toggled,
            this, &SignalPanel::onFreezeToggled);
    connect(m_scrub, &QSlider::valueChanged,
            this, &SignalPanel::onScrubChanged);

    // Periodic stats refresh (so fps doesn't update only on frame ingestion —
    // important when the stream stops entirely).
    auto* statsTick = new QTimer(this);
    statsTick->setInterval(500);
    connect(statsTick, &QTimer::timeout, this, &SignalPanel::refreshStatsStrip);
    statsTick->start();
}

// ── Slot wiring  ────────────────────────────────────────────────────────

void SignalPanel::onConnectionChanged(bool connected)
{
    if (!connected) {
        // Server went away — stop showing 'running'; we will *not* tear down
        // the ring buffer or freeze state, so the operator's last view stays
        // on screen for inspection until they hit Start again.
        m_running = false;
        if (m_startStop) m_startStop->setText(QStringLiteral("Start"));
        if (m_status)    m_status->setText(QStringLiteral("disconnected"));
        return;
    }
    // Reconnect — if the user had a stream running before, transparently
    // re-subscribe so they don't have to click Start again across a
    // network blip.
    if (m_running) {
        sendSubscribe(m_currentStream, m_currentTrx);
    }
}

void SignalPanel::onStreamChanged(int idx)
{
    const auto newStream =
        static_cast<Stream>(m_streamSel->itemData(idx).toInt());
    if (newStream == m_currentStream) return;

    // If we're currently subscribed, swap subscriptions atomically.
    if (m_running) {
        sendUnsubscribe(m_currentStream, m_currentTrx);
        m_currentStream = newStream;
        sendSubscribe(m_currentStream, m_currentTrx);
    } else {
        m_currentStream = newStream;
    }

    // Stream change invalidates the ring buffer (header layout differs and
    // mixing samples from different streams in one waterfall is misleading).
    m_ring.clear();
    m_framesSeen = 0;
    m_framesRejected = 0;
    m_emaFps = 0.0;
    if (m_spectrumView) m_spectrumView->clearFrame();
    if (m_waterfall)    m_waterfall->clearImage();
    refreshStatsStrip();
}

void SignalPanel::onTrxChanged(int trx)
{
    if (trx == m_currentTrx) return;
    if (m_running) {
        sendUnsubscribe(m_currentStream, m_currentTrx);
        m_currentTrx = trx;
        sendSubscribe(m_currentStream, m_currentTrx);
    } else {
        m_currentTrx = trx;
    }
    // Same rationale as stream change.
    m_ring.clear();
    if (m_spectrumView) m_spectrumView->clearFrame();
    if (m_waterfall)    m_waterfall->clearImage();
}

void SignalPanel::onStartStopClicked()
{
    if (!m_running) {
        sendSubscribe(m_currentStream, m_currentTrx);
        m_running = true;
        m_startStop->setText(QStringLiteral("Stop"));
    } else {
        sendUnsubscribe(m_currentStream, m_currentTrx);
        m_running = false;
        m_startStop->setText(QStringLiteral("Start"));
    }
}

void SignalPanel::onFreezeToggled(bool frozen)
{
    m_frozen = frozen;
    if (frozen) {
        // Enable scrub slider scoped to current ring contents.
        const int n = m_ring.size();
        m_scrub->setEnabled(n > 1);
        m_scrub->setRange(0, std::max(0, n - 1));
        m_scrub->setValue(std::max(0, n - 1));  // start at newest
        m_freezeBtn->setText(QStringLiteral("Unfreeze"));
    } else {
        m_scrub->setEnabled(false);
        m_freezeBtn->setText(QStringLiteral("Freeze"));
    }
}

void SignalPanel::onScrubChanged(int idx)
{
    if (!m_frozen) return;
    if (idx < 0 || idx >= m_ring.size()) return;
    renderFrame(m_ring[idx]);
    updateHeaderReadout(m_ring[idx]);
}

// ── Ingestion ───────────────────────────────────────────────────────────

void SignalPanel::ingestFrame(const TciBinaryFrame& f)
{
    // Filter on (stream, trx). Frames for other receivers / other types
    // are dropped silently so multiple panels can coexist with multiple
    // active subscriptions — including, eventually, the Inspector tab if
    // we add binary-frame inspection there.
    if (!frameMatches(f)) {
        // We still want to know if we're matching nothing — bump a separate
        // counter so the operator can see "frames are arriving, but not
        // mine" in the status strip.
        ++m_framesRejected;
        return;
    }

    ++m_framesSeen;

    // Smoothed fps (only meaningful when frames arrive at a steady rate)
    const qint64 now = f.wallClockNs;
    if (m_lastFrameNs != 0) {
        const double dtSec = (now - m_lastFrameNs) / 1e9;
        if (dtSec > 0.0 && dtSec < 5.0) {
            const double instantFps = 1.0 / dtSec;
            // EMA with alpha=0.15 — quick enough to react, slow enough that
            // a single jittery interval doesn't whipsaw the readout.
            m_emaFps = (m_emaFps == 0.0) ? instantFps
                                          : (0.85 * m_emaFps + 0.15 * instantFps);
        }
    }
    m_lastFrameNs = now;

    // Append to ring (drop oldest if at cap)
    if (m_ring.size() >= kRingCap) m_ring.removeFirst();
    m_ring.append(f);

    if (m_frozen) {
        // Frozen: don't render the live frame, but stretch the scrub slider
        // so the operator can reach the new tail if they want.
        m_scrub->blockSignals(true);
        m_scrub->setRange(0, m_ring.size() - 1);
        m_scrub->blockSignals(false);
        return;
    }

    renderFrame(f);
    updateHeaderReadout(f);
}

bool SignalPanel::frameMatches(const TciBinaryFrame& f) const
{
    if (f.type != expectedHeaderType(m_currentStream)) return false;
    // SPECTRUM frames carry the producing TRX in hdr.receiver too — we
    // honour the same filter on all three streams so the operator can
    // select per-slice.
    if (static_cast<int>(f.receiver) != m_currentTrx) return false;
    return true;
}

// ── Rendering ───────────────────────────────────────────────────────────

void SignalPanel::renderFrame(const TciBinaryFrame& f)
{
    // Convert payload bytes to a QVector<float> in a stream-appropriate way.
    // We standardise on float here so the line/waterfall views don't need
    // to know which stream is feeding them.
    QVector<float> samples;

    switch (m_currentStream) {
    case Stream::Spectrum: {
        // dBm float32 bins; payload length = f.length * sizeof(float).
        const int n = std::min<int>(
            static_cast<int>(f.length),
            static_cast<int>(f.payload.size() / sizeof(float)));
        samples.resize(n);
        if (n > 0) {
            std::memcpy(samples.data(), f.payload.constData(), n * sizeof(float));
        }
        m_spectrumView->setFrame(samples, QStringLiteral("dBm"));
        m_waterfall->pushRow(samples);
        break;
    }
    case Stream::Iq: {
        // Interleaved I/Q float32 pairs; show |I+jQ| magnitude per pair.
        const int totalFloats = static_cast<int>(f.payload.size() / sizeof(float));
        const int pairs = totalFloats / 2;
        samples.resize(pairs);
        const auto* src = reinterpret_cast<const float*>(f.payload.constData());
        for (int i = 0; i < pairs; ++i) {
            const float I = src[2 * i + 0];
            const float Q = src[2 * i + 1];
            samples[i] = std::sqrt(I * I + Q * Q);
        }
        m_spectrumView->setFrame(samples, QStringLiteral("|IQ|"));
        m_waterfall->pushRow(samples);
        break;
    }
    case Stream::RxAudio: {
        // Per the TCI header, format identifies sample width. AetherSDR
        // currently emits float32 for RX audio (format=3) and either
        // mono or stereo interleaved. We collapse to mono-mean for display.
        if (f.format == 3) {
            const int totalFloats = static_cast<int>(f.payload.size() / sizeof(float));
            const int ch = std::max<int>(1, f.channels);
            const int frames = totalFloats / ch;
            samples.resize(frames);
            const auto* src = reinterpret_cast<const float*>(f.payload.constData());
            for (int i = 0; i < frames; ++i) {
                float acc = 0.0f;
                for (int c = 0; c < ch; ++c) acc += src[i * ch + c];
                samples[i] = acc / ch;
            }
        } else if (f.format == 0) {
            // int16 fallback — convert in-flight.
            const int totalShorts = static_cast<int>(f.payload.size() / sizeof(qint16));
            const int ch = std::max<int>(1, f.channels);
            const int frames = totalShorts / ch;
            samples.resize(frames);
            const auto* src = reinterpret_cast<const qint16*>(f.payload.constData());
            for (int i = 0; i < frames; ++i) {
                int acc = 0;
                for (int c = 0; c < ch; ++c) acc += src[i * ch + c];
                samples[i] = (acc / ch) / 32768.0f;
            }
        }
        m_spectrumView->setFrame(samples, QStringLiteral("PCM"));
        m_waterfall->pushRow(samples);
        break;
    }
    }
}

void SignalPanel::updateHeaderReadout(const TciBinaryFrame& f)
{
    auto u32 = [](quint32 v) { return QString::number(v); };

    m_hdrReceiver->setText(u32(f.receiver));
    m_hdrSampleRate->setText(QStringLiteral("%1 Hz").arg(f.sampleRate));

    static const QStringList kFormatNames = {
        QStringLiteral("0 (int16)"),
        QStringLiteral("1 (int24)"),
        QStringLiteral("2 (int32)"),
        QStringLiteral("3 (float32)"),
    };
    m_hdrFormat->setText(f.format < (quint32)kFormatNames.size()
                             ? kFormatNames[f.format] : u32(f.format));
    m_hdrCodec->setText(u32(f.codec));
    m_hdrCrc->setText(u32(f.crc));
    m_hdrLength->setText(u32(f.length));

    static const QStringList kTypeNames = {
        QStringLiteral("0 (IQ)"),
        QStringLiteral("1 (RX_AUDIO)"),
        QStringLiteral("2 (TX_AUDIO)"),
        QStringLiteral("3 (TX_CHRONO)"),
        QStringLiteral("4 (SPECTRUM)"),
    };
    m_hdrType->setText(f.type < (quint32)kTypeNames.size()
                           ? kTypeNames[f.type] : u32(f.type));
    m_hdrChannels->setText(u32(f.channels));
    for (int i = 0; i < 8; ++i) {
        QString cell = u32(f.reserved[i]);
        // For SPECTRUM frames reserved[0..1] carry low/high band edges in Hz —
        // surface that as a parenthetical so the operator doesn't have to
        // remember the convention.
        if (f.type == 4) {
            if (i == 0) cell += QStringLiteral(" (low %1 MHz)")
                                     .arg(f.reserved[0] / 1.0e6, 0, 'f', 6);
            if (i == 1) cell += QStringLiteral(" (high %1 MHz)")
                                     .arg(f.reserved[1] / 1.0e6, 0, 'f', 6);
        }
        m_hdrReserved[i]->setText(cell);
    }
    m_hdrCounter->setText(u32(f.counter));
    // Wall clock — show seconds since the client started capturing frames
    // (monotonic), not system time, since the frame timestamp is a
    // QElapsedTimer reading by design.
    m_hdrWallClock->setText(QStringLiteral("%1 s (mono)")
                                .arg(f.wallClockNs / 1e9, 0, 'f', 3));
}

void SignalPanel::refreshStatsStrip()
{
    if (!m_status) return;
    QStringList parts;
    parts << (m_running ? QStringLiteral("subscribed") : QStringLiteral("idle"));
    parts << QStringLiteral("seen %1").arg(m_framesSeen);
    if (m_framesRejected > 0)
        parts << QStringLiteral("filtered %1").arg(m_framesRejected);
    if (m_emaFps > 0.0)
        parts << QStringLiteral("%1 fps").arg(m_emaFps, 0, 'f', 1);
    if (m_frozen)
        parts << QStringLiteral("[FROZEN]");
    m_status->setText(parts.join(QStringLiteral("  •  ")));
}

// ── Subscription wire commands ──────────────────────────────────────────

const char* SignalPanel::streamName(Stream s)
{
    switch (s) {
    case Stream::Spectrum: return "spectrum";
    case Stream::Iq:       return "iq";
    case Stream::RxAudio:  return "rx-audio";
    }
    return "?";
}

quint32 SignalPanel::expectedHeaderType(Stream s)
{
    switch (s) {
    case Stream::Spectrum: return 4;
    case Stream::Iq:       return 0;
    case Stream::RxAudio:  return 1;
    }
    return 0;
}

void SignalPanel::sendSubscribe(Stream s, int trx)
{
    if (!m_tci || !m_tci->connected()) return;
    switch (s) {
    case Stream::Spectrum:
        // SPECTRUM is AetherSDR-only; servers that don't speak it will
        // silently ignore the command (TCI servers MUST be tolerant of
        // unknown commands per spec).
        m_tci->send(QStringLiteral("spectrum_event:on;"));
        break;
    case Stream::Iq:
        m_tci->send(QStringLiteral("iq_start:%1;").arg(trx));
        break;
    case Stream::RxAudio:
        m_tci->send(QStringLiteral("audio_start:%1;").arg(trx));
        break;
    }
}

void SignalPanel::sendUnsubscribe(Stream s, int trx)
{
    if (!m_tci || !m_tci->connected()) return;
    switch (s) {
    case Stream::Spectrum:
        m_tci->send(QStringLiteral("spectrum_event:off;"));
        break;
    case Stream::Iq:
        m_tci->send(QStringLiteral("iq_stop:%1;").arg(trx));
        break;
    case Stream::RxAudio:
        m_tci->send(QStringLiteral("audio_stop:%1;").arg(trx));
        break;
    }
}

} // namespace TciMon
