#include "MainWindow.h"

#include "CalibrationPanel.h"
#include "CommandDescriptionDialog.h"
#include "CommandsReferenceDialog.h"
#include "ComparePanel.h"
#include "ConsolePanel.h"
#include "DiscoveryDialog.h"
#include "InspectorPanel.h"
#include "SignalPanel.h"
#include "ReplayPanel.h"
#include "SwrPlot.h"
#include "TciClient.h"
#include "TciCommands.h"

#include <QAction>
#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTextStream>

namespace TciMon {

namespace {

constexpr const char* kStatusOnline =
    "QLabel { color: #4cff7c; font-size: 14px; }";
constexpr const char* kStatusOffline =
    "QLabel { color: #6b8099; font-size: 14px; }";

constexpr const char* kBigValueStyle =
    "QLabel { color: #ffd400; font-size: 18px; font-weight: bold; "
    "font-family: Consolas, 'Cascadia Mono', monospace; }";

constexpr const char* kCaptionStyle =
    "QLabel { color: #6b8099; font-size: 9px; font-weight: bold; "
    "letter-spacing: 0.08em; }";

// Color used in the raw log for each message family.
constexpr const char* kColorVfo     = "#00d8ef";   // cyan
constexpr const char* kColorMode    = "#00d8ef";
constexpr const char* kColorSpot    = "#4cff7c";   // green
constexpr const char* kColorSpotDel = "#ffaa00";   // amber
constexpr const char* kColorMeta    = "#6b8099";   // muted (start, ready, protocol)
constexpr const char* kColorErr     = "#ff5050";
constexpr const char* kColorOther   = "#dde6f0";   // default

// Format a frequency string sourced from a TCI Hz integer.
QString hzToMhz(const QString& hzStr)
{
    bool ok = false;
    qint64 hz = hzStr.trimmed().toLongLong(&ok);
    if (!ok) return hzStr;
    return QString::number(hz / 1.0e6, 'f', 6);
}

// Decode an RGB color string (hex or decimal) to a CSS hex string for a
// swatch.  Returns empty string if it can't be parsed.
QString tciColorToCss(const QString& raw)
{
    const QString s = raw.trimmed();
    if (s.isEmpty()) return {};
    bool ok = false;
    quint32 v = s.toUInt(&ok, /*base*/ 0);   // auto-detect 0x prefix
    if (!ok) v = s.toUInt(&ok, 10);
    if (!ok) return {};
    return QString("#%1").arg(v & 0xFFFFFF, 6, 16, QChar('0')).toUpper();
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_tci(new TciClient(this))
{
    setWindowTitle("TCI Monitor");
    resize(1200, 720);

    buildUI();
    restoreSuppressions();   // re-apply last run's suppression filters
    restoreSweeps();         // re-load persisted per-band SWR sweeps

    connect(m_tci, &TciClient::connectionChanged, this, &MainWindow::onConnectionChanged);
    connect(m_tci, &TciClient::rawMessageReceived, this, &MainWindow::onRawMessage);
    connect(m_tci, &TciClient::errorTextReceived, this, &MainWindow::onErrorText);

    refreshStatus();
}

MainWindow::~MainWindow() = default;

// ── UI ────────────────────────────────────────────────────────────────────

void MainWindow::buildUI()
{
    // Menu bar — only entry for now is the TCI command reference.
    {
        auto* help = menuBar()->addMenu("&Help");
        auto* ref  = help->addAction("TCI &Commands…");
        ref->setShortcut(QKeySequence::HelpContents);   // F1 on most platforms
        connect(ref, &QAction::triggered,
                this, &MainWindow::onShowCommandsReference);
    }

    auto* central = new QWidget;
    setCentralWidget(central);
    auto* main = new QVBoxLayout(central);
    main->setContentsMargins(8, 6, 8, 6);
    main->setSpacing(6);

    // ── Connect bar ────────────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);

        auto* lh = new QLabel("HOST"); lh->setStyleSheet(kCaptionStyle);
        m_hostEdit = new QLineEdit("127.0.0.1");
        m_hostEdit->setMinimumWidth(160);

        auto* lp = new QLabel("PORT"); lp->setStyleSheet(kCaptionStyle);
        m_portSpin = new QSpinBox;
        m_portSpin->setRange(1, 65535);
        // AetherSDR's TCI default is 40001.  ExpertSDR2 / SunSDR use 50001.
        // Since AetherSDR is the most common target here, default to 40001
        // and let the user override in the spinbox.
        m_portSpin->setValue(40001);

        m_connectBtn    = new QPushButton("Connect");
        m_disconnectBtn = new QPushButton("Disconnect");
        m_disconnectBtn->setEnabled(false);
        m_discoverBtn   = new QPushButton(QStringLiteral("Discover…"));
        m_discoverBtn->setToolTip(tr("Browse the LAN for TCI peripherals "
                                     "advertising under _tci._tcp.local"));
        connect(m_connectBtn,    &QPushButton::clicked, this, &MainWindow::onConnectClicked);
        connect(m_disconnectBtn, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
        connect(m_discoverBtn,   &QPushButton::clicked, this, &MainWindow::onDiscoverClicked);

        m_statusDot  = new QLabel("●");
        m_statusDot->setStyleSheet(kStatusOffline);
        m_statusText = new QLabel("Offline");
        m_statusText->setStyleSheet(kCaptionStyle);
        m_statusText->setMinimumWidth(80);

        row->addWidget(lh); row->addWidget(m_hostEdit);
        row->addSpacing(8);
        row->addWidget(lp); row->addWidget(m_portSpin);
        row->addSpacing(8);
        row->addWidget(m_connectBtn);
        row->addWidget(m_disconnectBtn);
        row->addSpacing(8);
        row->addWidget(m_discoverBtn);
        row->addStretch();
        row->addWidget(m_statusDot);
        row->addWidget(m_statusText);
        main->addLayout(row);
    }

    // ── Splitter: parsed | raw ─────────────────────────────────────────
    auto* split = new QSplitter(Qt::Horizontal);

    // Left — parsed view
    {
        auto* left = new QWidget;
        auto* lv = new QVBoxLayout(left);
        lv->setContentsMargins(4, 4, 4, 4);
        lv->setSpacing(4);

        // Current VFO + Mode
        auto* hdr = new QHBoxLayout;
        auto* vfoBlock = new QVBoxLayout; vfoBlock->setSpacing(0);
        auto* lvf = new QLabel("VFO 0 / RX 0"); lvf->setStyleSheet(kCaptionStyle);
        m_curVfo = new QLabel("—");
        m_curVfo->setStyleSheet(kBigValueStyle);
        m_curVfo->setMinimumWidth(160);
        vfoBlock->addWidget(lvf); vfoBlock->addWidget(m_curVfo);

        auto* modeBlock = new QVBoxLayout; modeBlock->setSpacing(0);
        auto* lmd = new QLabel("MODE"); lmd->setStyleSheet(kCaptionStyle);
        m_curMode = new QLabel("—");
        m_curMode->setStyleSheet(kBigValueStyle);
        m_curMode->setMinimumWidth(80);
        modeBlock->addWidget(lmd); modeBlock->addWidget(m_curMode);

        auto* sweepBlock = new QVBoxLayout; sweepBlock->setSpacing(0);
        auto* lsw = new QLabel("SWR SWEEP"); lsw->setStyleSheet(kCaptionStyle);
        m_sweepLabel = new QLabel("idle");
        m_sweepLabel->setStyleSheet(kCaptionStyle);
        m_sweepLabel->setMinimumWidth(360);
        sweepBlock->addWidget(lsw); sweepBlock->addWidget(m_sweepLabel);

        hdr->addLayout(vfoBlock);
        hdr->addLayout(modeBlock);
        hdr->addSpacing(16);
        hdr->addLayout(sweepBlock);
        hdr->addStretch();
        lv->addLayout(hdr);

        m_leftTabs = new QTabWidget;

        // ── Tab 1: Spots ───────────────────────────────────────────────
        {
            auto* spotsTab = new QWidget;
            auto* sv = new QVBoxLayout(spotsTab);
            sv->setContentsMargins(2, 2, 2, 2);
            sv->setSpacing(4);

            m_spotTable = new QTableWidget;
            m_spotTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
            m_spotTable->setSelectionBehavior(QAbstractItemView::SelectRows);
            m_spotTable->setSelectionMode(QAbstractItemView::SingleSelection);
            m_spotTable->setAlternatingRowColors(true);
            m_spotTable->verticalHeader()->setVisible(false);
            m_spotTable->setColumnCount(6);
            m_spotTable->setHorizontalHeaderLabels(
                {"Time", "Call", "Mode", "Freq (MHz)", "Color", "Description"});
            m_spotTable->horizontalHeader()->setStretchLastSection(true);
            sv->addWidget(m_spotTable, 1);

            m_leftTabs->addTab(spotsTab, "Spots");
        }

        // ── Tab 2: SWR scan ────────────────────────────────────────────
        {
            auto* swrTab = new QWidget;
            auto* wv = new QVBoxLayout(swrTab);
            wv->setContentsMargins(2, 2, 2, 2);
            wv->setSpacing(4);

            auto* bandRowW = new QWidget;
            m_bandBtnLayout = new QHBoxLayout(bandRowW);
            m_bandBtnLayout->setContentsMargins(0, 0, 0, 0);
            m_bandBtnLayout->setSpacing(4);
            auto* bl = new QLabel("BANDS");
            bl->setStyleSheet(kCaptionStyle);
            m_bandBtnLayout->addWidget(bl);
            m_bandBtnLayout->addStretch();
            wv->addWidget(bandRowW);

            m_swrPlot = new SwrPlot;
            wv->addWidget(m_swrPlot, 1);

            m_leftTabs->addTab(swrTab, "SWR scan");
        }

        lv->addWidget(m_leftTabs, 1);
        split->addWidget(left);
    }

    // Right — raw log
    {
        auto* right = new QWidget;
        auto* rv = new QVBoxLayout(right);
        rv->setContentsMargins(4, 4, 4, 4);
        rv->setSpacing(4);

        // First control row: title + filter + autoscroll + pause
        auto* topRow = new QHBoxLayout;
        auto* lr = new QLabel("RAW MESSAGES");
        lr->setStyleSheet(kCaptionStyle);
        m_filterEdit = new QLineEdit;
        m_filterEdit->setPlaceholderText("Filter (substring)…");
        m_filterEdit->setMaximumWidth(220);
        connect(m_filterEdit, &QLineEdit::textChanged,
                this, &MainWindow::onFilterChanged);
        m_autoscrollCheck = new QCheckBox("Autoscroll");
        m_autoscrollCheck->setChecked(true);
        m_pauseBtn = new QPushButton("Pause");
        m_pauseBtn->setCheckable(true);
        m_pauseBtn->setStyleSheet(
            "QPushButton:checked { background: #003040; "
            "border: 1px solid #ffaa00; color: #ffaa00; }");
        connect(m_pauseBtn, &QPushButton::toggled,
                this, &MainWindow::onPauseToggled);
        topRow->addWidget(lr);
        topRow->addStretch();
        topRow->addWidget(m_filterEdit);
        topRow->addWidget(m_autoscrollCheck);
        topRow->addWidget(m_pauseBtn);
        rv->addLayout(topRow);

        // Second control row: suppression status
        auto* supRow = new QHBoxLayout;
        m_suppressLabel = new QLabel("0 suppressions");
        m_suppressLabel->setStyleSheet(kCaptionStyle);
        m_clearSuppressBtn = new QPushButton("Show / clear…");
        m_clearSuppressBtn->setEnabled(false);
        connect(m_clearSuppressBtn, &QPushButton::clicked,
                this, &MainWindow::onShowSuppressions);
        supRow->addStretch();
        supRow->addWidget(m_suppressLabel);
        supRow->addWidget(m_clearSuppressBtn);
        rv->addLayout(supRow);

        m_logTable = new QTableWidget;
        m_logTable->setColumnCount(3);
        m_logTable->setHorizontalHeaderLabels({"Time", "Cmd", "Message"});
        m_logTable->verticalHeader()->setVisible(false);
        m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_logTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_logTable->horizontalHeader()->setStretchLastSection(true);
        m_logTable->horizontalHeader()->setMinimumSectionSize(60);
        m_logTable->setColumnWidth(0, 100);
        m_logTable->setColumnWidth(1, 110);
        m_logTable->setShowGrid(false);
        m_logTable->setAlternatingRowColors(true);
        m_logTable->setStyleSheet(
            "QTableWidget { background-color: #050a14; "
            "alternate-background-color: #0a1320; "
            "color: #dde6f0; border: 1px solid #1c2a40; "
            "font-family: Consolas, 'Cascadia Mono', monospace; "
            "font-size: 11px; gridline-color: transparent; }");
        m_logTable->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_logTable, &QTableWidget::customContextMenuRequested,
                this, &MainWindow::onLogContextMenu);
        rv->addWidget(m_logTable, 1);

        split->addWidget(right);
    }

    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);

    // The live monitor (spots/SWR + raw log + its action row) becomes the
    // first tab.  The v0.3 tools are sibling tabs that share the connect
    // bar above — Inspect/Console/Replay are fed off the same m_tci stream;
    // Compare runs its own independent observer connections.
    auto* monitorTab = new QWidget;
    auto* mlay = new QVBoxLayout(monitorTab);
    mlay->setContentsMargins(0, 0, 0, 0);
    mlay->setSpacing(6);
    mlay->addWidget(split, 1);

    // ── Bottom action row (inside the Monitor tab) ─────────────────────
    {
        auto* row = new QHBoxLayout;
        m_saveBtn  = new QPushButton("Save log…");
        m_saveSweepBtn = new QPushButton("Save SWR sweep…");
        m_saveSweepBtn->setEnabled(false);
        m_clearBtn = new QPushButton("Clear log");
        connect(m_saveBtn,  &QPushButton::clicked, this, &MainWindow::onSaveLog);
        connect(m_saveSweepBtn, &QPushButton::clicked, this, &MainWindow::onSaveSweep);
        connect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::onClearLog);
        row->addStretch();
        row->addWidget(m_saveBtn);
        row->addWidget(m_saveSweepBtn);
        row->addWidget(m_clearBtn);
        mlay->addLayout(row);
    }

    // ── Top-level toolkit tabs ─────────────────────────────────────────
    m_topTabs = new QTabWidget;
    m_topTabs->addTab(monitorTab, "Monitor");

    m_inspector = new InspectorPanel;
    m_topTabs->addTab(m_inspector, "Inspect");

    m_console = new ConsolePanel(m_tci);
    m_topTabs->addTab(m_console, "Console");

    m_compare = new ComparePanel;
    m_topTabs->addTab(m_compare, "Compare");

    m_replay = new ReplayPanel;
    m_topTabs->addTab(m_replay, "Replay");

    m_cal = new CalibrationPanel(m_tci);
    m_topTabs->addTab(m_cal, "TX Cal");

    m_signal = new SignalPanel(m_tci);
    m_topTabs->addTab(m_signal, "Signal");
    // SignalPanel needs the binary stream + connection state directly —
    // unlike the text-only tabs (which we fan out from onRawMessage),
    // binary frames bypass the parsing pipeline entirely.
    connect(m_tci, &TciClient::binaryFrameReceived,
            m_signal, &SignalPanel::ingestFrame);
    connect(m_tci, &TciClient::connectionChanged,
            m_signal, &SignalPanel::onConnectionChanged);

    main->addWidget(m_topTabs, 1);

    // ── Status bar counters ────────────────────────────────────────────
    m_sbMsgCount  = new QLabel("0 msgs");
    m_sbSpotCount = new QLabel("0 spots");
    statusBar()->addPermanentWidget(m_sbMsgCount);
    statusBar()->addPermanentWidget(m_sbSpotCount);
}

// ── Connection ───────────────────────────────────────────────────────────

void MainWindow::onConnectClicked()
{
    const QString host  = m_hostEdit->text().trimmed();
    const quint16 port  = static_cast<quint16>(m_portSpin->value());
    appendLog(QString("[connect] ws://%1:%2").arg(host).arg(port), kColorMeta);
    m_tci->connectToServer(host, port);
}

void MainWindow::onDisconnectClicked()
{
    appendLog("[disconnect] user requested", kColorMeta);
    m_tci->disconnectFromServer();
}

void MainWindow::onConnectionChanged(bool connected)
{
    m_connectBtn->setEnabled(!connected);
    m_disconnectBtn->setEnabled(connected);
    m_statusDot->setStyleSheet(connected ? kStatusOnline : kStatusOffline);
    m_statusText->setText(connected ? "Connected" : "Offline");
    appendLog(connected ? "[connected]" : "[disconnected]", kColorMeta);
}

void MainWindow::onErrorText(const QString& text)
{
    appendLog(QString("[error] %1").arg(text), kColorErr);
}

void MainWindow::refreshStatus()
{
    m_sbMsgCount->setText(QString("%1 msgs").arg(m_msgCount));
    m_sbSpotCount->setText(QString("%1 spots").arg(m_spotCount));
}

// ── Raw message handling + parsing ───────────────────────────────────────

void MainWindow::onRawMessage(const QString& line)
{
    ++m_msgCount;

    // Pick a color based on the leading command name.
    const int colon = line.indexOf(':');
    QString cmd = colon < 0 ? line : line.left(colon);
    cmd = cmd.trimmed().toLower();

    QString color = kColorOther;
    if      (cmd == "vfo")           color = kColorVfo;
    else if (cmd == "mode" || cmd == "modulation") color = kColorMode;
    else if (cmd == "spot")          color = kColorSpot;
    else if (cmd == "spot_delete" || cmd == "spot_clear") color = kColorSpotDel;
    else if (cmd == "protocol" || cmd == "ready" || cmd == "start")
                                     color = kColorMeta;

    appendLog(line, color);
    parseLine(line);

    // Fan the same stream out to the toolkit tabs.  Compare has its own
    // independent observer connections, so it is not fed from here.
    if (m_inspector) m_inspector->ingest(line);
    if (m_console)   m_console->noteIncoming(line);
    if (m_replay)    m_replay->noteIncoming(line);
    if (m_cal)       m_cal->noteIncoming(line);

    refreshStatus();
}

void MainWindow::parseLine(const QString& line)
{
    const int colon = line.indexOf(':');
    if (colon < 0) return;
    const QString cmd  = line.left(colon).trimmed().toLower();
    const QString rest = line.mid(colon + 1);
    const QStringList args = rest.split(',', Qt::KeepEmptyParts);

    if      (cmd == "vfo")          handleVfo(args);
    else if (cmd == "mode" || cmd == "modulation") handleMode(args);
    else if (cmd == "spot")         handleSpot(args);
    else if (cmd == "spot_delete")  handleSpotDelete(args);
    else if (cmd == "spot_clear")   handleSpotClear();
    else if (cmd == "trx")          handleTrx(args);
    else if (cmd == "tx_sensors")   handleTxSensors(args);
}

void MainWindow::handleVfo(const QStringList& args)
{
    // vfo:rx,vfo,freqHz — report only RX 0 / VFO 0 in the parsed pane.
    if (args.size() < 3) return;
    if (args[0].trimmed() != "0" || args[1].trimmed() != "0") return;
    m_curVfo->setText(hzToMhz(args[2]) + " MHz");
    // Track the live freq so a sweep's tx_sensors SWR can be paired with it.
    bool ok = false;
    const qint64 hz = args[2].trimmed().toLongLong(&ok);
    if (ok && hz > 0) m_sweepCurFreqHz = hz;
}

// ── SWR sweep capture ──────────────────────────────────────────────────
//
// AE's antenna SWR sweep keys TX (trx:0,true), steps vfo:0,0,<hz>; across
// the band, and emits tx_sensors:0,mic,fwd,peak,swr; while transmitting.
// We pair the latest vfo freq with each tx_sensors SWR, keeping the
// minimum SWR seen per frequency, until trx:0,false ends the sweep.

void MainWindow::handleTrx(const QStringList& args)
{
    if (args.size() < 2 || args[0].trimmed() != "0") return;
    const QString state = args[1].trimmed().toLower();
    if (state == "true") {
        m_sweepActive = true;
        m_sweepSwr.clear();
        m_sweepCurSwr = 0.0;
        m_sweepStartStamp =
            QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    } else if (state == "false") {
        const bool had = m_sweepActive && !m_sweepSwr.isEmpty();
        m_sweepActive = false;
        if (had) finalizeSweep();
    }
    refreshSweepUi();
}

void MainWindow::handleTxSensors(const QStringList& args)
{
    // tx_sensors:trx,mic_dbm,fwd_watts,peak_watts,swr
    if (args.size() < 5 || args[0].trimmed() != "0") return;
    bool ok = false;
    const double swr = args[4].trimmed().toDouble(&ok);
    if (!ok) return;
    m_sweepCurSwr = swr;
    if (m_sweepActive && m_sweepCurFreqHz > 0) {
        auto it = m_sweepSwr.find(m_sweepCurFreqHz);
        if (it == m_sweepSwr.end() || swr < it.value())
            m_sweepSwr[m_sweepCurFreqHz] = swr;   // keep the best (min) SWR
        refreshSweepUi();
    }
}

void MainWindow::refreshSweepUi()
{
    const QMap<qint64,double>& src =
        m_sweepActive ? m_sweepSwr : m_lastSweep;

    if (src.isEmpty()) {
        m_sweepLabel->setText(m_sweepActive ? "sweeping…" : "idle");
        return;
    }
    // Find min-SWR point for the summary.
    qint64 minF = 0; double minS = 1e9;
    for (auto it = src.begin(); it != src.end(); ++it)
        if (it.value() < minS) { minS = it.value(); minF = it.key(); }

    const QString minMhz = QString::number(minF / 1e6, 'f', 3);
    if (m_sweepActive) {
        m_sweepLabel->setText(
            QString("● %1 MHz  SWR %2  |  min %3 @ %4 MHz  |  %5 pts")
                .arg(m_sweepCurFreqHz / 1e6, 0, 'f', 3)
                .arg(m_sweepCurSwr, 0, 'f', 2)
                .arg(minS, 0, 'f', 2).arg(minMhz)
                .arg(src.size()));
    } else {
        m_sweepLabel->setText(
            QString("done — %1 pts, min %2 @ %3 MHz  (Save SWR sweep…)")
                .arg(src.size()).arg(minS, 0, 'f', 2).arg(minMhz));
    }
}

// HF + 6 m band map. Returns "20m" etc., or a generic "%.3fMHz" bucket
// for out-of-band sweeps so nothing is ever silently dropped.
QString MainWindow::bandForHz(qint64 hz)
{
    const double m = hz / 1e6;
    struct B { const char* n; double lo, hi; };
    static const B bands[] = {
        {"160m", 1.8,  2.0},  {"80m", 3.5,  4.0},  {"60m",  5.25, 5.45},
        {"40m",  7.0,  7.3},  {"30m", 10.1, 10.15},{"20m",  13.9, 14.4},
        {"17m",  18.0, 18.2}, {"15m", 20.9, 21.5}, {"12m",  24.8, 25.0},
        {"10m",  28.0, 29.7}, {"6m",  50.0, 54.0},
    };
    for (const auto& b : bands)
        if (m >= b.lo && m <= b.hi) return QString(b.n);
    return QString("%1MHz").arg(m, 0, 'f', 3);
}

namespace {
// Stable per-slot colour palette for overlaid band curves.
QColor bandColor(int idx)
{
    static const char* pal[] = {
        "#00d8ef", "#ffaa00", "#4cff7c", "#ff5cff",
        "#ffe14c", "#ff5050", "#7c9cff", "#9cff00",
    };
    return QColor(pal[idx % 8]);
}
const QStringList kBandOrder = {
    "160m","80m","60m","40m","30m","20m","17m","15m","12m","10m","6m"};
int bandRank(const QString& b) {
    int i = kBandOrder.indexOf(b);
    return i < 0 ? 100 : i;             // unknown/out-of-band sort last
}
} // namespace

void MainWindow::finalizeSweep()
{
    if (m_sweepSwr.isEmpty()) return;
    // Band from the median sample (robust against edge stragglers).
    QList<qint64> keys = m_sweepSwr.keys();
    const qint64 medianHz = keys.at(keys.size() / 2);
    const QString band = bandForHz(medianHz);

    m_sweepsByBand[band]     = m_sweepSwr;
    m_sweepStampByBand[band] = m_sweepStartStamp;
    m_lastSweep              = m_sweepSwr;
    m_lastSweepBand          = band;
    m_saveSweepBtn->setEnabled(true);

    persistSweeps();
    rebuildBandButtons();
    if (auto* b = m_bandButtons.value(band)) b->setChecked(true);
    rebuildPlot();
    if (m_leftTabs) m_leftTabs->setCurrentIndex(1);   // jump to SWR tab
}

void MainWindow::rebuildBandButtons()
{
    // Order present bands by the standard band sequence.
    QStringList present = m_sweepsByBand.keys();
    std::sort(present.begin(), present.end(),
              [](const QString& a, const QString& b) {
                  return bandRank(a) < bandRank(b);
              });
    for (const QString& band : present) {
        if (m_bandButtons.contains(band)) continue;
        auto* btn = new QPushButton(band);
        btn->setCheckable(true);
        btn->setMaximumWidth(64);
        connect(btn, &QPushButton::toggled, this, &MainWindow::rebuildPlot);
        // Insert before the trailing stretch (last layout item).
        m_bandBtnLayout->insertWidget(m_bandBtnLayout->count() - 1, btn);
        m_bandButtons.insert(band, btn);
    }
}

void MainWindow::rebuildPlot()
{
    if (!m_swrPlot) return;
    QStringList shown = m_bandButtons.keys();
    std::sort(shown.begin(), shown.end(),
              [](const QString& a, const QString& b) {
                  return bandRank(a) < bandRank(b);
              });
    QVector<SwrPlot::Curve> curves;
    int idx = 0;
    for (const QString& band : shown) {
        auto* btn = m_bandButtons.value(band);
        if (!btn || !btn->isChecked()) { ++idx; continue; }
        if (!m_sweepsByBand.contains(band)) { ++idx; continue; }
        SwrPlot::Curve c;
        c.band   = band;
        c.color  = bandColor(idx);
        c.points = m_sweepsByBand.value(band);
        curves.push_back(c);
        ++idx;
    }
    m_swrPlot->setCurves(curves);
}

void MainWindow::persistSweeps()
{
    QSettings s;
    s.beginGroup("swrsweeps");
    s.remove("");                                  // clear stale entries
    s.setValue("bands", QStringList(m_sweepsByBand.keys()));
    s.setValue("last", m_lastSweepBand);
    for (auto it = m_sweepsByBand.begin(); it != m_sweepsByBand.end(); ++it) {
        QStringList pts;
        for (auto p = it.value().begin(); p != it.value().end(); ++p)
            pts << QString("%1:%2").arg(p.key())
                                   .arg(p.value(), 0, 'f', 2);
        s.beginGroup(it.key());
        s.setValue("points", pts.join('|'));
        s.setValue("stamp",  m_sweepStampByBand.value(it.key()));
        s.endGroup();
    }
    s.endGroup();
}

void MainWindow::restoreSweeps()
{
    QSettings s;
    s.beginGroup("swrsweeps");
    const QStringList bands = s.value("bands").toStringList();
    m_lastSweepBand = s.value("last").toString();
    for (const QString& band : bands) {
        s.beginGroup(band);
        const QString blob  = s.value("points").toString();
        const QString stamp = s.value("stamp").toString();
        s.endGroup();
        QMap<qint64,double> pts;
        const QStringList items = blob.split('|', Qt::SkipEmptyParts);
        for (const QString& it : items) {
            const int c = it.indexOf(':');
            if (c < 0) continue;
            bool okF = false, okS = false;
            const qint64 hz  = it.left(c).toLongLong(&okF);
            const double swr = it.mid(c + 1).toDouble(&okS);
            if (okF && okS) pts[hz] = swr;
        }
        if (!pts.isEmpty()) {
            m_sweepsByBand[band]     = pts;
            m_sweepStampByBand[band] = stamp;
        }
    }
    s.endGroup();

    if (!m_sweepsByBand.isEmpty()) {
        rebuildBandButtons();
        for (auto* b : m_bandButtons) b->setChecked(true);  // show all history
        if (m_sweepsByBand.contains(m_lastSweepBand))
            m_lastSweep = m_sweepsByBand.value(m_lastSweepBand);
        m_saveSweepBtn->setEnabled(true);
        rebuildPlot();
    }
}

void MainWindow::handleMode(const QStringList& args)
{
    if (args.size() < 2) return;
    if (args[0].trimmed() != "0") return;
    m_curMode->setText(args[1].trimmed().toUpper());
}

void MainWindow::handleSpot(const QStringList& args)
{
    // spot:call,mode,freqHz,color,description (TCI 1.10 form).  Some
    // servers omit one or more trailing fields — handle gracefully.
    QString call = args.value(0).trimmed();
    QString mode = args.value(1).trimmed();
    QString hz   = args.value(2).trimmed();
    QString col  = args.value(3).trimmed();
    // description may itself contain commas — rejoin everything from
    // index 4 onward.
    QString desc;
    if (args.size() > 4) {
        QStringList tail = args.mid(4);
        desc = tail.join(',').trimmed();
    }
    if (call.isEmpty()) return;

    ++m_spotCount;
    const int row = m_spotTable->rowCount();
    m_spotTable->insertRow(row);
    auto* tItem = new QTableWidgetItem(QDateTime::currentDateTime().toString("HH:mm:ss"));
    m_spotTable->setItem(row, 0, tItem);
    m_spotTable->setItem(row, 1, new QTableWidgetItem(call));
    m_spotTable->setItem(row, 2, new QTableWidgetItem(mode));
    m_spotTable->setItem(row, 3, new QTableWidgetItem(hzToMhz(hz)));

    auto* colItem = new QTableWidgetItem(col);
    const QString css = tciColorToCss(col);
    if (!css.isEmpty()) {
        QColor c{css};
        colItem->setBackground(c);
        // Pick contrasting foreground so the value stays legible.
        const int luma = (299 * c.red() + 587 * c.green() + 114 * c.blue()) / 1000;
        colItem->setForeground(QColor(luma > 128 ? "#000000" : "#FFFFFF"));
    }
    m_spotTable->setItem(row, 4, colItem);
    m_spotTable->setItem(row, 5, new QTableWidgetItem(desc));

    m_spotTable->resizeColumnsToContents();
    m_spotTable->scrollToBottom();
}

void MainWindow::handleSpotDelete(const QStringList& args)
{
    // spot_delete:call — remove rows matching the call.  Keeps the
    // table aligned with what's currently shown on the radio.
    if (args.isEmpty()) return;
    const QString call = args[0].trimmed();
    for (int r = m_spotTable->rowCount() - 1; r >= 0; --r) {
        auto* it = m_spotTable->item(r, 1);
        if (it && it->text() == call) m_spotTable->removeRow(r);
    }
}

void MainWindow::handleSpotClear()
{
    m_spotTable->setRowCount(0);
}

// ── Log view ──────────────────────────────────────────────────────────────

void MainWindow::appendLog(const QString& line, const QString& colorHex)
{
    // Hard drops first — these don't even bump the visible row.
    if (m_paused) return;

    const int colon = line.indexOf(':');
    const QString cmd = (colon < 0 ? line : line.left(colon)).trimmed().toLower();
    if (m_suppressed.contains(cmd)) return;

    const QString filter = m_filterEdit->text().trimmed();
    if (!filter.isEmpty() && !line.contains(filter, Qt::CaseInsensitive)) {
        return;
    }

    // Bound memory for long sessions — keep a rolling window of 50k rows.
    constexpr int kMaxRows = 50000;
    if (m_logTable->rowCount() >= kMaxRows) {
        m_logTable->removeRow(0);
    }

    const int row = m_logTable->rowCount();
    m_logTable->insertRow(row);

    const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    auto* tsItem  = new QTableWidgetItem(ts);
    auto* cmdItem = new QTableWidgetItem(cmd);
    auto* msgItem = new QTableWidgetItem(line);
    const QColor color{colorHex};
    const QColor tsColor{"#6b8099"};
    tsItem->setForeground(tsColor);
    cmdItem->setForeground(color);
    msgItem->setForeground(color);
    m_logTable->setItem(row, 0, tsItem);
    m_logTable->setItem(row, 1, cmdItem);
    m_logTable->setItem(row, 2, msgItem);

    if (m_autoscrollCheck->isChecked()) {
        m_logTable->scrollToBottom();
    }
}

void MainWindow::onClearLog()
{
    m_logTable->setRowCount(0);
    m_msgCount  = 0;
    m_spotCount = 0;
    refreshStatus();
}

void MainWindow::onSaveLog()
{
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString defaultName = QString("tci-monitor-%1.log").arg(stamp);
    // Open the dialog in the directory used for the last successful save,
    // falling back to Documents the first time (key shared via the app's
    // G0JKN / "TCI Monitor" QSettings, same as DiscoveryDialog).
    QSettings settings;
    const QString fallbackDir =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString dir = settings.value("log/lastSaveDir", fallbackDir).toString();
    const QString path = QFileDialog::getSaveFileName(this, "Save log",
                            dir + "/" + defaultName,
                            "Log files (*.log *.txt);;All files (*)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::critical(this, "Save failed", f.errorString());
        return;
    }
    QTextStream out(&f);
    for (int r = 0; r < m_logTable->rowCount(); ++r) {
        out << m_logTable->item(r, 0)->text() << "  "
            << m_logTable->item(r, 2)->text() << "\n";
    }
    f.close();

    // Remember where we saved so the next Save log… opens here, not Documents.
    settings.setValue("log/lastSaveDir", QFileInfo(path).absolutePath());
}

void MainWindow::onSaveSweep()
{
    if (m_sweepsByBand.isEmpty()) {
        QMessageBox::information(this, "No sweep",
            "No completed SWR sweep captured yet. Run an antenna SWR sweep "
            "in AetherSDR with TCI Monitor connected, then try again.");
        return;
    }
    // Export the band(s) currently ticked on the SWR tab.
    QStringList bands;
    for (auto it = m_bandButtons.begin(); it != m_bandButtons.end(); ++it)
        if (it.value()->isChecked() && m_sweepsByBand.contains(it.key()))
            bands << it.key();
    std::sort(bands.begin(), bands.end(),
              [](const QString& a, const QString& b) {
                  return bandRank(a) < bandRank(b);
              });
    if (bands.isEmpty()) {
        QMessageBox::information(this, "No band selected",
            "Tick at least one band button on the SWR scan tab to choose "
            "which sweep(s) to export.");
        return;
    }
    const bool multi = bands.size() > 1;
    if (multi) {
        const auto btn = QMessageBox::question(this, "Export multiple bands",
            QString("%1 bands are ticked and will all be written to one "
                    "file:\n\n    %2\n\nExport all of them? "
                    "(Untick bands on the SWR tab to narrow it down.)")
                .arg(bands.size()).arg(bands.join(", ")),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (btn != QMessageBox::Yes) return;
    }
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString defaultName =
        QString("swr-sweep-%1-%2.csv").arg(bands.join('-')).arg(stamp);
    QSettings settings;   // share the remembered folder with Save log…
    const QString fallbackDir =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString dir = settings.value("log/lastSaveDir", fallbackDir).toString();
    const QString path = QFileDialog::getSaveFileName(this, "Save SWR sweep",
                            dir + "/" + defaultName,
                            "CSV files (*.csv);;All files (*)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::critical(this, "Save failed", f.errorString());
        return;
    }
    QTextStream out(&f);
    out << "# TCI Monitor SWR sweep\n";
    int total = 0;
    for (const QString& band : bands) {
        const auto& m = m_sweepsByBand.value(band);
        qint64 minF = 0; double minS = 1e9;
        for (auto it = m.begin(); it != m.end(); ++it)
            if (it.value() < minS) { minS = it.value(); minF = it.key(); }
        out << "# " << band << ": " << m.size() << " pts, captured "
            << m_sweepStampByBand.value(band) << ", min SWR "
            << QString::number(minS, 'f', 2) << " @ "
            << QString::number(minF / 1e6, 'f', 6) << " MHz\n";
        total += m.size();
    }
    out << (multi ? "band,frequency_mhz,swr\n" : "frequency_mhz,swr\n");
    for (const QString& band : bands) {
        const auto& m = m_sweepsByBand.value(band);
        // QMap iterates sorted by key → ascending frequency per band.
        for (auto it = m.begin(); it != m.end(); ++it) {
            if (multi) out << band << ',';
            out << QString::number(it.key() / 1e6, 'f', 6) << ','
                << QString::number(it.value(), 'f', 2) << '\n';
        }
    }
    f.close();

    settings.setValue("log/lastSaveDir", QFileInfo(path).absolutePath());
    statusBar()->showMessage(
        QString("Saved %1 (%2 band%3, %4 pts)")
            .arg(QFileInfo(path).fileName())
            .arg(bands.size()).arg(bands.size() == 1 ? "" : "s").arg(total),
        4000);
}

void MainWindow::onFilterChanged(const QString&)
{
    // Filter applies to future appends; existing rows stay put.  Tell the
    // user via a brief status-bar nudge.
    statusBar()->showMessage(
        "Filter applies to new rows — use Clear to apply retroactively", 3000);
}

// ── Pause / suppress controls ──────────────────────────────────────────

void MainWindow::onPauseToggled(bool checked)
{
    m_paused = checked;
    m_pauseBtn->setText(checked ? "Paused — click to resume" : "Pause");
    statusBar()->showMessage(checked ? "Paused — log freezes (parsed views still update)"
                                     : "Resumed",
                             2500);
}

void MainWindow::onLogContextMenu(const QPoint& pos)
{
    const auto rows = m_logTable->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;

    // Pull the cmd of the row under the cursor (or the first selected row
    // if the user right-clicked outside their selection).
    const QModelIndex idx = m_logTable->indexAt(pos);
    QString cmdHere;
    if (idx.isValid()) {
        auto* it = m_logTable->item(idx.row(), 1);
        if (it) cmdHere = it->text();
    }
    if (cmdHere.isEmpty()) {
        auto* it = m_logTable->item(rows.first().row(), 1);
        if (it) cmdHere = it->text();
    }

    QMenu menu(this);
    QAction* removeAct = menu.addAction(
        rows.size() == 1 ? QString("Remove this row")
                         : QString("Remove %1 selected rows").arg(rows.size()));
    QAction* suppressAct = nullptr;
    if (!cmdHere.isEmpty()) {
        suppressAct = menu.addAction(
            QString("Suppress all '%1' messages").arg(cmdHere));
    }
    menu.addSeparator();
    QAction* describeAct = nullptr;
    if (!cmdHere.isEmpty()) {
        describeAct = menu.addAction(
            QString("Description of '%1'…").arg(cmdHere));
    }
    menu.addSeparator();
    QAction* showSupAct = menu.addAction("Show suppressions…");
    QAction* refAct     = menu.addAction("TCI commands reference…");

    QAction* chosen = menu.exec(m_logTable->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == removeAct) {
        // Remove rows in reverse so earlier indices stay valid.
        QList<int> rowNums;
        for (const auto& mi : rows) rowNums << mi.row();
        std::sort(rowNums.begin(), rowNums.end(), std::greater<int>());
        for (int r : rowNums) m_logTable->removeRow(r);
    } else if (suppressAct && chosen == suppressAct) {
        m_suppressed.insert(cmdHere);
        // Also remove existing rows of this type for instant relief.
        for (int r = m_logTable->rowCount() - 1; r >= 0; --r) {
            auto* it = m_logTable->item(r, 1);
            if (it && it->text() == cmdHere) m_logTable->removeRow(r);
        }
        refreshSuppressionUi();
    } else if (describeAct && chosen == describeAct) {
        const TciCommand* cmd = findTciCommand(cmdHere);
        CommandDescriptionDialog dlg(cmd, cmdHere, this);
        dlg.exec();
    } else if (chosen == showSupAct) {
        onShowSuppressions();
    } else if (chosen == refAct) {
        onShowCommandsReference();
    }
}

void MainWindow::onShowSuppressions()
{
    QDialog dlg(this);
    dlg.setWindowTitle("Suppressed message types");
    dlg.resize(560, 360);

    auto* layout = new QVBoxLayout(&dlg);
    auto* hint = new QLabel(
        "These command prefixes are dropped before reaching the log.  "
        "Select one or more and click Remove to start showing them again. "
        "Double-click a row to see the full description.");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* tree = new QTreeWidget;
    tree->setColumnCount(2);
    tree->setHeaderLabels({"Command", "Description"});
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->header()->setStretchLastSection(true);
    tree->setColumnWidth(0, 160);

    QStringList items(m_suppressed.begin(), m_suppressed.end());
    items.sort();
    for (const QString& name : items) {
        auto* it = new QTreeWidgetItem(tree);
        it->setText(0, name);
        const TciCommand* cmd = findTciCommand(name);
        it->setText(1, cmd ? cmd->summary
                           : QStringLiteral("(no description on file)"));
        if (!cmd) {
            it->setForeground(1, QColor("#6b8099"));
        }
    }
    layout->addWidget(tree, 1);

    auto* row = new QHBoxLayout;
    auto* describeBtn = new QPushButton("Describe…");
    auto* removeBtn   = new QPushButton("Remove selected");
    auto* clearBtn    = new QPushButton("Clear all");
    auto* closeBtn    = new QPushButton("Close");
    row->addWidget(describeBtn);
    row->addSpacing(12);
    row->addWidget(removeBtn);
    row->addWidget(clearBtn);
    row->addStretch();
    row->addWidget(closeBtn);
    layout->addLayout(row);

    auto describeSelected = [this, &dlg, tree]() {
        auto sel = tree->selectedItems();
        if (sel.isEmpty()) return;
        const QString name = sel.first()->text(0);
        const TciCommand* cmd = findTciCommand(name);
        CommandDescriptionDialog desc(cmd, name, &dlg);
        desc.exec();
    };

    connect(describeBtn, &QPushButton::clicked, &dlg, describeSelected);
    connect(tree, &QTreeWidget::itemDoubleClicked, &dlg,
            [describeSelected](QTreeWidgetItem*, int) { describeSelected(); });

    connect(removeBtn, &QPushButton::clicked, &dlg, [&]() {
        for (auto* it : tree->selectedItems()) {
            m_suppressed.remove(it->text(0));
            delete tree->takeTopLevelItem(tree->indexOfTopLevelItem(it));
        }
    });
    connect(clearBtn, &QPushButton::clicked, &dlg, [&]() {
        m_suppressed.clear();
        tree->clear();
    });
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.exec();

    refreshSuppressionUi();
}

void MainWindow::onClearSuppressions()
{
    m_suppressed.clear();
    refreshSuppressionUi();
}

// ── Suppression persistence ────────────────────────────────────────────

void MainWindow::refreshSuppressionUi()
{
    const int n = m_suppressed.size();
    m_suppressLabel->setText(n == 0
        ? QStringLiteral("0 suppressions")
        : QString("%1 suppression%2").arg(n).arg(n == 1 ? "" : "s"));
    m_clearSuppressBtn->setEnabled(n > 0);

    // Persist so the same filters re-apply on next launch (shared G0JKN /
    // "TCI Monitor" QSettings).
    QSettings settings;
    settings.setValue("log/suppressed",
        QStringList(m_suppressed.begin(), m_suppressed.end()));
}

void MainWindow::restoreSuppressions()
{
    QSettings settings;
    const QStringList saved = settings.value("log/suppressed").toStringList();
    for (const QString& cmd : saved) m_suppressed.insert(cmd);
    refreshSuppressionUi();
}

void MainWindow::onShowCommandsReference()
{
    CommandsReferenceDialog dlg(this);
    dlg.exec();
}

void MainWindow::onDiscoverClicked()
{
    DiscoveryDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        const QString host = dlg.chosenHost();
        const quint16 port = dlg.chosenPort();
        if (!host.isEmpty()) {
            m_hostEdit->setText(host);
            // Port 0 in the advertisement means "no TCI server" — keep the
            // user's existing spinbox value rather than overwriting with 0.
            if (port != 0) m_portSpin->setValue(port);
        }
    }
}

} // namespace TciMon
