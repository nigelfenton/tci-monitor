#pragma once

// SignalPanel — live view of TCI binary streams (spectrum / IQ / RX audio).
//
// What it does
//   * Subscribes to one of three TCI binary streams at a time via the
//     appropriate text command (spectrum_event:on, iq_start:<trx>,
//     audio_start:<trx>).
//   * Shows the latest frame as a line plot (top) and a scrolling
//     waterfall (bottom) — same paradigm as a panadapter.
//   * Shows every field of the frame's 64-byte header in a side panel so
//     you can sanity-check what the radio is sending.
//   * Freeze pauses live updates; a scrub slider then walks back through
//     the last ~300 frames (~15 s of spectrum at 20 fps; ~3 s of audio
//     at 100 fps) without dropping the stream connection.
//
// What it does NOT do
//   * No client-side FFT of IQ — the IQ "line plot" shows the time-domain
//     |sample| envelope of the latest frame. If you want a real FFT'd
//     spectrum, pick the Spectrum stream (the radio has already done the FFT).
//   * No audio playback — RX audio frames are displayed visually only.

#include <QByteArray>
#include <QVector>
#include <QWidget>

#include "TciClient.h"   // TciBinaryFrame

class QComboBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

namespace TciMon {

class TciClient;
class SignalSpectrumView;
class SignalWaterfallView;

class SignalPanel : public QWidget {
    Q_OBJECT
public:
    enum class Stream { Spectrum = 0, Iq = 1, RxAudio = 2 };

    explicit SignalPanel(TciClient* tci, QWidget* parent = nullptr);

public slots:
    // Connect TciClient::binaryFrameReceived → this.
    void ingestFrame(const TciMon::TciBinaryFrame& f);

    // Connect TciClient::connectionChanged → this so we can re-subscribe
    // automatically across reconnects.
    void onConnectionChanged(bool connected);

private slots:
    void onStreamChanged(int idx);
    void onTrxChanged(int trx);
    void onStartStopClicked();
    void onFreezeToggled(bool frozen);
    void onScrubChanged(int idx);

private:
    // ── Subscription plumbing ────────────────────────────────────────────
    void sendSubscribe(Stream s, int trx);
    void sendUnsubscribe(Stream s, int trx);
    static const char* streamName(Stream s);
    static quint32     expectedHeaderType(Stream s);  // matches hdr.type

    // ── Frame plumbing ───────────────────────────────────────────────────
    // Filter inbound frames against currently selected (stream, trx).
    // Returns true if the frame is for our selection and should be drawn.
    bool frameMatches(const TciBinaryFrame& f) const;

    // Display the given frame (live or scrubbed-from-buffer).
    void renderFrame(const TciBinaryFrame& f);

    // Update the header-values readout from a frame.
    void updateHeaderReadout(const TciBinaryFrame& f);

    // Update fps / rate / accepted-rejected counters in the status strip.
    void refreshStatsStrip();

    // ── Widgets ──────────────────────────────────────────────────────────
    TciClient*           m_tci{nullptr};
    QComboBox*           m_streamSel{nullptr};
    QSpinBox*            m_trxSel{nullptr};
    QPushButton*         m_startStop{nullptr};
    QPushButton*         m_freezeBtn{nullptr};
    QSlider*             m_scrub{nullptr};
    QLabel*              m_scrubLabel{nullptr};
    QLabel*              m_status{nullptr};

    SignalSpectrumView*  m_spectrumView{nullptr};
    SignalWaterfallView* m_waterfall{nullptr};

    // Header readout (right-side grid)
    QGridLayout*         m_hdrGrid{nullptr};
    QLabel*              m_hdrReceiver{nullptr};
    QLabel*              m_hdrSampleRate{nullptr};
    QLabel*              m_hdrFormat{nullptr};
    QLabel*              m_hdrCodec{nullptr};
    QLabel*              m_hdrCrc{nullptr};
    QLabel*              m_hdrLength{nullptr};
    QLabel*              m_hdrType{nullptr};
    QLabel*              m_hdrChannels{nullptr};
    QLabel*              m_hdrReserved[8]{};
    QLabel*              m_hdrCounter{nullptr};
    QLabel*              m_hdrWallClock{nullptr};

    // ── State ────────────────────────────────────────────────────────────
    Stream  m_currentStream{Stream::Spectrum};
    int     m_currentTrx{0};
    bool    m_running{false};
    bool    m_frozen{false};

    // Ring buffer of recent frames (oldest at front, newest at back).
    // Cap is intentionally generous: at 20 fps spectrum, 300 frames = 15 s
    // of history; at 48 kHz audio with 1024-sample frames (~47 fps),
    // ~6 s; deep enough to walk back through transient events.
    static constexpr int kRingCap = 300;
    QVector<TciBinaryFrame> m_ring;

    // Stats — recomputed periodically by refreshStatsStrip().
    quint64 m_framesSeen{0};
    quint64 m_framesRejected{0};
    qint64  m_lastFrameNs{0};
    double  m_emaFps{0.0};   // exponentially smoothed
};

} // namespace TciMon
