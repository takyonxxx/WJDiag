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

#include "elm327connection.h"
#include "kwp2000handler.h"
#include "tcmdiagnostics.h"
#include "livedata.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onConnect();
    void onDisconnect();
    void onConnectionStateChanged(ELM327Connection::ConnectionState state);

    void onStartSession();
    void onReadDTCs();
    void onClearDTCs();

    void onStartLiveData();
    void onStopLiveData();
    void onLiveDataUpdated(const QMap<uint8_t, double> &values);
    void onFullStatusUpdated(const TCMDiagnostics::TCMStatus &status);

    void onReadIO();
    void onReadTCMInfo();

    void onLogMessage(const QString &msg);

private:
    void setupUI();
    QWidget* createConnectionTab();
    QWidget* createDTCTab();
    QWidget* createLiveDataTab();
    QWidget* createIOTab();
    QWidget* createLogTab();

    void updateStatusLabels(const TCMDiagnostics::TCMStatus &status);
    QString gearToString(TCMDiagnostics::Gear gear);

#ifdef Q_OS_ANDROID
    void keepScreenOn(bool on);
#endif

    // Core objects
    ELM327Connection *m_elm;
    TCMDiagnostics   *m_tcm;
    LiveDataManager  *m_liveData;

    // UI elements
    QTabWidget   *m_tabs;

    // Connection tab
    QLineEdit    *m_hostEdit;
    QSpinBox     *m_portSpin;
    QPushButton  *m_connectBtn;
    QPushButton  *m_disconnectBtn;
    QPushButton  *m_startSessionBtn;
    QLabel       *m_connStatusLabel;
    QLabel       *m_elmVersionLabel;
    QLabel       *m_batteryVoltLabel;

    // TCM Info
    QLabel       *m_tcmPartLabel;
    QLabel       *m_tcmSwLabel;
    QLabel       *m_tcmHwLabel;

    // DTC tab
    QTableWidget *m_dtcTable;
    QPushButton  *m_readDtcBtn;
    QPushButton  *m_clearDtcBtn;
    QLabel       *m_dtcCountLabel;

    // Live Data tab
    QTableWidget *m_liveTable;
    QPushButton  *m_startLiveBtn;
    QPushButton  *m_stopLiveBtn;
    QPushButton  *m_logBtn;

    // Status panel
    QLabel       *m_gearLabel;
    QLabel       *m_rpmLabel;
    QLabel       *m_tempLabel;
    QLabel       *m_solVoltLabel;  // ← P2602 selenoid voltaj!
    QLabel       *m_limpLabel;
    QProgressBar *m_throttleBar;

    // I/O tab
    QTableWidget *m_ioTable;
    QPushButton  *m_readIOBtn;

    // Log tab
    QTextEdit    *m_logText;
};
