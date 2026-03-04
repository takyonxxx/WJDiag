#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QDateTime>
#include <QFont>
#include <QColor>
#include <QApplication>
#include <QStandardPaths>
#include <QTimer>
#include <QGuiApplication>
#include <QClipboard>
#include <QScreen>
#include <QScrollArea>
#include <QScroller>
#include <QDir>
#include <QFrame>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Core nesneleri oluştur
    m_elm      = new ELM327Connection(this);
    m_tcm      = new TCMDiagnostics(m_elm, this);
    m_liveData = new LiveDataManager(m_tcm, this);

    setupUI();

    // Sinyalleri bağla
    connect(m_elm, &ELM327Connection::stateChanged,
            this, &MainWindow::onConnectionStateChanged);
    connect(m_elm, &ELM327Connection::logMessage,
            this, &MainWindow::onLogMessage);
    connect(m_tcm, &TCMDiagnostics::logMessage,
            this, &MainWindow::onLogMessage);
    connect(m_tcm->kwp(), &KWP2000Handler::logMessage,
            this, &MainWindow::onLogMessage);

    connect(m_liveData, &LiveDataManager::dataUpdated,
            this, &MainWindow::onLiveDataUpdated);
    connect(m_liveData, &LiveDataManager::fullStatusUpdated,
            this, &MainWindow::onFullStatusUpdated);
    connect(m_liveData, &LiveDataManager::ecuDataUpdated,
            this, &MainWindow::onECUDataUpdated);

    setWindowTitle("WJ Diag - Jeep Grand Cherokee 2.7 CRD | NAG1 722.6 TCM");

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    showMaximized();
#ifdef Q_OS_ANDROID
    keepScreenOn(true);
#endif
#else
    // Windows/macOS: iPhone 16 Pro logical resolution ile basla (393x852pt)
    resize(393, 852);
    setMinimumSize(360, 640);
#endif
}

#ifdef Q_OS_ANDROID
void MainWindow::keepScreenOn(bool on)
{
    // Android FLAG_KEEP_SCREEN_ON
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([on]() {
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        QJniObject window = activity.callObjectMethod(
            "getWindow", "()Landroid/view/Window;");
        if (window.isValid()) {
            const int FLAG_KEEP_SCREEN_ON = 128;
            if (on) {
                window.callMethod<void>("addFlags", "(I)V", FLAG_KEEP_SCREEN_ON);
            } else {
                window.callMethod<void>("clearFlags", "(I)V", FLAG_KEEP_SCREEN_ON);
            }
        }
    });
}
#endif

MainWindow::~MainWindow()
{
    m_liveData->stopPolling();
    m_elm->disconnect();
#ifdef Q_OS_ANDROID
    keepScreenOn(false);
#endif
}

void MainWindow::setupUI()
{
    QWidget *central = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // Mobil: iPhone 16 Pro 393x852pt, Android benzer
    mainLayout->setContentsMargins(3, 2, 3, 2);
    mainLayout->setSpacing(2);
    QFont appFont = QApplication::font();
    appFont.setPointSize(10);
    QApplication::setFont(appFont);
#else
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(4);
#endif

    // === ÜST PANEL: Durum Göstergeleri ===
    // === DASHBOARD PANEL (11 gauge) ===
    mainLayout->addWidget(createDashboardPanel());
    m_throttleBar = new QProgressBar();
    m_throttleBar->setRange(0, 100);
    m_throttleBar->setVisible(false);
    mainLayout->addWidget(m_throttleBar);

    // === TABLAR ===
    m_tabs = new QTabWidget();
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // Mobil: kompakt tab basliklari
    m_tabs->setStyleSheet(
        "QTabWidget::pane{border:1px solid #2a2a4a;border-top:none;}"
        "QTabBar::tab{background:#1a1a2e;color:#8888aa;padding:4px 6px;"
        "border:1px solid #2a2a4a;border-bottom:none;border-radius:3px 3px 0 0;"
        "font-size:9px;min-width:40px;}"
        "QTabBar::tab:selected{background:#2a2a4a;color:#00ff88;font-weight:bold;}"
    );
    m_tabs->tabBar()->setExpanding(true);
#endif
    m_tabs->addTab(createConnectionTab(), "Connect");
    m_tabs->addTab(createDTCTab(),        "Faults");
    m_tabs->addTab(createLiveDataTab(),   "Live Data");
    m_tabs->addTab(createIOTab(),         "I/O");
    m_tabs->addTab(createLogTab(),        "Log");

    mainLayout->addWidget(m_tabs);
    setCentralWidget(central);

    // Durum çubuğu
    //statusBar()->showMessage("Waiting for connection...");
}


// ================================================================
// Dashboard - 11 gauge responsive grid
// ================================================================

QFrame* MainWindow::createGaugeCard(const QString &title, const QString &initValue,
    const QString &unit, QLabel **valueLabel, QLabel **unitLabel)
{
    QFrame *card = new QFrame();
    card->setFrameShape(QFrame::StyledPanel);
    card->setStyleSheet("QFrame{background:#1a1a2e;border:1px solid #2a2a4a;border-radius:4px;padding:2px;}");
    card->setMinimumWidth(70);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout *lay = new QVBoxLayout(card);
    lay->setContentsMargins(2,1,2,1); lay->setSpacing(0);
    QLabel *tl = new QLabel(title);
    tl->setStyleSheet("color:#8888aa;font-size:8px;border:none;background:transparent;");
    tl->setAlignment(Qt::AlignCenter); lay->addWidget(tl);
    QLabel *vl = new QLabel(initValue);
    vl->setAlignment(Qt::AlignCenter);
    vl->setStyleSheet("color:#00ff88;font-size:16px;font-weight:bold;"
        "font-family:'Consolas','Courier New',monospace;border:none;background:transparent;");
    lay->addWidget(vl);
    QLabel *ul = new QLabel(unit);
    ul->setAlignment(Qt::AlignCenter);
    ul->setStyleSheet("color:#6666aa;font-size:7px;border:none;background:transparent;");
    lay->addWidget(ul);
    *valueLabel = vl; *unitLabel = ul;
    return card;
}

QWidget* MainWindow::createDashboardPanel()
{
    QWidget *p = new QWidget();
    QGridLayout *g = new QGridLayout(p);
    g->setContentsMargins(2,2,2,2); g->setSpacing(3);

    // Row 0: Vites, Hiz, Turbin RPM, Trans Sicaklik
    g->addWidget(createGaugeCard("VITES","---","",&m_dashGearVal,&m_dashGearUnit), 0,0);
    g->addWidget(createGaugeCard("HIZ","---","km/h",&m_dashSpeedVal,&m_dashSpeedUnit), 0,1);
    g->addWidget(createGaugeCard("TURBIN","---","rpm",&m_dashRpmVal,&m_dashRpmUnit), 0,2);
    g->addWidget(createGaugeCard("TRANS","---","C",&m_dashCoolantVal,&m_dashCoolantUnit), 0,3);

    // Row 1: Selenoid V, Aku V, Su Sicak (Motor), Limp
    g->addWidget(createGaugeCard("SOL V","---","V",&m_dashSolVoltVal,&m_dashSolVoltUnit), 1,0);
    g->addWidget(createGaugeCard("AKU","---","V",&m_dashBatVoltVal,&m_dashBatVoltUnit), 1,1);
    g->addWidget(createGaugeCard("SU","---","C",&m_dashMotCoolVal,&m_dashMotCoolUnit), 1,2);
    g->addWidget(createGaugeCard("LIMP","---","",&m_dashLimpVal,&m_dashLimpUnit), 1,3);

    // Row 2: Motor ECU verileri (DUAL modda guncellenir)
    g->addWidget(createGaugeCard("M.RPM","---","rpm",&m_dashMotRpmVal,&m_dashMotRpmUnit), 2,0);
    g->addWidget(createGaugeCard("BOOST","---","mbar",&m_dashMotBoostVal,&m_dashMotBoostUnit), 2,1);
    g->addWidget(createGaugeCard("MAF","---","mg/s",&m_dashMotMafVal,&m_dashMotMafUnit), 2,2);
    g->addWidget(createGaugeCard("RAIL","---","bar",&m_dashMotRailVal,&m_dashMotRailUnit), 2,3);

    for(int c=0;c<4;++c) g->setColumnStretch(c,1);
    return p;
}

void MainWindow::setGaugeColor(QLabel *vl, const QString &c) {
    vl->setStyleSheet(QString("color:%1;font-size:16px;font-weight:bold;"
        "font-family:'Consolas','Courier New',monospace;border:none;background:transparent;").arg(c));
}

void MainWindow::updateDashboardFromLiveData(const QMap<uint8_t, double> &v)
{
    // TCM J1850 VPW PIDs (APK referansi)
    if(v.contains(0x01)){
        int g=(int)v[0x01];
        m_dashGearVal->setText(gearToString((TCMDiagnostics::Gear)g));
        setGaugeColor(m_dashGearVal, g>=3 ? "#00ff88" : "#ffcc44");
    }
    if(v.contains(0x20)) m_dashSpeedVal->setText(QString::number(v[0x20],'f',0));       // Vehicle Speed
    if(v.contains(0x10)) m_dashRpmVal->setText(QString::number(v[0x10],'f',0));          // Turbine RPM
    if(v.contains(0x16)){                                                                 // Solenoid Supply
        double sv=v[0x16];
        m_dashSolVoltVal->setText(QString::number(sv,'f',1));
        setGaugeColor(m_dashSolVoltVal, sv<9.0?"#ff4444":sv<11.0?"#ffaa00":"#00ff88");
    }
    // Limp mode: max gear <= 2 ise limp
    if(v.contains(0x03)){
        bool l = v[0x03] <= 2 && v.value(0x14, 0) > 100;
        m_dashLimpVal->setText(l?"ACTIVE!":"Normal");
        setGaugeColor(m_dashLimpVal, l?"#ff4444":"#00ff88");
    }
    // Trans temp
    if(v.contains(0x14)){
        double ct=v[0x14];
        m_dashCoolantVal->setText(QString::number(ct,'f',0));
        setGaugeColor(m_dashCoolantVal, ct>105?"#ff4444":ct>95?"#ffaa00":"#00ff88");
    }

    // Motor ECU KWP local ID'ler (dual mode veya ECU canli veri)
    // 0x20 = coolant temp block'undan gelir (KWP ReadDataByLocalID)
    if(v.contains(0xE0)){
        // 0xE0 = ECU coolant temp (ozel mapping, Motor ECU oturumundan)
        double ct = v[0xE0];
        m_dashMotCoolVal->setText(QString::number(ct,'f',0));
        setGaugeColor(m_dashMotCoolVal,
            ct > 105 ? "#ff4444" : ct > 95 ? "#ffaa00" : "#00ff88");
    }
}

QWidget* MainWindow::createConnectionTab()
{
    QScrollArea *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture);

    QWidget *w = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(w);

    // Bağlantı ayarları
    // Uyumluluk bilgisi
    QLabel *compatInfo = new QLabel(
        "Jeep Grand Cherokee 2002-2005 WJ/WG 2.7 CRD Only\n"
        "Engine ECU: K-Line (ATSP5) | TCM/ABS/Airbag/Other: J1850 VPW (ATSP2)\n"
        "Orijinal ELM327 onerisi: ATFI, ATWM, ATSH destegi gerekli");
    compatInfo->setStyleSheet("background:#1a2a1a;padding:4px;border-radius:4px;"
                              "color:#88cc88;font-family:monospace;font-size:9px;");
    compatInfo->setWordWrap(true);
    layout->addWidget(compatInfo);


    QGroupBox *connBox = new QGroupBox("ELM327 Connection");
    QGridLayout *connGrid = new QGridLayout(connBox);

    // WiFi row
    connGrid->addWidget(new QLabel("WiFi:"), 0, 0);
    m_hostEdit = new QLineEdit("192.168.0.10");
    connGrid->addWidget(m_hostEdit, 0, 1);
    m_portSpin = new QSpinBox();
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(35000);
    m_portSpin->setMaximumWidth(80);
    connGrid->addWidget(m_portSpin, 0, 2);
    m_connectBtn = new QPushButton("WiFi Connect");
    m_connectBtn->setMinimumHeight(34);
    connGrid->addWidget(m_connectBtn, 0, 3);

    // Bluetooth row
    connGrid->addWidget(new QLabel("BT:"), 1, 0);
    m_btCombo = new QComboBox();
    m_btCombo->setPlaceholderText("Select Bluetooth device...");
    m_btCombo->setStyleSheet("QComboBox{background:#1a1a2e;color:#88ccff;border:1px solid #3a3a6a;"
                             "border-radius:3px;padding:2px 4px;}");
    connGrid->addWidget(m_btCombo, 1, 1, 1, 2);
    m_btScanBtn = new QPushButton("Scan");
    m_btScanBtn->setMinimumHeight(34);
    m_btScanBtn->setStyleSheet("QPushButton{background:#1a2a4a;color:#88ccff;border:1px solid #3a5a8a;"
                               "border-radius:3px;font-weight:bold;}");
    connGrid->addWidget(m_btScanBtn, 1, 3);

    // BT Baglan + Disconnect
    m_btConnectBtn = new QPushButton("BT Connect");
    m_btConnectBtn->setMinimumHeight(34);
    m_btConnectBtn->setStyleSheet("QPushButton{background:#1a3a5a;color:#88ccff;border:1px solid #3a6a9a;"
                                  "border-radius:3px;font-weight:bold;}");
    m_btConnectBtn->setEnabled(false);
    connGrid->addWidget(m_btConnectBtn, 2, 0, 1, 2);

    m_disconnectBtn = new QPushButton("Disconnect");
    m_disconnectBtn->setMinimumHeight(34);
    m_disconnectBtn->setEnabled(false);
    connGrid->addWidget(m_disconnectBtn, 2, 2, 1, 2);

    // Durum
    m_connStatusLabel = new QLabel("Status: Disconnected");
    m_connStatusLabel->setStyleSheet("color: red; font-weight: bold;");
    connGrid->addWidget(m_connStatusLabel, 3, 0, 1, 4);

    layout->addWidget(connBox);

    // ELM327 Bilgileri
    QGroupBox *elmBox = new QGroupBox("ELM327 Bilgileri");
    QGridLayout *elmGrid = new QGridLayout(elmBox);

    m_elmVersionLabel = new QLabel("Version: ---");

    elmGrid->addWidget(m_elmVersionLabel, 0, 0);

    layout->addWidget(elmBox);

    // TCM Oturum
    // === TCM Diagnostik Oturum ===
    QGroupBox *tcmBox = new QGroupBox("TCM - NAG1 722.6 Transmission");
    tcmBox->setStyleSheet("QGroupBox{font-weight:bold;color:#88ccff;}");
    QVBoxLayout *tcmLayout = new QVBoxLayout(tcmBox);

    m_startSessionBtn = new QPushButton("Start TCM Session");
    m_startSessionBtn->setEnabled(false);
    m_startSessionBtn->setMinimumHeight(34);
    m_startSessionBtn->setStyleSheet(
        "QPushButton{background:#1a3a5a;color:white;border:1px solid #3a6a9a;border-radius:4px;}"
        "QPushButton:hover{background:#2a4a6a;}");
    tcmLayout->addWidget(m_startSessionBtn);

    QLabel *tcmProto = new QLabel(
        "Protocol: J1850 VPW (ATSP2)\n"
        "Address: 0x28 (TCM - NAG1 722.6)\n"
        "Header: ATSH2428xx  |  SID 0x22 ReadDataByPID");
    tcmProto->setStyleSheet("background:#1a2a3a;padding:4px;border-radius:4px;"
                            "color:#88ff88;font-family:monospace;font-size:9px;");
    tcmLayout->addWidget(tcmProto);

    layout->addWidget(tcmBox);

    // === ECU Diagnostik Oturum ===
    QGroupBox *ecuBox = new QGroupBox("ECU - Engine (OM612 2.7 CRD)");
    ecuBox->setStyleSheet("QGroupBox{font-weight:bold;color:#ffcc44;}");
    QVBoxLayout *ecuLayout = new QVBoxLayout(ecuBox);

    m_startEcuBtn = new QPushButton("Start ECU Session");
    m_startEcuBtn->setEnabled(false);
    m_startEcuBtn->setMinimumHeight(34);
    m_startEcuBtn->setStyleSheet(
        "QPushButton{background:#3a3a1a;color:white;border:1px solid #6a6a3a;border-radius:4px;}"
        "QPushButton:hover{background:#4a4a2a;}");
    ecuLayout->addWidget(m_startEcuBtn);

    QLabel *ecuProto = new QLabel(
        "Protocol: K-Line ISO 14230-4 (ATSP5)\n"
        "Address: 0x15 (Engine ECU - Bosch EDC15C2)\n"
        "Header: ATSH8115F1  |  ATWM8115F13E");
    ecuProto->setStyleSheet("background:#2a2a1a;padding:4px;border-radius:4px;"
                            "color:#ffdd88;font-family:monospace;font-size:9px;");
    ecuLayout->addWidget(ecuProto);

    layout->addWidget(ecuBox);

    // === Aktif Header Gostergesi ===
    m_activeHeaderLabel = new QLabel("Active Header: ---  (Waiting for connection)");
    m_activeHeaderLabel->setStyleSheet("background:#2a2a3a;padding:4px;border-radius:4px;"
                                       "color:#aaaaaa;font-family:monospace;font-weight:bold;");
    m_activeHeaderLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_activeHeaderLabel);

    layout->addStretch();

    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnect);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &MainWindow::onDisconnect);

    // Bluetooth butonlari
    connect(m_btScanBtn, &QPushButton::clicked, this, [this]() {
        m_btCombo->clear();
        m_btScanBtn->setText("Scanning...");
        m_btScanBtn->setEnabled(false);
        m_elm->scanBluetooth();
    });
    connect(m_btConnectBtn, &QPushButton::clicked, this, [this]() {
        if (m_btCombo->currentIndex() >= 0) {
            QString addr = m_btCombo->currentData().toString();
            m_elm->connectBluetooth(addr);
        }
    });
    connect(m_btCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_btConnectBtn->setEnabled(idx >= 0);
    });
#if HAS_BLUETOOTH
    connect(m_elm, &ELM327Connection::bluetoothDeviceFound, this,
            [this](const QString &name, const QString &addr) {
        m_btCombo->addItem(name + " [" + addr + "]", addr);
    });
    connect(m_elm, &ELM327Connection::bluetoothScanFinished, this, [this]() {
        m_btScanBtn->setText("Scan");
        m_btScanBtn->setEnabled(true);
        if (m_btCombo->count() == 0)
            statusBar()->showMessage("No BT device found");
    });
#endif

    connect(m_startSessionBtn, &QPushButton::clicked, this, [this]() {
        if (m_tcmSessionActive) {
            // Toggle off
            m_tcmSessionActive = false;
            m_startSessionBtn->setText("Start TCM Session");
            m_startSessionBtn->setStyleSheet(
                "QPushButton{background:#1a3a5a;color:white;border:1px solid #3a6a9a;border-radius:4px;}"
                "QPushButton:hover{background:#2a4a6a;}");
            statusBar()->showMessage("TCM session closed");
            updateActiveHeaderLabel();
            return;
        }
        // Start session
        m_startSessionBtn->setEnabled(false);
        statusBar()->showMessage("Starting TCM session...");
        m_tcm->startSession([this](bool success) {
            if (success) {
                m_tcmSessionActive = true;
                m_startSessionBtn->setText("TCM Active");
                m_startSessionBtn->setStyleSheet(
                    "QPushButton{background:#2a5a2a;color:#88ff88;border:1px solid #4a8a4a;border-radius:4px;font-weight:bold;}"
                    "QPushButton:hover{background:#3a6a3a;}");
                m_startSessionBtn->setEnabled(true);
                m_startEcuBtn->setEnabled(true);
                m_readDtcBtn->setEnabled(true);
                m_clearDtcBtn->setEnabled(true);
                m_startLiveBtn->setEnabled(true);
                m_readIOBtn->setEnabled(true);
                statusBar()->showMessage("TCM diagnostic session active");
            } else {
                m_startSessionBtn->setEnabled(true);
                statusBar()->showMessage("TCM session failed!");
            }
            updateActiveHeaderLabel();
        });
    });

    connect(m_startEcuBtn, &QPushButton::clicked, this, [this]() {
        if (m_ecuSessionActive) {
            // Toggle off
            m_ecuSessionActive = false;
            m_startEcuBtn->setText("Start ECU Session");
            m_startEcuBtn->setStyleSheet(
                "QPushButton{background:#3a3a1a;color:white;border:1px solid #6a6a3a;border-radius:4px;}"
                "QPushButton:hover{background:#4a4a2a;}");
            statusBar()->showMessage("ECU session closed");
            updateActiveHeaderLabel();
            return;
        }
        // Start ECU session - K-Line init
        m_startEcuBtn->setEnabled(false);
        statusBar()->showMessage("Starting ECU session (K-Line)...");
        m_tcm->switchToModule(WJDiagnostics::Module::MotorECU, [this](bool success) {
            if (success) {
                m_ecuSessionActive = true;
                m_startEcuBtn->setText("ECU Active");
                m_startEcuBtn->setStyleSheet(
                    "QPushButton{background:#2a5a2a;color:#88ff88;border:1px solid #4a8a4a;border-radius:4px;font-weight:bold;}"
                    "QPushButton:hover{background:#3a6a3a;}");
                statusBar()->showMessage("ECU session active - K-Line ready");
            } else {
                statusBar()->showMessage("ECU session failed - K-Line init error");
            }
            m_startEcuBtn->setEnabled(true);
            updateActiveHeaderLabel();
        });
    });

    scroll->setWidget(w);
    return scroll;
}

QWidget* MainWindow::createDTCTab()
{
    QWidget *w = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(w);

    // === ECU / TCM kaynak secici ===
    QHBoxLayout *sourceLayout = new QHBoxLayout();
    QLabel *srcLabel = new QLabel("Source:");
    srcLabel->setStyleSheet("font-weight:bold;");
    sourceLayout->addWidget(srcLabel);

    m_dtcTcmBtn = new QPushButton("TCM (Trans)");
    m_dtcTcmBtn->setCheckable(true);
    m_dtcTcmBtn->setChecked(true);
    m_dtcTcmBtn->setMinimumHeight(34);
    m_dtcTcmBtn->setStyleSheet(
        "QPushButton{background:#1a3a5a;color:white;border:1px solid #3a6a9a;border-radius:4px;padding:4px 12px;}"
        "QPushButton:checked{background:#2a5a2a;color:#88ff88;border:1px solid #4a8a4a;font-weight:bold;}");

    m_dtcEcuBtn = new QPushButton("ECU (Engine)");
    m_dtcEcuBtn->setCheckable(true);
    m_dtcEcuBtn->setChecked(false);
    m_dtcEcuBtn->setMinimumHeight(34);
    m_dtcEcuBtn->setStyleSheet(
        "QPushButton{background:#3a3a1a;color:white;border:1px solid #6a6a3a;border-radius:4px;padding:4px 12px;}"
        "QPushButton:checked{background:#2a5a2a;color:#88ff88;border:1px solid #4a8a4a;font-weight:bold;}");

    m_dtcAbsBtn = new QPushButton("ABS");
    m_dtcAbsBtn->setCheckable(true);
    m_dtcAbsBtn->setChecked(false);
    m_dtcAbsBtn->setMinimumHeight(34);
    m_dtcAbsBtn->setStyleSheet(
        "QPushButton{background:#1a2a3a;color:white;border:1px solid #3a5a7a;border-radius:4px;padding:4px 12px;}"
        "QPushButton:checked{background:#2a5a2a;color:#88ff88;border:1px solid #4a8a4a;font-weight:bold;}");

    m_dtcAirbagBtn = new QPushButton("Airbag");
    m_dtcAirbagBtn->setCheckable(true);
    m_dtcAirbagBtn->setChecked(false);
    m_dtcAirbagBtn->setMinimumHeight(34);
    m_dtcAirbagBtn->setStyleSheet(
        "QPushButton{background:#3a2a1a;color:white;border:1px solid #6a4a2a;border-radius:4px;padding:4px 12px;}"
        "QPushButton:checked{background:#2a5a2a;color:#88ff88;border:1px solid #4a8a4a;font-weight:bold;}");

    sourceLayout->addWidget(m_dtcTcmBtn);
    sourceLayout->addWidget(m_dtcEcuBtn);
    sourceLayout->addWidget(m_dtcAbsBtn);
    sourceLayout->addWidget(m_dtcAirbagBtn);
    sourceLayout->addStretch();
    layout->addLayout(sourceLayout);

    // Toggle logic: radio-button style
    auto selectDtcSource = [this](int src) {
        m_dtcSourceIdx = src;
        m_dtcTcmBtn->setChecked(src == 0);
        m_dtcEcuBtn->setChecked(src == 1);
        m_dtcAbsBtn->setChecked(src == 2);
        m_dtcAirbagBtn->setChecked(src == 3);
        m_dtcTable->setRowCount(0);
        static const char* names[] = {"TCM","ECU","ABS","Airbag"};
        m_dtcCountLabel->setText(QString("Source: %1 - 0 fault codes").arg(names[src]));
    };
    connect(m_dtcTcmBtn, &QPushButton::clicked, this, [=]() { selectDtcSource(0); });
    connect(m_dtcEcuBtn, &QPushButton::clicked, this, [=]() { selectDtcSource(1); });
    connect(m_dtcAbsBtn, &QPushButton::clicked, this, [=]() { selectDtcSource(2); });
    connect(m_dtcAirbagBtn, &QPushButton::clicked, this, [=]() { selectDtcSource(3); });

    // === Butonlar ===
    QHBoxLayout *btnLayout = new QHBoxLayout();

    m_readDtcBtn  = new QPushButton("Read Fault Codes");
    m_clearDtcBtn = new QPushButton("Clear Fault Codes");
    m_dtcCountLabel = new QLabel("Source: TCM - 0 fault codes");

    m_readDtcBtn->setMinimumHeight(34);
    m_clearDtcBtn->setMinimumHeight(34);
    m_readDtcBtn->setEnabled(false);
    m_clearDtcBtn->setEnabled(false);

    btnLayout->addWidget(m_readDtcBtn);
    btnLayout->addWidget(m_clearDtcBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_dtcCountLabel);

    layout->addLayout(btnLayout);

    // DTC tablosu - 6 sutun (kaynak eklendi)
    m_dtcTable = new QTableWidget(0, 5);
    m_dtcTable->setHorizontalHeaderLabels({
        "Source", "Code", "Description", "State", "Status"
    });
    m_dtcTable->horizontalHeader()->setStretchLastSection(true);
    m_dtcTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_dtcTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dtcTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dtcTable->setAlternatingRowColors(true);
    QScroller::grabGesture(m_dtcTable->viewport(), QScroller::LeftMouseButtonGesture);

    layout->addWidget(m_dtcTable);

    // P2602 notu
    QLabel *p2602Note = new QLabel(
        "NOTE: P2602 (Solenoid Voltage) is usually caused by weak battery or "
        "transmission 13-pin connector contact issue.");
    p2602Note->setWordWrap(true);
    p2602Note->setStyleSheet("background:#3a3a20;padding:4px;border-radius:4px;color:#ffcc44;");
    layout->addWidget(p2602Note);

    connect(m_readDtcBtn, &QPushButton::clicked, this, &MainWindow::onReadDTCs);
    connect(m_clearDtcBtn, &QPushButton::clicked, this, &MainWindow::onClearDTCs);

    return w;
}

QWidget* MainWindow::createLiveDataTab()
{
    QWidget *w = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(w);

    QHBoxLayout *btnLayout = new QHBoxLayout();

    m_startLiveBtn = new QPushButton("Start");
    m_stopLiveBtn  = new QPushButton("Stop");
    m_logBtn       = new QPushButton("Copy CSV");

    m_startLiveBtn->setMinimumHeight(34);
    m_stopLiveBtn->setMinimumHeight(34);
    m_logBtn->setMinimumHeight(34);
    m_startLiveBtn->setEnabled(false);
    m_stopLiveBtn->setEnabled(false);

    // Mod secici
    m_modeCombo = new QComboBox();
    m_modeCombo->addItem("TCM", (int)LiveDataManager::TCM_ONLY);
    m_modeCombo->addItem("ECU", (int)LiveDataManager::ECU_ONLY);
    m_modeCombo->addItem("TCM+ECU", (int)LiveDataManager::DUAL);
    m_modeCombo->setCurrentIndex(2); // default DUAL
    m_modeCombo->setMinimumHeight(34);
    m_modeCombo->setStyleSheet("QComboBox{background:#1a2a3a;color:#88ff88;border:1px solid #3a6a9a;"
                               "border-radius:3px;padding:2px 6px;font-weight:bold;}");
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_liveData->setMode(static_cast<LiveDataManager::Mode>(m_modeCombo->itemData(idx).toInt()));
    });
    m_liveData->setMode(LiveDataManager::DUAL);

    btnLayout->addWidget(m_modeCombo);
    btnLayout->addWidget(m_startLiveBtn);
    btnLayout->addWidget(m_stopLiveBtn);
    btnLayout->addWidget(m_logBtn);

    layout->addLayout(btnLayout);

    // Live data tablosu
    m_liveTable = new QTableWidget(0, 5);
    m_liveTable->setHorizontalHeaderLabels({
        "Select", "Parametre", "Value", "Birim", "Local ID"
    });
    m_liveTable->horizontalHeader()->setStretchLastSection(false);
    m_liveTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_liveTable->setAlternatingRowColors(true);
    QScroller::grabGesture(m_liveTable->viewport(), QScroller::LeftMouseButtonGesture);

    // Parametreleri tabloya ekle
    auto params = m_tcm->liveDataParams();
    m_liveTable->setRowCount(params.size());

    for (int i = 0; i < params.size(); ++i) {
        const auto &p = params[i];

        // Checkbox
        QTableWidgetItem *checkItem = new QTableWidgetItem();
        checkItem->setCheckState(Qt::Checked);
        m_liveTable->setItem(i, 0, checkItem);

        // İsim
        m_liveTable->setItem(i, 1, new QTableWidgetItem(p.name));

        // Value
        QTableWidgetItem *valItem = new QTableWidgetItem("---");
        valItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QFont valFont;
        valFont.setFamily("Consolas");
        valFont.setPointSize(11);
        valItem->setFont(valFont);
        m_liveTable->setItem(i, 2, valItem);

        // Birim
        m_liveTable->setItem(i, 3, new QTableWidgetItem(p.unit));

        // Local ID
        m_liveTable->setItem(i, 4, new QTableWidgetItem(
            QString("0x%1").arg(p.localID, 2, 16, QChar('0')).toUpper()));

        // P2602 ile ilgili satırı vurgula
        if (p.localID == 0x09) { // Selenoid Besleme Voltajı
            for (int col = 0; col < 5; ++col) {
                if (m_liveTable->item(i, col)) {
                    m_liveTable->item(i, col)->setBackground(QColor(60, 60, 20));
                }
            }
        }
    }

    layout->addWidget(m_liveTable);

    connect(m_startLiveBtn, &QPushButton::clicked, this, &MainWindow::onStartLiveData);
    connect(m_stopLiveBtn, &QPushButton::clicked, this, &MainWindow::onStopLiveData);
    connect(m_logBtn, &QPushButton::clicked, this, [this]() {
        if (m_liveData->isLogging()) {
            m_liveData->stopLogging();
            m_logBtn->setText("Copy CSV");
            // Copy last log to clipboard
            if (!m_lastLogPath.isEmpty()) {
                QFile f(m_lastLogPath);
                if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QGuiApplication::clipboard()->setText(QString::fromUtf8(f.readAll()));
                    f.close();
                    statusBar()->showMessage("CSV copied to clipboard");
                }
            }
        } else {
            // Save to temp file (will be copied to clipboard later)
            QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
            if (dir.isEmpty())
                dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            QDir().mkpath(dir);
            m_lastLogPath = dir + "/wjdiag_"
                + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".csv";
            m_liveData->startLogging(m_lastLogPath);
            m_logBtn->setText("Stop+Copy");
            statusBar()->showMessage("CSV kaydediliyor...");
        }
    });

    return w;
}

QWidget* MainWindow::createIOTab()
{
    QWidget *w = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(w);

    m_readIOBtn = new QPushButton("Read I/O States");
    m_readIOBtn->setMinimumHeight(34);
    m_readIOBtn->setEnabled(false);
    layout->addWidget(m_readIOBtn);

    m_ioTable = new QTableWidget(0, 4);
    m_ioTable->setHorizontalHeaderLabels({
        "I/O", "Description", "State", "Detay"
    });
    m_ioTable->horizontalHeader()->setStretchLastSection(true);
    m_ioTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_ioTable->setAlternatingRowColors(true);
    m_ioTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    QScroller::grabGesture(m_ioTable->viewport(), QScroller::LeftMouseButtonGesture);

    // I/O tanımlarını yükle
    auto ioDefs = m_tcm->ioDefinitions();
    m_ioTable->setRowCount(ioDefs.size());

    for (int i = 0; i < ioDefs.size(); ++i) {
        const auto &io = ioDefs[i];
        m_ioTable->setItem(i, 0, new QTableWidgetItem(io.name));
        m_ioTable->setItem(i, 1, new QTableWidgetItem(io.description));
        m_ioTable->setItem(i, 2, new QTableWidgetItem("---"));
        m_ioTable->setItem(i, 3, new QTableWidgetItem(
            QString("ID: 0x%1").arg(io.localID, 2, 16, QChar('0'))));
    }

    layout->addWidget(m_ioTable);

    QLabel *ioWarning = new QLabel(
        "WARNING: Solenoid actuation test must only be performed with vehicle stationary and transmission in P/N!\n"
        "Incorrect solenoid activation may damage the transmission."
    );
    ioWarning->setWordWrap(true);
    ioWarning->setStyleSheet("background: #4a2020; padding: 8px; border-radius: 4px; "
                             "color: #ff6666; font-weight: bold;");
    layout->addWidget(ioWarning);

    connect(m_readIOBtn, &QPushButton::clicked, this, &MainWindow::onReadIO);

    return w;
}

QWidget* MainWindow::createLogTab()
{
    QWidget *w = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(w);

    m_logText = new QTextEdit();
    m_logText->setReadOnly(true);
    m_logText->setFont(QFont("Consolas", 9));
    m_logText->setStyleSheet("background: #1a1a2e; color: #00ff00;");

    layout->addWidget(m_logText);

    // --- Log butonlari: 2 satirlik grid ---
    QGridLayout *logGrid = new QGridLayout();
    logGrid->setSpacing(4);

    // Row 0: Clear | Copy Log | Raw Data Read
    QPushButton *clearLogBtn = new QPushButton("Clear");
    clearLogBtn->setStyleSheet("padding:4px 6px;");
    connect(clearLogBtn, &QPushButton::clicked, m_logText, &QTextEdit::clear);
    logGrid->addWidget(clearLogBtn, 0, 0);

    QPushButton *saveLogBtn = new QPushButton("Copy Log");
    saveLogBtn->setStyleSheet("background:#2a3a2a; color:#88ff88; font-weight:bold; padding:4px 6px;");
    connect(saveLogBtn, &QPushButton::clicked, this, [this]() {
        QString text = m_logText->toPlainText();
        if (text.isEmpty()) {
            statusBar()->showMessage("Log empty - nothing to copy");
            return;
        }
        QGuiApplication::clipboard()->setText(text);
        statusBar()->showMessage(QString("Log copied (%1 lines) - paste to WhatsApp/Notes")
            .arg(text.count('\n') + 1));
    });
    logGrid->addWidget(saveLogBtn, 0, 1);

    m_rawDumpBtn = new QPushButton("Raw Data");
    m_rawDumpBtn->setStyleSheet("background:#2a4858; color:#00ffcc; font-weight:bold; padding:4px 6px;");
    connect(m_rawDumpBtn, &QPushButton::clicked, this, &MainWindow::onRawBusDump);
    logGrid->addWidget(m_rawDumpBtn, 0, 2);

    // Satir 1: Komut input + Gonder
    m_rawCmdEdit = new QLineEdit();
    m_rawCmdEdit->setPlaceholderText("21 01 veya ATRV");
    m_rawCmdEdit->setStyleSheet("background:#1a1a2e; color:#00ff00; border:1px solid #444; padding:3px;");
    logGrid->addWidget(m_rawCmdEdit, 1, 0, 1, 2);

    m_rawSendBtn = new QPushButton("Send");
    m_rawSendBtn->setStyleSheet("background:#4a2858; color:#ff88ff; font-weight:bold; padding:4px 6px;");
    connect(m_rawSendBtn, &QPushButton::clicked, this, &MainWindow::onRawSendCustom);
    connect(m_rawCmdEdit, &QLineEdit::returnPressed, this, &MainWindow::onRawSendCustom);
    logGrid->addWidget(m_rawSendBtn, 1, 2);

    layout->addLayout(logGrid);

    return w;
}

// --- SLOT İmplementasyonları ---

void MainWindow::onConnect()
{
    m_connectBtn->setEnabled(false);
    m_elm->connectToDevice(m_hostEdit->text(),
                            static_cast<quint16>(m_portSpin->value()));
}

void MainWindow::onDisconnect()
{
    m_liveData->stopPolling();
    m_elm->disconnect();
}

void MainWindow::onConnectionStateChanged(ELM327Connection::ConnectionState state)
{
    switch (state) {
    case ELM327Connection::ConnectionState::Disconnected:
        m_connStatusLabel->setText("Status: Disconnected");
        m_connStatusLabel->setStyleSheet("color: red; font-weight: bold;");
        m_connectBtn->setEnabled(true);
        m_disconnectBtn->setEnabled(false);
        m_startSessionBtn->setEnabled(false);
        m_readDtcBtn->setEnabled(false);
        m_clearDtcBtn->setEnabled(false);
        m_startLiveBtn->setEnabled(false);
        m_readIOBtn->setEnabled(false);
        if (m_batteryTimer) m_batteryTimer->stop();
        statusBar()->showMessage("Disconnected");
        break;

    case ELM327Connection::ConnectionState::Connecting:
        m_connStatusLabel->setText("Status: Connecting...");
        m_connStatusLabel->setStyleSheet("color: orange; font-weight: bold;");
        statusBar()->showMessage("Connecting...");
        break;

    case ELM327Connection::ConnectionState::Initializing:
        m_connStatusLabel->setText("Status: Initializing ELM327...");
        m_connStatusLabel->setStyleSheet("color: yellow; font-weight: bold;");
        statusBar()->showMessage("Initializing ELM327...");
        break;

    case ELM327Connection::ConnectionState::Ready:
        m_connStatusLabel->setText("Status: Ready");
        m_connStatusLabel->setStyleSheet("color: lime; font-weight: bold;");
        m_connectBtn->setEnabled(false);
        m_disconnectBtn->setEnabled(true);
        m_startSessionBtn->setEnabled(true);
        m_elmVersionLabel->setText("Version: " + m_elm->elmVersion());

        statusBar()->showMessage("ELM327 ready - Start a diagnostic session");
        break;

    case ELM327Connection::ConnectionState::Error:
        m_connStatusLabel->setText("Status: ERROR!");
        m_connStatusLabel->setStyleSheet("color: red; font-weight: bold;");
        m_connectBtn->setEnabled(true);
        m_disconnectBtn->setEnabled(true);
        statusBar()->showMessage("Connection error!");
        break;

    default:
        break;
    }
}

void MainWindow::onReadDTCs()
{
    m_readDtcBtn->setEnabled(false);
    static const char* names[] = {"TCM","ECU","ABS","Airbag"};
    QString src = names[m_dtcSourceIdx];
    statusBar()->showMessage("Reading fault codes: " + src + "...");

    WJDiagnostics::Module mod;
    switch (m_dtcSourceIdx) {
    case 1:  mod = WJDiagnostics::Module::MotorECU; break;
    case 2:  mod = WJDiagnostics::Module::ABS; break;
    case 3:  mod = WJDiagnostics::Module::Airbag; break;
    default: mod = WJDiagnostics::Module::TCM; break;
    }

    m_tcm->readDTCs(mod, [this, src](const QList<WJDiagnostics::DTCEntry> &dtcs) {
        m_dtcTable->setRowCount(0);
        for (const auto &d : dtcs) {
            int row = m_dtcTable->rowCount();
            m_dtcTable->insertRow(row);
            m_dtcTable->setItem(row, 0, new QTableWidgetItem(src));
            m_dtcTable->setItem(row, 1, new QTableWidgetItem(d.code));
            m_dtcTable->setItem(row, 2, new QTableWidgetItem(d.description));
            m_dtcTable->setItem(row, 3, new QTableWidgetItem(d.isActive ? "Active" : "Stored"));
            m_dtcTable->setItem(row, 4, new QTableWidgetItem(
                QString("0x%1").arg(d.status, 2, 16, QChar('0')).toUpper()));

            if (d.isActive) {
                for (int col = 0; col < 5; ++col) {
                    m_dtcTable->item(row, col)->setBackground(QColor(80, 20, 20));
                    m_dtcTable->item(row, col)->setForeground(QColor(255, 100, 100));
                }
            }
        }
        m_readDtcBtn->setEnabled(true);
        m_dtcCountLabel->setText(QString("Source: %1 - %2 fault codes").arg(src).arg(dtcs.size()));
        statusBar()->showMessage(QString("%1 fault codes read (%2)").arg(dtcs.size()).arg(src));
    });
}

void MainWindow::onClearDTCs()
{
    static const char* names[] = {"TCM","ECU","ABS","Airbag"};
    QString src = names[m_dtcSourceIdx];

    QString warning = (m_dtcSourceIdx == 3)
        ? QString("Are you sure you want to clear %1 fault codes?\n\n"
                  "WARNING: Only clear Airbag DTCs after faults have been repaired!").arg(src)
        : QString("Are you sure you want to clear %1 fault codes?\n\n"
                  "NOTE: Active faults will reappear if the problem persists.").arg(src);

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Clear Fault Codes", warning,
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        m_clearDtcBtn->setEnabled(false);
        statusBar()->showMessage("Clearing " + src + " fault codes...");

        WJDiagnostics::Module mod;
        switch (m_dtcSourceIdx) {
        case 1:  mod = WJDiagnostics::Module::MotorECU; break;
        case 2:  mod = WJDiagnostics::Module::ABS; break;
        case 3:  mod = WJDiagnostics::Module::Airbag; break;
        default: mod = WJDiagnostics::Module::TCM; break;
        }

        m_tcm->clearDTCs(mod, [this, src](bool success) {
            m_clearDtcBtn->setEnabled(true);
            if (success) {
                m_dtcTable->setRowCount(0);
                m_dtcCountLabel->setText(QString("Source: %1 - 0 fault codes").arg(src));
                statusBar()->showMessage(src + " fault codes cleared");
            } else {
                statusBar()->showMessage(src + " fault codes could not be cleared!");
            }
        });
    }
}

void MainWindow::onStartLiveData()
{
    // Selectili parametreleri topla
    QList<uint8_t> selected;
    auto params = m_tcm->liveDataParams();

    for (int i = 0; i < m_liveTable->rowCount() && i < params.size(); ++i) {
        if (m_liveTable->item(i, 0)->checkState() == Qt::Checked) {
            selected.append(params[i].localID);
        }
    }

    if (selected.isEmpty()) {
        QMessageBox::warning(this, "Parametre Selectimi",
                             "You must select at least one parameter!");
        return;
    }

    m_liveData->setSelectedParams(selected);
    m_liveData->startPolling(300); // 300ms aralık

    m_startLiveBtn->setEnabled(false);
    m_stopLiveBtn->setEnabled(true);
    statusBar()->showMessage("Live data stream started...");
}

void MainWindow::onStopLiveData()
{
    m_liveData->stopPolling();
    m_startLiveBtn->setEnabled(true);
    m_stopLiveBtn->setEnabled(false);
    statusBar()->showMessage("Live data stopped");
}

void MainWindow::onLiveDataUpdated(const QMap<uint8_t, double> &values)
{
    auto params = m_tcm->liveDataParams();

    for (int i = 0; i < params.size(); ++i) {
        if (values.contains(params[i].localID)) {
            double val = values[params[i].localID];
            QString valStr;

            if (params[i].unit == "rpm" || params[i].unit == "km/h") {
                valStr = QString::number(val, 'f', 0);
            } else if (params[i].unit == "°C" || params[i].unit == "V" ||
                       params[i].unit == "bar") {
                valStr = QString::number(val, 'f', 1);
            } else if (params[i].unit == "%") {
                valStr = QString::number(val, 'f', 1);
            } else {
                valStr = QString::number(val, 'f', 0);
            }

            if (m_liveTable->item(i, 2)) {
                m_liveTable->item(i, 2)->setText(valStr);
            }

            // Selenoid voltajı düşükse uyarı rengi
            if (params[i].localID == 0x09) {
                QColor bg = (val < 9.0) ? QColor(80, 20, 0) :
                            (val < 11.0) ? QColor(80, 60, 0) :
                                           QColor(20, 60, 20);
                for (int col = 0; col < 5; ++col) {
                    if (m_liveTable->item(i, col))
                        m_liveTable->item(i, col)->setBackground(bg);
                }
            }
        }
    }

    // Update dashboard gauges
    updateDashboardFromLiveData(values);
}

void MainWindow::onFullStatusUpdated(const TCMDiagnostics::TCMStatus &status)
{
    updateStatusLabels(status);
    // Battery voltage from ATRV (updated in TCM cycle too)
    if (status.batteryVoltage > 0) {
        m_dashBatVoltVal->setText(QString::number(status.batteryVoltage, 'f', 1));
        setGaugeColor(m_dashBatVoltVal,
            status.batteryVoltage < 11.5 ? "#ff4444" : status.batteryVoltage < 12.5 ? "#ffaa00" : "#00ff88");
    }
}

void MainWindow::onECUDataUpdated(const TCMDiagnostics::ECUStatus &ecu)
{
    // Motor RPM
    m_dashMotRpmVal->setText(QString::number(ecu.rpm, 'f', 0));
    setGaugeColor(m_dashMotRpmVal,
        ecu.rpm > 4500 ? "#ff4444" : ecu.rpm > 3500 ? "#ffaa00" : "#00ff88");

    // Boost Pressure (mbar)
    m_dashMotBoostVal->setText(QString::number(ecu.boostPressure, 'f', 0));
    setGaugeColor(m_dashMotBoostVal,
        ecu.boostPressure > 2000 ? "#ff4444" : ecu.boostPressure > 1500 ? "#ffaa00" : "#00ff88");

    // MAF
    m_dashMotMafVal->setText(QString::number(ecu.mafActual, 'f', 0));

    // Rail Pressure (bar)
    m_dashMotRailVal->setText(QString::number(ecu.railActual, 'f', 0));
    setGaugeColor(m_dashMotRailVal,
        ecu.railActual > 1400 ? "#ff4444" : ecu.railActual > 1200 ? "#ffaa00" : "#00ff88");

    // Su sicakligi (Motor ECU'dan gelen coolant)
    m_dashMotCoolVal->setText(QString::number(ecu.coolantTemp, 'f', 0));
    setGaugeColor(m_dashMotCoolVal,
        ecu.coolantTemp > 105 ? "#ff4444" : ecu.coolantTemp > 95 ? "#ffaa00" : "#00ff88");

    // Battery voltage (ECU'dan)
    if (ecu.batteryVoltage > 0) {
        m_dashBatVoltVal->setText(QString::number(ecu.batteryVoltage, 'f', 1));
        setGaugeColor(m_dashBatVoltVal,
            ecu.batteryVoltage < 11.5 ? "#ff4444" : ecu.batteryVoltage < 12.5 ? "#ffaa00" : "#00ff88");
    }
}

void MainWindow::onReadIO()
{
    m_readIOBtn->setEnabled(false);
    statusBar()->showMessage("Reading I/O states...");

    m_tcm->readIOStates([this](const QList<TCMDiagnostics::IOState> &states) {
        for (int i = 0; i < states.size() && i < m_ioTable->rowCount(); ++i) {
            QString statusStr = states[i].isActive ? "ACTIVE" : "Off";
            m_ioTable->item(i, 2)->setText(statusStr);

            if (states[i].isActive) {
                m_ioTable->item(i, 2)->setBackground(QColor(20, 80, 20));
            } else {
                m_ioTable->item(i, 2)->setBackground(QColor(40, 40, 40));
            }
        }

        m_readIOBtn->setEnabled(true);
        statusBar()->showMessage("I/O states updated");
    });
}

void MainWindow::onLogMessage(const QString &msg)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    m_logText->append(QString("[%1] %2").arg(timestamp, msg));
}

void MainWindow::updateStatusLabels(const TCMDiagnostics::TCMStatus &st)
{
    m_dashGearVal->setText(gearToString(st.currentGear));
    m_dashRpmVal->setText(QString::number(st.turbineRPM,'f',0));
    m_dashSpeedVal->setText(QString::number(st.vehicleSpeed,'f',0));
    m_dashSolVoltVal->setText(QString::number(st.solenoidSupply,'f',1));
    m_dashCoolantVal->setText(QString::number(st.transTemp,'f',0));      // Trans temp
    m_dashLimpVal->setText(st.limpMode ? "ACTIVE!" : "Normal");
    // Motor su sicakligi: coolantTemp alaninda ECU'dan gelir
    if (st.coolantTemp > 0) {
        m_dashMotCoolVal->setText(QString::number(st.coolantTemp,'f',0));
        setGaugeColor(m_dashMotCoolVal,
            st.coolantTemp>105?"#ff4444":st.coolantTemp>95?"#ffaa00":"#00ff88");
    }
    setGaugeColor(m_dashLimpVal, st.limpMode ? "#ff4444" : "#00ff88");
    setGaugeColor(m_dashSolVoltVal,
        st.solenoidSupply<9.0?"#ff4444":st.solenoidSupply<11.0?"#ffaa00":"#00ff88");
    setGaugeColor(m_dashCoolantVal,
        st.transTemp>105?"#ff4444":st.transTemp>95?"#ffaa00":"#00ff88");
}

void MainWindow::updateActiveHeaderLabel()
{
    QString text;
    QString style;
    if (m_tcmSessionActive && m_ecuSessionActive) {
        text = "Active: TCM (J1850) + ECU (K-Line)  |  Dual Mode";
        style = "background:#2a3a2a;padding:4px;border-radius:4px;"
                "color:#88ffaa;font-family:monospace;font-weight:bold;";
    } else if (m_tcmSessionActive) {
        text = "Active: TCM  |  J1850 VPW  |  ATSH2428xx  |  NAG1 722.6";
        style = "background:#1a3a5a;padding:4px;border-radius:4px;"
                "color:#88ccff;font-family:monospace;font-weight:bold;";
    } else if (m_ecuSessionActive) {
        text = "Active: ECU  |  ATSH 81 15 F1  |  Engine OM612 (EDC15C2)";
        style = "background:#3a3a1a;padding:4px;border-radius:4px;"
                "color:#ffcc44;font-family:monospace;font-weight:bold;";
    } else {
        text = "Active Header: ---  (No session started)";
        style = "background:#2a2a3a;padding:4px;border-radius:4px;"
                "color:#aaaaaa;font-family:monospace;font-weight:bold;";
    }
    m_activeHeaderLabel->setText(text);
    m_activeHeaderLabel->setStyleSheet(style);
}

QString MainWindow::gearToString(TCMDiagnostics::Gear gear)
{
    switch (gear) {
    case TCMDiagnostics::Gear::Park:    return "P";
    case TCMDiagnostics::Gear::Reverse: return "R";
    case TCMDiagnostics::Gear::Neutral: return "N";
    case TCMDiagnostics::Gear::Drive1:  return "D1";
    case TCMDiagnostics::Gear::Drive2:  return "D2";
    case TCMDiagnostics::Gear::Drive3:  return "D3";
    case TCMDiagnostics::Gear::Drive4:  return "D4";
    case TCMDiagnostics::Gear::Drive5:  return "D5";
    case TCMDiagnostics::Gear::Limp:    return "LIMP!";
    default: return "?";
    }
}

// --- Ham Bus Veri Dump ---
void MainWindow::onRawBusDump()
{
    m_rawDumpBtn->setEnabled(false);
    m_rawDumpBtn->setText("Reading...");

    auto logHex = [this](const QString &prefix, const QString &cmd, const QByteArray &resp) {
        QString hex;
        for (int i = 0; i < resp.size(); i++)
            hex += QString("%1 ").arg(static_cast<uint8_t>(resp[i]), 2, 16, QChar('0')).toUpper();
        QString ts = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
        m_logText->append(QString("<font color='%1'>[%2] TX: %3</font>").arg(prefix, ts, cmd));
        if (resp.isEmpty())
            m_logText->append(QString("<font color='red'>       RX: (bos yanit)</font>"));
        else
            m_logText->append(QString("<font color='#88ff88'>       RX [%1 byte]: %2</font>").arg(resp.size()).arg(hex.trimmed()));
    };

    // Phase 1: Motor ECU (K-Line 0x15) - Bosch EDC15C2 bloklari
    QList<uint8_t> ecuIDs = {0x12, 0x28, 0x62, 0xB0, 0xB1, 0xB2};
    // Phase 2: TCM (J1850 VPW 0x28) - SID 0x22 ReadDataByID
    QList<uint8_t> tcmPIDs = {0x01, 0x02, 0x10, 0x11, 0x14, 0x15, 0x16, 0x17, 0x20};
    // Phase 3: ABS (J1850 VPW 0x40) - SID 0x20 ReadDataByAddress
    QList<uint8_t> absPIDs = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
    // Phase 4: Airbag (J1850 VPW 0x60) - SID 0x28 ReadMemory
    QList<uint8_t> airbagPIDs = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};

    m_logText->append("<font color='white'>========== RAW BUS DUMP STARTED ==========</font>");
    m_logText->append("<font color='#ffcc00'>--- Motor ECU (0x15) K-Line ---</font>");

    // Phase 1: Motor ECU via K-Line
    m_tcm->rawBusDump(WJDiagnostics::Module::MotorECU, ecuIDs,
        [this, logHex](uint8_t lid, const QByteArray &data) {
            QString cmd = QString("21 %1").arg(lid, 2, 16, QChar('0')).toUpper();
            logHex("#ffcc00", cmd, data);
        },
        [this, logHex, tcmPIDs, absPIDs, airbagPIDs]() {
            // Phase 2: TCM via J1850
            m_logText->append("<font color='#00cccc'>--- TCM (0x28) J1850 VPW ATSH242810 ---</font>");
            m_tcm->rawBusDump(WJDiagnostics::Module::TCM, tcmPIDs,
                [this, logHex](uint8_t pid, const QByteArray &data) {
                    QString cmd = QString("22 %1").arg(pid, 2, 16, QChar('0')).toUpper();
                    logHex("#00cccc", cmd, data);
                },
                [this, logHex, absPIDs, airbagPIDs]() {
                    // Phase 3: ABS via J1850
                    m_logText->append("<font color='#ff88ff'>--- ABS (0x40) J1850 VPW ATSH244022 ---</font>");
                    m_tcm->rawBusDump(WJDiagnostics::Module::ABS, absPIDs,
                        [this, logHex](uint8_t pid, const QByteArray &data) {
                            QString cmd = QString("20 %1").arg(pid, 2, 16, QChar('0')).toUpper();
                            logHex("#ff88ff", cmd, data);
                        },
                        [this, logHex, airbagPIDs]() {
                            // Phase 4: Airbag via J1850
                            m_logText->append("<font color='#ffaa44'>--- Airbag (0x60) J1850 VPW ATSH246022 ---</font>");
                            m_tcm->rawBusDump(WJDiagnostics::Module::Airbag, airbagPIDs,
                                [this, logHex](uint8_t pid, const QByteArray &data) {
                                    QString cmd = QString("28 %1").arg(pid, 2, 16, QChar('0')).toUpper();
                                    logHex("#ffaa44", cmd, data);
                                },
                                [this]() {
                                    m_logText->append("<font color='white'>========== RAW BUS DUMP COMPLETED ==========</font>");
                                    m_rawDumpBtn->setEnabled(true);
                                    m_rawDumpBtn->setText("Raw Data Read");
                                });
                        });
                });
        });
}

void MainWindow::onRawSendCustom()
{
    QString cmd = m_rawCmdEdit->text().trimmed();
    if (cmd.isEmpty()) return;
    m_rawCmdEdit->clear();
    QString ts = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    if (cmd.startsWith("AT", Qt::CaseInsensitive)) {
        m_logText->append(QString("<font color='#ff88ff'>[%1] TX (AT): %2</font>").arg(ts, cmd));
        m_elm->sendCommand(cmd, [this](const QString &resp) {
            m_logText->append(QString("<font color='#88ff88'>       RX: %1</font>").arg(resp));
        });
    } else {
        QString hexClean = cmd.remove(' ');
        m_logText->append(QString("<font color='#ff88ff'>[%1] TX (KWP): %2</font>").arg(ts, cmd));
        m_elm->sendCommand(hexClean, [this](const QString &resp) {
            m_logText->append(QString("<font color='#88ff88'>       RX: %1</font>").arg(resp));
        });
    }
}
