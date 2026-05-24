#pragma once

// WaterfallIdPanel — paint text into receivers' waterfalls via SSB TX.
//
// What it does
//   * Renders short text (typically your callsign) as an 8 × 8 pixel
//     bitmap, concatenated horizontally.
//   * Synthesises a TCI TX-audio stream where each row of the bitmap is
//     emitted as a brief multitone burst covering a 2.7 kHz swath of
//     audio.  Receivers see your text appear in their waterfall as their
//     display scrolls — like a Hellschreiber "ticker tape."
//   * Optional CW identifier appended (synthesised 700 Hz keyed tone),
//     because waterfall paint doesn't qualify as ID in most jurisdictions.
//
// Wire format reused from CalibrationPanel: TCI binary audio frames,
// type=2 (TX_AUDIO), 48 kHz float32 stereo (L=R), 50 ms per frame.
//
// Operator workflow
//   1. Open the tab. Type your text. Optionally tick CW ID.
//   2. Set the radio to USB on slice 0 (you do this on the radio side —
//      we intentionally don't touch mode like CalibrationPanel does, to
//      keep this tab feeling like an interactive tool rather than a
//      managed sequence).
//   3. Click Send.  The panel keys trx 0, streams the painted audio,
//      then unkeys.

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTimer;

namespace TciMon {

class TciClient;

class WaterfallIdPanel : public QWidget {
    Q_OBJECT
public:
    explicit WaterfallIdPanel(TciClient* tci, QWidget* parent = nullptr);

public slots:
    void onConnectionChanged(bool connected);
    // Watch incoming TCI lines for slice 0 mode/modulation events so we
    // know what mode to restore at the end of a Send run.
    void onTciLine(const QString& line);

private slots:
    void onSendClicked();
    void onAbortClicked();
    void onSaveWavClicked();
    void onAudioTick();

private:
    // ── Audio synthesis ─────────────────────────────────────────────────
    // Build the painted-text portion of the TX stream.  Each character
    // contributes 8 rows × kCharColumns of bitmap.  Output is a flat
    // mono float32 buffer at kAudioRate.
    static QVector<float> buildPaintedAudio(const QString& text,
                                            int rowMs, float bandwidthHz,
                                            float centerHz);

    // Build the CW ID portion: keyed 700 Hz tone for `callsign` at `wpm`.
    static QVector<float> buildCwIdAudio(const QString& callsign,
                                         int wpm, float cwToneHz);

    // Pack a 50 ms slice (kFrameSamples mono samples) into a TCI binary
    // TX-audio frame (64-byte TciAudioHeader + float32 stereo payload).
    static QByteArray packTxFrame(const float* mono, int monoSamples);

    // Resolve text to a per-row uint64 mask of "on" columns.  Caller
    // sizes the column count from text length.
    struct TextBitmap {
        int cols{0};           // total columns (text.size() * kCharColumns)
        QVector<quint64> rows; // rows[r] bit c (LSB = leftmost col) = pixel on
        static constexpr int kRows = 8;
    };
    static TextBitmap textToBitmap(const QString& text);

    // ── UI helpers ──────────────────────────────────────────────────────
    void log(const QString& msg, const QString& colorHex = QString());
    void setRunning(bool running);

    // ── Members ─────────────────────────────────────────────────────────
    TciClient*    m_tci{nullptr};

    QLineEdit*    m_textEdit{nullptr};
    QCheckBox*    m_cwIdEnable{nullptr};
    QSpinBox*     m_cwWpm{nullptr};
    QSpinBox*     m_rowMs{nullptr};
    QPushButton*  m_sendBtn{nullptr};
    QPushButton*  m_abortBtn{nullptr};
    QPushButton*  m_saveWavBtn{nullptr};
    QProgressBar* m_progress{nullptr};
    QLabel*       m_status{nullptr};
    QLabel*       m_logLabel{nullptr};

    // Streaming state — once Send is clicked, we slice the synthesised
    // audio into 50 ms chunks and ship one chunk per m_audio timer tick.
    QTimer*           m_audio{nullptr};
    QVector<float>    m_buffer;      // full mono audio (paint + optional CW)
    int               m_bufferPos{0};
    bool              m_streaming{false};

    // Slice 0 mode tracking — AE's TciServer only routes TCI audio to
    // dax_tx when slice 0 is in a digital mode (digu/digl/rtty/fdv*).
    // We track the current mode from TCI events and, if Send is clicked
    // while a voice mode is active, save it, force DIGU, and restore the
    // original on completion / abort / disconnect. Same pattern as
    // CalibrationPanel (TX Cal tab, commit 9a470af).
    QString           m_currentMode;
    QString           m_savedMode;

    // Constants — kept in the header for the static synthesis helpers
    // to share with the streaming logic.
    static constexpr int   kAudioRate    = 48000;
    static constexpr int   kFrameMs      = 50;
    static constexpr int   kFrameSamples = kAudioRate * kFrameMs / 1000; // 2400

    // Default tone-painter geometry.  These ARE configurable via the row-ms
    // spinner, but the bandwidth + centre stay fixed to standard SSB.
    static constexpr float kBandwidthHz  = 2700.0f;
    static constexpr float kCenterHz     = 1500.0f;  // 150 … 2850 Hz at default
    static constexpr float kCwToneHz     = 700.0f;
};

} // namespace TciMon
