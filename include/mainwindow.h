#pragma once
#include <QMainWindow>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QProgressBar>
#include <QComboBox>
#include <QFrame>
#include <QScrollArea>
#include <functional>
#include "elm327connection.h"
#include "kwp2000handler.h"
#include "wjdiagnostics.h"
#include "livedata.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent=nullptr);
    ~MainWindow();
private slots:
    void onConnect();
    void onDisconnect();
    void onConnectionStateChanged(ELM327Connection::ConnectionState);
    void onReadDTCs();
    void onClearDTCs();
    void onStartLiveData();
    void onStopLiveData();
    void onLiveDataUpdated(const QMap<uint8_t,double>&);
    void onFullStatusUpdated(const TCMDiagnostics::TCMStatus&);
    void onECUDataUpdated(const TCMDiagnostics::ECUStatus&);
    void onLogMessage(const QString&);
    void onRawBusDump();
    void onRawSendCustom();
    void scanBluetoothDevices();
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    void requestBluetoothPermissions();
    void requestBluetoothPermissionOnly();
#endif
private:
    void setupUI();
    void runDiscoveryPhases(
        std::function<void(const QString&, const QString&)> log,
        std::function<void()> done);
    QWidget* createDashboardPanel();
    void rebuildDashboard();
    QFrame* createGaugeCard(const QString&,const QString&,const QString&,QLabel**,QLabel**);
    QWidget* createConnectionTab();
    QWidget* createDTCTab();
    QWidget* createLiveDataTab();
    QWidget* createLogTab();
    void updateDashboardFromLiveData(const QMap<uint8_t,double>&);
    void updateStatusLabels(const TCMDiagnostics::TCMStatus&);
    void updateActiveHeaderLabel();
    void updateLiveTableForModule();
    void setGaugeColor(QLabel*,const QString&);
    QString gearToString(TCMDiagnostics::Gear);
#ifdef Q_OS_ANDROID
    void keepScreenOn(bool);
#endif
    ELM327Connection *m_elm; TCMDiagnostics *m_tcm; LiveDataManager *m_liveData;
    QTabWidget *m_tabs;
    QLineEdit *m_hostEdit; QSpinBox *m_portSpin;
    QPushButton *m_connectBtn,*m_disconnectBtn;
    QPushButton *m_btScanBtn,*m_btConnectBtn;
    QComboBox *m_btCombo;
    QLabel *m_activeHeaderLabel;
    // Module system: single active module
    WJDiagnostics::Module m_activeModId = WJDiagnostics::Module::KLineTCM;
    bool m_moduleSessionActive = false;
    QList<QPushButton*> m_moduleButtons;
    QVBoxLayout *m_moduleListLayout = nullptr;
    QScrollArea *m_modScroll = nullptr;
    QLabel *m_connStatusLabel;
    QTimer *m_batteryTimer = nullptr;  // ATRV periyodik okuma
    QString m_lastLogPath;
    QTableWidget *m_dtcTable; QPushButton *m_readDtcBtn,*m_clearDtcBtn; QLabel *m_dtcCountLabel;
    QPushButton *m_dtcTcmBtn=nullptr,*m_dtcEcuBtn=nullptr,*m_dtcAbsBtn=nullptr,*m_dtcAirbagBtn=nullptr;
    int m_dtcSourceIdx = 0; // 0=TCM, 1=ECU, 2=ABS, 3=Airbag
    QTableWidget *m_liveTable; QPushButton *m_startLiveBtn,*m_stopLiveBtn,*m_logBtn;
    QLabel *m_dashGearVal,*m_dashGearUnit;
    QLabel *m_dashSpeedVal,*m_dashSpeedUnit;
    QLabel *m_dashRpmVal,*m_dashRpmUnit;
    QLabel *m_dashCoolantVal,*m_dashCoolantUnit;
    QLabel *m_dashSolVoltVal,*m_dashSolVoltUnit;
    QLabel *m_dashBatVoltVal,*m_dashBatVoltUnit;
    QLabel *m_dashMotCoolVal,*m_dashMotCoolUnit;
    QLabel *m_dashLimpVal,*m_dashLimpUnit;
    // Motor ECU gauges (Row 3)
    QLabel *m_dashMotRpmVal,*m_dashMotRpmUnit;
    QLabel *m_dashMotBoostVal,*m_dashMotBoostUnit;
    QLabel *m_dashMotMafVal,*m_dashMotMafUnit;
    QLabel *m_dashMotRailVal,*m_dashMotRailUnit;
    // ECU protected data gauges
    QLabel *m_dashEgrVal=nullptr,*m_dashEgrUnit=nullptr;
    QLabel *m_dashWgVal=nullptr,*m_dashWgUnit=nullptr;
    QLabel *m_dashInjAdaptVal=nullptr,*m_dashInjAdaptUnit=nullptr;
    QLabel *m_dashFuelAdaptVal=nullptr,*m_dashFuelAdaptUnit=nullptr;
    QLabel *m_dashBoostAdaptVal=nullptr,*m_dashBoostAdaptUnit=nullptr;
    QLabel *m_dashOilPressVal=nullptr,*m_dashOilPressUnit=nullptr;
    // Dashboard container
    QWidget *m_dashStack=nullptr;
    QVBoxLayout *m_dashLayout=nullptr;
    QProgressBar *m_throttleBar;
    // ABS tab
    QTableWidget *m_absDtcTable=nullptr;
    QPushButton *m_absReadDtcBtn=nullptr, *m_absClearDtcBtn=nullptr, *m_absLiveBtn=nullptr;
    QLabel *m_absLFLabel=nullptr, *m_absRFLabel=nullptr, *m_absLRLabel=nullptr, *m_absRRLabel=nullptr;
    QLabel *m_absSpeedLabel=nullptr, *m_absDtcCountLabel=nullptr;
    // Airbag tab
    QTableWidget *m_airbagDtcTable=nullptr;
    QPushButton *m_airbagReadDtcBtn=nullptr, *m_airbagClearDtcBtn=nullptr;
    QLabel *m_airbagDtcCountLabel=nullptr;
    QTextEdit *m_logText;
    QPushButton *m_rawDumpBtn, *m_rawSendBtn;
    QLineEdit *m_rawCmdEdit;
    QSpinBox *m_timeoutSpin;
    bool m_rawDumping = false;
};
