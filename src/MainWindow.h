#pragma once

// MainWindow — TCI Monitor's only window.
//
// Layout:
//   • Top connect bar  — host + port + [Connect]/[Disconnect] + status
//   • Splitter:
//       Left  — parsed view: current VFO/mode + spot table
//       Right — raw message log (color-coded by message type)
//   • Bottom action bar — [Save log…] [Clear log] [Filter: ...]
//
// Parsing happens in this class (not in TciClient) — the client just
// emits raw lines and we slice them into structured forms here.

#include <QMainWindow>
#include <QMap>
#include <QSet>
#include <QString>

class QLineEdit;
class QSpinBox;
class QPushButton;
class QLabel;
class QTableWidget;
class QCheckBox;
class QTabWidget;
class QHBoxLayout;

namespace TciMon {

class TciClient;
class SwrPlot;
class InspectorPanel;
class ConsolePanel;
class ComparePanel;
class ReplayPanel;
class CalibrationPanel;
class SignalPanel;
class WaterfallIdPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onConnectClicked();
    void onDisconnectClicked();
    void onConnectionChanged(bool connected);
    void onRawMessage(const QString& line);
    void onErrorText(const QString& text);
    void onSaveLog();
    void onSaveSweep();
    void onClearLog();
    void onFilterChanged(const QString& text);
    void onPauseToggled(bool checked);
    void onLogContextMenu(const QPoint& pos);
    void onShowSuppressions();
    void onClearSuppressions();
    void onShowCommandsReference();
    void onDiscoverClicked();

private:
    void buildUI();
    void appendLog(const QString& line, const QString& colorHex);
    void parseLine(const QString& line);
    void handleVfo(const QStringList& args);
    void handleMode(const QStringList& args);
    void handleSpot(const QStringList& args);
    void handleSpotDelete(const QStringList& args);
    void handleSpotClear();
    void handleTrx(const QStringList& args);        // sweep start/stop
    void handleTxSensors(const QStringList& args);  // SWR samples
    void refreshSweepUi();                          // live readout + save btn
    void finalizeSweep();          // bucket completed sweep into its band
    void rebuildBandButtons();     // sync SWR-tab band button row
    void rebuildPlot();            // push checked bands' curves to the plot
    void persistSweeps();          // save per-band sweeps to QSettings
    void restoreSweeps();          // load per-band sweeps at startup
    static QString bandForHz(qint64 hz);
    void refreshStatus();
    void refreshSuppressionUi();   // update label + clear-button, persist set
    void restoreSuppressions();    // load persisted suppressions at startup

    TciClient* m_tci{nullptr};

    // Toolkit tabs (Monitor is tab 0; the rest are the v0.3 tools)
    QTabWidget*     m_topTabs{};
    InspectorPanel*   m_inspector{};
    ConsolePanel*     m_console{};
    ComparePanel*     m_compare{};
    ReplayPanel*      m_replay{};
    CalibrationPanel* m_cal{};
    SignalPanel*      m_signal{};
    WaterfallIdPanel* m_wfid{};

    // Connect bar
    QLineEdit*   m_hostEdit{};
    QSpinBox*    m_portSpin{};
    QPushButton* m_connectBtn{};
    QPushButton* m_disconnectBtn{};
    QPushButton* m_discoverBtn{};
    QLabel*      m_statusDot{};
    QLabel*      m_statusText{};

    // Parsed display
    QLabel*       m_curVfo{};
    QLabel*       m_curMode{};
    QLabel*       m_sweepLabel{};   // live SWR-sweep readout
    QTableWidget* m_spotTable{};

    // SWR sweep capture. AE's antenna sweep keys TX (trx:0,true) and steps
    // vfo:0,0,<hz>; while emitting tx_sensors:0,mic,fwd,peak,swr; — we pair
    // the most-recent vfo freq with each tx_sensors SWR until trx:0,false.
    qint64              m_sweepCurFreqHz{0};   // latest vfo: freq (RX0/VFO0)
    double              m_sweepCurSwr{0.0};    // latest SWR seen in a sweep
    bool                m_sweepActive{false};  // between trx true→false
    QString             m_sweepStartStamp;     // wall-clock at sweep start
    QMap<qint64,double> m_sweepSwr;            // freqHz → min SWR (in-progress)
    QMap<qint64,double> m_lastSweep;           // last completed sweep
    QString             m_lastSweepBand;       // band of last completed sweep
    QPushButton*        m_saveSweepBtn{};

    // Per-band sweep store (persisted) + SWR tab UI
    QMap<QString, QMap<qint64,double>> m_sweepsByBand;
    QMap<QString, QString>             m_sweepStampByBand;
    QTabWidget*                        m_leftTabs{};
    SwrPlot*                           m_swrPlot{};
    QHBoxLayout*                       m_bandBtnLayout{};
    QMap<QString, QPushButton*>        m_bandButtons;

    // Raw log — table so we can right-click rows and suppress message
    // types that flood the stream (rx_smeter etc).
    QTableWidget*  m_logTable{};
    QLineEdit*     m_filterEdit{};
    QCheckBox*     m_autoscrollCheck{};
    QPushButton*   m_pauseBtn{};
    QLabel*        m_suppressLabel{};
    QPushButton*   m_clearSuppressBtn{};

    bool           m_paused{false};
    QSet<QString>  m_suppressed;          // lowercase cmd names to drop

    // Action row
    QPushButton* m_saveBtn{};
    QPushButton* m_clearBtn{};

    // Stats / counters in status bar
    QLabel* m_sbMsgCount{};
    QLabel* m_sbSpotCount{};
    int     m_msgCount{0};
    int     m_spotCount{0};
};

} // namespace TciMon
