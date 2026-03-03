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
#include <QStatusBar>
#include <QProgressBar>
#include <QComboBox>
#include <QFrame>
#include "elm327connection.h"
#include "kwp2000handler.h"
#include "tcmdiagnostics.h"
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
    void onReadIO();
    void onLogMessage(const QString&);
    void onRawBusDump();
    void onRawSendCustom();
private:
    void setupUI();
    QWidget* createDashboardPanel();
    QFrame* createGaugeCard(const QString&,const QString&,const QString&,QLabel**,QLabel**);
    QWidget* createConnectionTab();
    QWidget* createDTCTab();
    QWidget* createLiveDataTab();
    QWidget* createIOTab();
    QWidget* createABSTab();
    QWidget* createAirbagTab();
    QWidget* createLogTab();
    void updateDashboardFromLiveData(const QMap<uint8_t,double>&);
    void updateStatusLabels(const TCMDiagnostics::TCMStatus&);
    void updateActiveHeaderLabel();
    void setGaugeColor(QLabel*,const QString&);
    QString gearToString(TCMDiagnostics::Gear);
#ifdef Q_OS_ANDROID
    void keepScreenOn(bool);
#endif
    ELM327Connection *m_elm; TCMDiagnostics *m_tcm; LiveDataManager *m_liveData;
    QTabWidget *m_tabs;
    QLineEdit *m_hostEdit; QSpinBox *m_portSpin;
    QPushButton *m_connectBtn,*m_disconnectBtn,*m_startSessionBtn,*m_startEcuBtn;
    QLabel *m_activeHeaderLabel;
    bool m_tcmSessionActive = false;
    bool m_ecuSessionActive = false;
    QLabel *m_connStatusLabel,*m_elmVersionLabel;
    QTimer *m_batteryTimer = nullptr;  // ATRV periyodik okuma
    QString m_lastLogPath;
    QTableWidget *m_dtcTable; QPushButton *m_readDtcBtn,*m_clearDtcBtn; QLabel *m_dtcCountLabel;
    QPushButton *m_dtcTcmBtn,*m_dtcEcuBtn;
    bool m_dtcSourceECU = false; // false=TCM, true=ECU
    QTableWidget *m_liveTable; QPushButton *m_startLiveBtn,*m_stopLiveBtn,*m_logBtn;
    QComboBox *m_modeCombo;
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
    QProgressBar *m_throttleBar;
    QTableWidget *m_ioTable; QPushButton *m_readIOBtn;
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
    bool m_rawDumping = false;
};
