#include "WaterfallIdPanel.h"

#include "TciClient.h"

#include <QByteArray>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace TciMon {

// ─────────────────────────────────────────────────────────────────────────
// 8 × 8 pixel font — minimal public-domain set covering A-Z, 0-9, a few
// punctuation marks needed for callsigns (slash, hyphen, space).  Each
// glyph is 8 bytes; byte r = row r (top-to-bottom in glyph space; bit 0
// = leftmost column).  Glyphs include their own one-pixel-wide trailing
// blank column for letter spacing.
//
// This is hand-crafted, deliberately simple (5-wide letters in an 8-wide
// box).  Not perfect typography — just legible at typical waterfall
// resolutions (~30-80 Hz/bin).
// ─────────────────────────────────────────────────────────────────────────
namespace {

constexpr int kCharColumns = 8;   // glyph width (including trailing pad)
constexpr int kRampMs      = 5;   // per-row attack/release ramp

constexpr double kPi = 3.14159265358979323846;

// Append a little-endian uint32 / float32 to a QByteArray — matches
// CalibrationPanel's putU32/putF32 so the wire format is identical.
inline void putU32(QByteArray& b, quint32 v) {
    char d[4] = { char(v & 0xff), char((v >> 8) & 0xff),
                  char((v >> 16) & 0xff), char((v >> 24) & 0xff) };
    b.append(d, 4);
}
inline void putF32(QByteArray& b, float f) {
    quint32 u; std::memcpy(&u, &f, 4); putU32(b, u);
}

// Glyph table.  Lookup by ASCII char in [32, 95]; out-of-range → space.
struct Glyph { quint8 row[8]; };
constexpr Glyph kFont[64] = {
    /* 32 ' ' */ {{0,0,0,0,0,0,0,0}},
    /* 33 '!' */ {{0x04,0x04,0x04,0x04,0x04,0x00,0x04,0x00}},
    /* 34 '"' */ {{0x0A,0x0A,0x00,0x00,0x00,0x00,0x00,0x00}},
    /* 35 '#' */ {{0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A,0x00}},
    /* 36 '$' */ {{0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04,0x00}},
    /* 37 '%' */ {{0x18,0x19,0x02,0x04,0x08,0x13,0x03,0x00}},
    /* 38 '&' */ {{0x08,0x14,0x14,0x08,0x15,0x12,0x0D,0x00}},
    /* 39 '\''*/ {{0x04,0x04,0x00,0x00,0x00,0x00,0x00,0x00}},
    /* 40 '(' */ {{0x02,0x04,0x08,0x08,0x08,0x04,0x02,0x00}},
    /* 41 ')' */ {{0x08,0x04,0x02,0x02,0x02,0x04,0x08,0x00}},
    /* 42 '*' */ {{0x00,0x04,0x15,0x0E,0x15,0x04,0x00,0x00}},
    /* 43 '+' */ {{0x00,0x04,0x04,0x1F,0x04,0x04,0x00,0x00}},
    /* 44 ',' */ {{0x00,0x00,0x00,0x00,0x00,0x04,0x04,0x08}},
    /* 45 '-' */ {{0x00,0x00,0x00,0x1F,0x00,0x00,0x00,0x00}},
    /* 46 '.' */ {{0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00}},
    /* 47 '/' */ {{0x00,0x01,0x02,0x04,0x08,0x10,0x00,0x00}},
    /* 48 '0' */ {{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E,0x00}},
    /* 49 '1' */ {{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E,0x00}},
    /* 50 '2' */ {{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F,0x00}},
    /* 51 '3' */ {{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E,0x00}},
    /* 52 '4' */ {{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02,0x00}},
    /* 53 '5' */ {{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E,0x00}},
    /* 54 '6' */ {{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E,0x00}},
    /* 55 '7' */ {{0x1F,0x01,0x02,0x04,0x08,0x08,0x08,0x00}},
    /* 56 '8' */ {{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E,0x00}},
    /* 57 '9' */ {{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C,0x00}},
    /* 58 ':' */ {{0x00,0x04,0x00,0x00,0x00,0x04,0x00,0x00}},
    /* 59 ';' */ {{0x00,0x04,0x00,0x00,0x00,0x04,0x04,0x08}},
    /* 60 '<' */ {{0x02,0x04,0x08,0x10,0x08,0x04,0x02,0x00}},
    /* 61 '=' */ {{0x00,0x00,0x1F,0x00,0x1F,0x00,0x00,0x00}},
    /* 62 '>' */ {{0x08,0x04,0x02,0x01,0x02,0x04,0x08,0x00}},
    /* 63 '?' */ {{0x0E,0x11,0x01,0x02,0x04,0x00,0x04,0x00}},
    /* 64 '@' */ {{0x0E,0x11,0x17,0x15,0x17,0x10,0x0E,0x00}},
    /* 65 'A' */ {{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11,0x00}},
    /* 66 'B' */ {{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E,0x00}},
    /* 67 'C' */ {{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E,0x00}},
    /* 68 'D' */ {{0x1C,0x12,0x11,0x11,0x11,0x12,0x1C,0x00}},
    /* 69 'E' */ {{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F,0x00}},
    /* 70 'F' */ {{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10,0x00}},
    /* 71 'G' */ {{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F,0x00}},
    /* 72 'H' */ {{0x11,0x11,0x11,0x1F,0x11,0x11,0x11,0x00}},
    /* 73 'I' */ {{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E,0x00}},
    /* 74 'J' */ {{0x07,0x02,0x02,0x02,0x02,0x12,0x0C,0x00}},
    /* 75 'K' */ {{0x11,0x12,0x14,0x18,0x14,0x12,0x11,0x00}},
    /* 76 'L' */ {{0x10,0x10,0x10,0x10,0x10,0x10,0x1F,0x00}},
    /* 77 'M' */ {{0x11,0x1B,0x15,0x15,0x11,0x11,0x11,0x00}},
    /* 78 'N' */ {{0x11,0x11,0x19,0x15,0x13,0x11,0x11,0x00}},
    /* 79 'O' */ {{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E,0x00}},
    /* 80 'P' */ {{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10,0x00}},
    /* 81 'Q' */ {{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D,0x00}},
    /* 82 'R' */ {{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11,0x00}},
    /* 83 'S' */ {{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E,0x00}},
    /* 84 'T' */ {{0x1F,0x04,0x04,0x04,0x04,0x04,0x04,0x00}},
    /* 85 'U' */ {{0x11,0x11,0x11,0x11,0x11,0x11,0x0E,0x00}},
    /* 86 'V' */ {{0x11,0x11,0x11,0x11,0x11,0x0A,0x04,0x00}},
    /* 87 'W' */ {{0x11,0x11,0x11,0x15,0x15,0x15,0x0A,0x00}},
    /* 88 'X' */ {{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11,0x00}},
    /* 89 'Y' */ {{0x11,0x11,0x11,0x0A,0x04,0x04,0x04,0x00}},
    /* 90 'Z' */ {{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F,0x00}},
    /* 91 '[' */ {{0x0E,0x08,0x08,0x08,0x08,0x08,0x0E,0x00}},
    /* 92 '\\'*/ {{0x00,0x10,0x08,0x04,0x02,0x01,0x00,0x00}},
    /* 93 ']' */ {{0x0E,0x02,0x02,0x02,0x02,0x02,0x0E,0x00}},
    /* 94 '^' */ {{0x04,0x0A,0x11,0x00,0x00,0x00,0x00,0x00}},
    /* 95 '_' */ {{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1F}},
};

const Glyph& glyphFor(QChar ch)
{
    quint16 c = ch.toUpper().unicode();
    if (c < 32 || c > 95) c = 32;   // unknowns → space
    return kFont[c - 32];
}

// Morse table for the CW ID — uppercase letters + digits + the few
// punctuation marks legal in callsigns (/, -).  Anything else is dropped.
QString morseFor(QChar ch)
{
    static const struct { char c; const char* code; } table[] = {
        {'A',".-"},   {'B',"-..."}, {'C',"-.-."}, {'D',"-.."},
        {'E',"."},    {'F',"..-."}, {'G',"--."},  {'H',"...."},
        {'I',".."},   {'J',".---"}, {'K',"-.-"},  {'L',".-.."},
        {'M',"--"},   {'N',"-."},   {'O',"---"},  {'P',".--."},
        {'Q',"--.-"}, {'R',".-."},  {'S',"..."},  {'T',"-"},
        {'U',"..-"},  {'V',"...-"}, {'W',".--"},  {'X',"-..-"},
        {'Y',"-.--"}, {'Z',"--.."},
        {'0',"-----"},{'1',".----"},{'2',"..---"},{'3',"...--"},
        {'4',"....-"},{'5',"....."},{'6',"-...."},{'7',"--..."},
        {'8',"---.."},{'9',"----."},
        {'/',"-..-."},{'-',"-....-"},
    };
    char c = ch.toUpper().toLatin1();
    for (const auto& e : table)
        if (e.c == c) return QString::fromLatin1(e.code);
    return QString();
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// Synthesis
// ─────────────────────────────────────────────────────────────────────────

WaterfallIdPanel::TextBitmap WaterfallIdPanel::textToBitmap(const QString& text)
{
    TextBitmap bm;
    bm.cols = text.size() * kCharColumns;
    bm.rows.fill(0, TextBitmap::kRows);

    for (int ci = 0; ci < text.size(); ++ci) {
        const Glyph& g = glyphFor(text[ci]);
        const int colBase = ci * kCharColumns;
        for (int r = 0; r < TextBitmap::kRows; ++r) {
            // Glyph rows are stored MSB-on-the-LEFT (bit 4 = column 0
            // within glyph).  Flip into the bitmap's LSB-on-the-LEFT
            // convention so column 0 = leftmost text column.
            quint8 glyphRow = g.row[r];
            for (int gc = 0; gc < 5; ++gc) {        // glyph is 5 cols wide
                if (glyphRow & (1 << (4 - gc))) {
                    bm.rows[r] |= (quint64(1) << (colBase + gc));
                }
            }
        }
    }
    return bm;
}

QVector<float> WaterfallIdPanel::buildPaintedAudio(const QString& text,
                                                   int rowMs,
                                                   float bandwidthHz,
                                                   float centerHz)
{
    if (text.isEmpty() || rowMs <= 0) return {};

    const TextBitmap bm = textToBitmap(text);
    if (bm.cols <= 0) return {};

    // Map columns onto the audio passband.  We always span the full
    // requested bandwidth, so a short callsign gets coarse columns and
    // a long string gets fine ones.  All columns are inside the SSB
    // filter as long as centerHz ± bandwidthHz/2 stays within ~150-3000 Hz.
    const float colSpacingHz = bandwidthHz / float(bm.cols);
    const float lowHz = centerHz - bandwidthHz / 2.0f;

    // Pre-compute the per-column frequency, *centered* in each column's
    // slot — receivers' FFT bins straddle, so centering gives the cleanest
    // peak.
    QVector<float> colFreq(bm.cols);
    for (int c = 0; c < bm.cols; ++c)
        colFreq[c] = lowHz + (float(c) + 0.5f) * colSpacingHz;

    // Per-column phase offset using the golden-ratio low-discrepancy
    // sequence — c * φ⁻¹ mod 1, scaled to 2π.  Without this, all tones
    // start at phase 0 at t=0 and constructively phase-align, pushing the
    // crest factor up to N (and clipping at int16 conversion).  Staggered
    // phases keep peak ≈ √N while leaving the tone frequencies — and
    // therefore the receiver's recovered bitmap — completely unchanged.
    constexpr double kPhiInv = 0.6180339887498949;  // (√5 - 1) / 2
    QVector<float> colPhase(bm.cols);
    for (int c = 0; c < bm.cols; ++c) {
        double frac = double(c) * kPhiInv;
        frac -= std::floor(frac);
        colPhase[c] = float(2.0 * kPi * frac);
    }

    const int rowSamples  = kAudioRate * rowMs / 1000;
    const int rampSamples = kAudioRate * kRampMs / 1000;
    QVector<float> out;
    out.reserve(rowSamples * TextBitmap::kRows);

    // Emit rows BOTTOM-TO-TOP in time order.  Standard waterfalls scroll
    // newest-at-top; emitting bottom row first means it ends up at the
    // bottom of the display, with the top row at the top.  Net result:
    // text reads right-side-up.
    for (int r = TextBitmap::kRows - 1; r >= 0; --r) {
        // Collect "on" frequencies AND their per-column phases for this row.
        QVector<float> onFreqs;
        QVector<float> onPhases;
        for (int c = 0; c < bm.cols; ++c) {
            if (bm.rows[r] & (quint64(1) << c)) {
                onFreqs.append(colFreq[c]);
                onPhases.append(colPhase[c]);
            }
        }

        // Per-row normalisation: divide by sqrt(N) so the RMS stays
        // sensible whether there are 2 tones or 40.  Combined with the
        // staggered phases this keeps peak comfortably below 1.0 even on
        // dense rows, while keeping thin rows audible.
        const float gain = onFreqs.isEmpty()
            ? 0.0f
            : 0.85f / std::sqrt(float(onFreqs.size()));

        for (int n = 0; n < rowSamples; ++n) {
            float s = 0.0f;
            const double t = double(n) / kAudioRate;
            for (int k = 0; k < onFreqs.size(); ++k)
                s += std::sin(2.0 * kPi * onFreqs[k] * t + onPhases[k]);
            s *= gain;

            // Raised-cosine envelope on both ends of the row to suppress
            // the keying-click spectrum that a hard on/off would splatter
            // across the band.
            float env = 1.0f;
            if (n < rampSamples) {
                env = 0.5f * (1.0f - std::cos(kPi * float(n) / rampSamples));
            } else if (n > rowSamples - rampSamples) {
                env = 0.5f * (1.0f - std::cos(kPi * float(rowSamples - n) / rampSamples));
            }
            out.append(s * env);
        }
    }

    // Final peak normalisation.  Staggered phases reduce — but don't
    // eliminate — moments where many tones momentarily align after t=0
    // (each tone drifts through relative phase at its own rate).  One
    // scan to find the peak, one multiply to scale, guarantees no
    // clipping when the buffer is converted to int16 for the WAV file
    // or to the TCI binary stream.  Target 0.9 leaves a 1 dB headroom
    // for any downstream resampling.
    float peakAbs = 0.0f;
    for (float s : out) {
        const float a = std::fabs(s);
        if (a > peakAbs) peakAbs = a;
    }
    if (peakAbs > 0.9f) {
        const float scale = 0.9f / peakAbs;
        for (float& s : out) s *= scale;
    }
    return out;
}

QVector<float> WaterfallIdPanel::buildCwIdAudio(const QString& callsign,
                                                int wpm, float cwToneHz)
{
    if (callsign.isEmpty() || wpm <= 0) return {};

    // Standard PARIS timing: 1 unit = 1200 / WPM milliseconds.
    const int unitMs    = std::max(1, 1200 / wpm);
    const int unitSamps = kAudioRate * unitMs / 1000;
    const int ramp      = std::max(1, kAudioRate * 4 / 1000);  // 4 ms ramp

    QVector<float> out;

    auto appendTone = [&](int totalSamples) {
        for (int n = 0; n < totalSamples; ++n) {
            float env = 1.0f;
            if (n < ramp) env = 0.5f * (1.0f - std::cos(kPi * float(n) / ramp));
            else if (n > totalSamples - ramp)
                env = 0.5f * (1.0f - std::cos(kPi * float(totalSamples - n) / ramp));
            const double t = double(n) / kAudioRate;
            out.append(0.85f * env * float(std::sin(2.0 * kPi * cwToneHz * t)));
        }
    };
    auto appendSilence = [&](int samples) {
        for (int n = 0; n < samples; ++n) out.append(0.0f);
    };

    // Small leading pause to separate from the painted text.
    appendSilence(unitSamps * 3);

    bool firstChar = true;
    for (QChar ch : callsign.toUpper()) {
        if (ch == ' ') {                  // word gap = 7 units total
            appendSilence(unitSamps * 4); // 3 from prior char-gap → 7 total
            firstChar = true;
            continue;
        }
        const QString code = morseFor(ch);
        if (code.isEmpty()) continue;

        if (!firstChar) appendSilence(unitSamps * 3);  // char gap
        firstChar = false;

        for (int i = 0; i < code.size(); ++i) {
            if (i > 0) appendSilence(unitSamps);          // intra-element gap
            appendTone(unitSamps * (code[i] == '-' ? 3 : 1));
        }
    }
    return out;
}

QByteArray WaterfallIdPanel::packTxFrame(const float* mono, int monoSamples)
{
    QByteArray f;
    f.reserve(64 + monoSamples * 2 * 4);
    // 64-byte TciAudioHeader, identical layout to CalibrationPanel's
    // onAudioTick — receiver/sr/format/codec/crc/length/type/channels +
    // 8 × reserved zeros.
    putU32(f, 0);                              // receiver
    putU32(f, kAudioRate);                     // sampleRate
    putU32(f, 3);                              // format = float32
    putU32(f, 0);                              // codec = uncompressed
    putU32(f, 0);                              // crc
    putU32(f, quint32(monoSamples * 2));       // length = total floats (stereo)
    putU32(f, 2);                              // type = 2 (TX_AUDIO)
    putU32(f, 2);                              // channels = stereo
    for (int i = 0; i < 8; ++i) putU32(f, 0);  // reserved[8]
    for (int i = 0; i < monoSamples; ++i) {
        putF32(f, mono[i]);                    // L
        putF32(f, mono[i]);                    // R (duplicate)
    }
    return f;
}

// ─────────────────────────────────────────────────────────────────────────
// UI + streaming
// ─────────────────────────────────────────────────────────────────────────

WaterfallIdPanel::WaterfallIdPanel(TciClient* tci, QWidget* parent)
    : QWidget(parent), m_tci(tci)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);
    outer->setSpacing(8);

    // ── Configuration group ─────────────────────────────────────────────
    auto* configBox = new QGroupBox(QStringLiteral("Waterfall ID — paint your callsign into receivers"));
    auto* cfg = new QVBoxLayout(configBox);
    cfg->setSpacing(6);

    auto* row1 = new QHBoxLayout;
    row1->addWidget(new QLabel(QStringLiteral("Text:")));
    m_textEdit = new QLineEdit;
    m_textEdit->setPlaceholderText(QStringLiteral("e.g. G0JKN  (A-Z, 0-9, /, -, space)"));
    m_textEdit->setText(QStringLiteral("G0JKN"));
    m_textEdit->setMaxLength(12);
    row1->addWidget(m_textEdit, 1);

    row1->addWidget(new QLabel(QStringLiteral("Row ms:")));
    m_rowMs = new QSpinBox;
    m_rowMs->setRange(15, 200);
    m_rowMs->setValue(40);  // 40 ms × 8 rows = ~320 ms for the paint
    m_rowMs->setSuffix(QStringLiteral(" ms"));
    m_rowMs->setToolTip(QStringLiteral(
        "Audio duration of each pixel row. Slower → more pixels of vertical "
        "extent in the receiver's waterfall, more legible but longer TX. "
        "40 ms is a reasonable default."));
    row1->addWidget(m_rowMs);
    cfg->addLayout(row1);

    auto* row2 = new QHBoxLayout;
    m_cwIdEnable = new QCheckBox(QStringLiteral("Append CW ID"));
    m_cwIdEnable->setChecked(true);
    m_cwIdEnable->setToolTip(QStringLiteral(
        "Synthesise a 700 Hz Morse identifier after the painted text. "
        "Waterfall paint isn't legally recognised as ID in most "
        "jurisdictions; the CW tail is what makes the transmission "
        "compliant. Skip only for bench testing into a dummy load."));
    row2->addWidget(m_cwIdEnable);

    row2->addWidget(new QLabel(QStringLiteral("WPM:")));
    m_cwWpm = new QSpinBox;
    m_cwWpm->setRange(8, 40);
    m_cwWpm->setValue(20);
    row2->addWidget(m_cwWpm);
    row2->addStretch(1);

    cfg->addLayout(row2);
    outer->addWidget(configBox);

    // ── Action row ──────────────────────────────────────────────────────
    auto* actionRow = new QHBoxLayout;
    m_sendBtn = new QPushButton(QStringLiteral("▶ Send"));
    m_sendBtn->setMinimumWidth(120);
    m_abortBtn = new QPushButton(QStringLiteral("Abort"));
    m_abortBtn->setEnabled(false);
    m_saveWavBtn = new QPushButton(QStringLiteral("Save WAV…"));
    m_saveWavBtn->setToolTip(QStringLiteral(
        "Synthesise the same audio that Send would transmit and save it "
        "to a 48 kHz mono WAV file. Open in Audacity's spectrogram view "
        "to verify the 8-row text structure independent of the radio."));
    actionRow->addWidget(m_sendBtn);
    actionRow->addWidget(m_abortBtn);
    actionRow->addWidget(m_saveWavBtn);
    actionRow->addStretch(1);

    m_status = new QLabel(QStringLiteral("idle — connect first"));
    m_status->setStyleSheet(QStringLiteral("color:#9ab;"));
    actionRow->addWidget(m_status);
    outer->addLayout(actionRow);

    // ── Progress bar ────────────────────────────────────────────────────
    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setFormat(QStringLiteral("ready"));
    outer->addWidget(m_progress);

    // ── Operator notes (always visible — TX is loud and expensive) ──────
    auto* notes = new QLabel(QStringLiteral(
        "Setup expected on the radio side:\n"
        "  • Slice 0 in USB (or USB-D / DIGU)\n"
        "  • Bandwidth ≥ 2.7 kHz (this tab spans 150–2850 Hz audio)\n"
        "  • Output power dialled in — this sends at peak audio amplitude\n"
        "  • Antenna / dummy load connected — Send keys trx:0,true; for the\n"
        "    full duration of the painted text + optional CW ID, then unkeys.\n"
        "  • This tab does NOT change mode or PTT-coordinator state for you.\n"
        "    Abort stops audio mid-stream and sends trx:0,false; immediately."));
    notes->setStyleSheet(QStringLiteral(
        "color:#aab; padding:6px; background:#1a1e26;"
        "border:1px solid #2a3040; border-radius:3px;"));
    notes->setWordWrap(true);
    outer->addWidget(notes);

    // ── Log area ────────────────────────────────────────────────────────
    m_logLabel = new QLabel;
    m_logLabel->setWordWrap(true);
    m_logLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_logLabel->setStyleSheet(QStringLiteral(
        "color:#cde; padding:6px; background:#0d1018; font-family:Consolas,monospace;"
        "border:1px solid #1a2030; border-radius:3px;"));
    m_logLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_logLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    outer->addWidget(m_logLabel, 1);

    // ── Timers ──────────────────────────────────────────────────────────
    m_audio = new QTimer(this);
    m_audio->setInterval(kFrameMs);
    connect(m_audio, &QTimer::timeout, this, &WaterfallIdPanel::onAudioTick);

    connect(m_sendBtn,    &QPushButton::clicked, this, &WaterfallIdPanel::onSendClicked);
    connect(m_abortBtn,   &QPushButton::clicked, this, &WaterfallIdPanel::onAbortClicked);
    connect(m_saveWavBtn, &QPushButton::clicked, this, &WaterfallIdPanel::onSaveWavClicked);

    // Initial state — wait for a connection before the Send button is useful.
    m_sendBtn->setEnabled(false);
    setRunning(false);
}

void WaterfallIdPanel::onConnectionChanged(bool connected)
{
    m_sendBtn->setEnabled(connected && !m_streaming);
    if (!connected && m_streaming) {
        // Connection dropped mid-stream. Cancel cleanly.
        m_streaming = false;
        m_audio->stop();
        m_buffer.clear();
        m_bufferPos = 0;
        setRunning(false);
        m_status->setText(QStringLiteral("aborted — connection dropped"));
        log(QStringLiteral("ABORT — server disconnect mid-transmit"), QStringLiteral("#ff7070"));
    } else if (!connected) {
        m_status->setText(QStringLiteral("idle — connect first"));
    } else if (!m_streaming) {
        m_status->setText(QStringLiteral("ready"));
    }
}

void WaterfallIdPanel::onSendClicked()
{
    if (m_streaming) return;
    if (!m_tci || !m_tci->connected()) {
        log(QStringLiteral("not connected — connect on the Monitor tab first"),
            QStringLiteral("#ff7070"));
        return;
    }
    const QString text = m_textEdit->text().trimmed();
    if (text.isEmpty()) {
        log(QStringLiteral("text is empty — nothing to send"), QStringLiteral("#ff7070"));
        return;
    }

    // Synthesise the full audio buffer up-front. For typical callsigns this
    // is well under 100 KB; no point streaming-synthesising mid-TX.
    m_buffer = buildPaintedAudio(text, m_rowMs->value(), kBandwidthHz, kCenterHz);
    if (m_cwIdEnable->isChecked()) {
        QVector<float> cw = buildCwIdAudio(text, m_cwWpm->value(), kCwToneHz);
        m_buffer.append(cw);
    }
    if (m_buffer.isEmpty()) {
        log(QStringLiteral("synthesised 0 samples — check text for unrenderable chars"),
            QStringLiteral("#ff7070"));
        return;
    }

    const double durSec = double(m_buffer.size()) / kAudioRate;
    log(QStringLiteral("▶ TX \"%1\"  paint=%2 cols × 8 rows × %3 ms"
                       "%4  total=%5 s")
            .arg(text)
            .arg(text.size() * kCharColumns)
            .arg(m_rowMs->value())
            .arg(m_cwIdEnable->isChecked()
                 ? QStringLiteral("  + CW@%1wpm").arg(m_cwWpm->value())
                 : QString())
            .arg(durSec, 0, 'f', 2),
        QStringLiteral("#7fd6ff"));

    m_bufferPos = 0;
    m_streaming = true;
    setRunning(true);

    // Key the radio.  We use the same "bare trx:0,true" idiom CalibrationPanel
    // uses — lets AetherSDR's TciServer own PTT coordination.
    m_tci->send(QStringLiteral("trx:0,true;"));
    m_audio->start();
}

void WaterfallIdPanel::onAbortClicked()
{
    if (!m_streaming) return;
    m_audio->stop();
    if (m_tci) m_tci->send(QStringLiteral("trx:0,false;"));
    m_streaming = false;
    m_buffer.clear();
    m_bufferPos = 0;
    setRunning(false);
    m_status->setText(QStringLiteral("aborted by user"));
    log(QStringLiteral("ABORT — operator hit Abort"), QStringLiteral("#ffb060"));
}

void WaterfallIdPanel::onSaveWavClicked()
{
    const QString text = m_textEdit->text().trimmed();
    if (text.isEmpty()) {
        log(QStringLiteral("text is empty — nothing to save"), QStringLiteral("#ff7070"));
        return;
    }

    // Synthesise the same audio buffer Send would transmit, so the wav
    // is a faithful preview of what receivers will see in their waterfall.
    QVector<float> buf = buildPaintedAudio(text, m_rowMs->value(),
                                           kBandwidthHz, kCenterHz);
    if (m_cwIdEnable->isChecked()) {
        buf.append(buildCwIdAudio(text, m_cwWpm->value(), kCwToneHz));
    }
    if (buf.isEmpty()) {
        log(QStringLiteral("synthesised 0 samples — check text for unrenderable chars"),
            QStringLiteral("#ff7070"));
        return;
    }

    // Suggested filename — share the last-saved directory key with the rest
    // of the toolkit so all "save…" dialogs open in the same place.
    QSettings settings;
    QString lastDir = settings.value(QStringLiteral("log/lastSaveDir")).toString();
    if (lastDir.isEmpty() || !QDir(lastDir).exists()) {
        lastDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    const QString safe = QString(text).replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")),
                                               QStringLiteral("_"));
    const QString suggested = QDir(lastDir).filePath(
        QStringLiteral("waterfall-%1-%2.wav")
            .arg(safe, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Waterfall ID WAV"), suggested,
        tr("WAV files (*.wav)"));
    if (path.isEmpty()) return;   // user cancelled

    // ── Write 48 kHz mono 16-bit PCM WAV ────────────────────────────────
    //
    // Sized so QFile/QDataStream avoid heap thrash. RIFF header is fixed
    // at 44 bytes for "fmt " (16 bytes of canonical PCM fmt) + "data".
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        log(QStringLiteral("could not open %1 for writing").arg(path),
            QStringLiteral("#ff7070"));
        return;
    }

    const quint32 sampleRate   = kAudioRate;
    const quint16 channels     = 1;
    const quint16 bitsPerSamp  = 16;
    const quint16 blockAlign   = channels * bitsPerSamp / 8;
    const quint32 byteRate     = sampleRate * blockAlign;
    const quint32 dataBytes    = quint32(buf.size()) * blockAlign;
    const quint32 fileMinus8   = 36 + dataBytes;  // RIFF size minus the first 8 bytes

    QByteArray hdr;
    hdr.reserve(44);
    auto putBytes = [&](const char* s, int n) { hdr.append(s, n); };
    putBytes("RIFF", 4);
    putU32(hdr, fileMinus8);
    putBytes("WAVE", 4);
    putBytes("fmt ", 4);
    putU32(hdr, 16);                              // fmt chunk size for PCM
    auto putU16 = [&](quint16 v) {
        char d[2] = { char(v & 0xff), char((v >> 8) & 0xff) };
        hdr.append(d, 2);
    };
    putU16(1);                                    // audio format = PCM
    putU16(channels);
    putU32(hdr, sampleRate);
    putU32(hdr, byteRate);
    putU16(blockAlign);
    putU16(bitsPerSamp);
    putBytes("data", 4);
    putU32(hdr, dataBytes);

    if (f.write(hdr) != hdr.size()) {
        log(QStringLiteral("header write failed: %1").arg(f.errorString()),
            QStringLiteral("#ff7070"));
        f.close();
        return;
    }

    // Float [-1, 1] → int16. Buffer peaks at 0.85 by construction so no
    // clipping; still clamp defensively in case of future synthesis changes.
    QByteArray pcm;
    pcm.resize(int(dataBytes));
    qint16* out = reinterpret_cast<qint16*>(pcm.data());
    for (int i = 0; i < buf.size(); ++i) {
        float s = buf[i];
        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        out[i] = qint16(std::lrint(s * 32767.0f));
    }
    if (f.write(pcm) != pcm.size()) {
        log(QStringLiteral("data write failed: %1").arg(f.errorString()),
            QStringLiteral("#ff7070"));
        f.close();
        return;
    }
    f.close();

    // Remember the directory for next time (shared with other save handlers).
    settings.setValue(QStringLiteral("log/lastSaveDir"),
                      QFileInfo(path).absolutePath());

    const double durSec = double(buf.size()) / kAudioRate;
    log(QStringLiteral("✓ saved %1 (%2 samples, %3 s)")
            .arg(QFileInfo(path).fileName())
            .arg(buf.size())
            .arg(durSec, 0, 'f', 2),
        QStringLiteral("#80e080"));
}

void WaterfallIdPanel::onAudioTick()
{
    if (!m_streaming || !m_tci) return;

    // Shipped 50 ms at a time.  When we run off the end of the buffer,
    // send whatever's left (may be < 50 ms — we pad with silence so the
    // frame is consistent length on the wire) and then unkey.
    int monoSamples = std::min<int>(kFrameSamples,
                                    static_cast<int>(m_buffer.size() - m_bufferPos));
    if (monoSamples <= 0) {
        // Done: unkey, leave a small trailing silence so the radio's
        // ALC and PA see a clean shutdown rather than an abrupt cliff.
        m_audio->stop();
        m_tci->send(QStringLiteral("trx:0,false;"));
        m_streaming = false;
        setRunning(false);
        m_status->setText(QStringLiteral("done"));
        log(QStringLiteral("✓ TX complete — unkeyed"), QStringLiteral("#80e080"));
        return;
    }

    // Pad-to-frame so every wire frame is identical size — keeps AetherSDR's
    // packetiser happy.  Padding is silence; ramps in the synthesised
    // audio already prevent click at the join.
    QVector<float> chunk(kFrameSamples, 0.0f);
    std::memcpy(chunk.data(), m_buffer.data() + m_bufferPos,
                monoSamples * sizeof(float));
    QByteArray frame = packTxFrame(chunk.data(), kFrameSamples);
    m_tci->sendBinary(frame);

    m_bufferPos += monoSamples;

    const int pct = int(100.0 * m_bufferPos / m_buffer.size());
    m_progress->setValue(pct);
    m_progress->setFormat(QStringLiteral("transmitting — %p%"));
}

void WaterfallIdPanel::setRunning(bool running)
{
    m_sendBtn->setEnabled(!running && m_tci && m_tci->connected());
    m_abortBtn->setEnabled(running);
    m_textEdit->setEnabled(!running);
    m_rowMs->setEnabled(!running);
    m_cwIdEnable->setEnabled(!running);
    m_cwWpm->setEnabled(!running);
    if (running) {
        m_status->setText(QStringLiteral("transmitting…"));
        m_progress->setValue(0);
        m_progress->setFormat(QStringLiteral("0%"));
    } else {
        m_progress->setFormat(QStringLiteral("ready"));
        m_progress->setValue(0);
    }
}

void WaterfallIdPanel::log(const QString& msg, const QString& colorHex)
{
    // Tiny single-line log: prepend timestamp, keep last ~40 lines.
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    QString styled;
    if (colorHex.isEmpty())
        styled = QStringLiteral("[%1] %2").arg(ts, msg.toHtmlEscaped());
    else
        styled = QStringLiteral("<span style='color:%1;'>[%2] %3</span>")
                     .arg(colorHex, ts, msg.toHtmlEscaped());

    QString existing = m_logLabel->text();
    QStringList lines = existing.split(QStringLiteral("<br>"), Qt::SkipEmptyParts);
    lines.append(styled);
    while (lines.size() > 40) lines.removeFirst();
    m_logLabel->setText(lines.join(QStringLiteral("<br>")));
}

} // namespace TciMon
