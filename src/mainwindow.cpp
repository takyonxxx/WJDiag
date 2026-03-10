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
#include <QSet>
#include <QScroller>
#include <QScrollBar>
#include <QDir>
#include <QFrame>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
#endif

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
#include <QPermissions>
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
    // KWP2000Handler logMessage is already forwarded by WJDiagnostics

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
    appFont.setPointSize(13);
    QApplication::setFont(appFont);
#else
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(4);
#endif

    // === DASHBOARD PANEL (11 gauge) ===
    mainLayout->addWidget(createDashboardPanel());
    m_throttleBar = new QProgressBar();
    m_throttleBar->setRange(0, 100);
    m_throttleBar->setVisible(false);
    mainLayout->addWidget(m_throttleBar);

    // === TABLAR ===
    m_tabs = new QTabWidget();
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    m_tabs->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #1a3050; border-top: none; }"
        "QTabBar::tab {"
        "background: #0e1828; color: #6090a8; padding: 10px 14px;"
        "border: 1px solid #1a3050; border-bottom: none; border-radius: 4px 4px 0 0;"
        "font-size: 14px; min-width: 40px; font-weight: 500;"
        "}"
        "QTabBar::tab:selected {"
        "background: #122840; color: #00d4b4; font-weight: bold; border-bottom: 2px solid #00d4b4;"
        "}"
        "QScrollBar:vertical { width: 0px; background: transparent; }"
        "QScrollBar:horizontal { height: 0px; background: transparent; }"
        "QTabBar::scroller { width: 0px; }"
        "QTabBar QToolButton { width: 0px; height: 0px; }"
    );
    m_tabs->tabBar()->setExpanding(true);
    m_tabs->tabBar()->setUsesScrollButtons(false);
    // Disable scroll policies on existing tab pages
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *scrollArea = qobject_cast<QAbstractScrollArea*>(m_tabs->widget(i))) {
            scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    }
#endif
    m_tabs->addTab(createConnectionTab(), "Connect");
    m_tabs->addTab(createDTCTab(),        "Faults");
    m_tabs->addTab(createLiveDataTab(),   "Live Data");
    m_tabs->addTab(createLogTab(),        "Log");

    mainLayout->addWidget(m_tabs);
    setCentralWidget(central);

    // Status bar
    statusBar()->showMessage("Waiting for connection...");
}


// ================================================================
// Dashboard - 11 gauge responsive grid
// ================================================================

QFrame* MainWindow::createGaugeCard(const QString &title, const QString &initValue,
    const QString &unit, QLabel **valueLabel, QLabel **unitLabel)
{
    QFrame *card = new QFrame();
    card->setFrameShape(QFrame::StyledPanel);
    card->setStyleSheet("QFrame{background:#0e1828;border:1px solid #1a3050;border-radius:6px;padding:4px;}");
    card->setMinimumWidth(70);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout *lay = new QVBoxLayout(card);
    lay->setContentsMargins(2,1,2,1); lay->setSpacing(0);
    QLabel *tl = new QLabel(title);
    tl->setStyleSheet("color:#5888a8;font-size:11px;border:none;background:transparent;");
    tl->setAlignment(Qt::AlignCenter); lay->addWidget(tl);
    QLabel *vl = new QLabel(initValue);
    vl->setAlignment(Qt::AlignCenter);
    vl->setStyleSheet("color:#00d4b4;font-size:16px;font-weight:bold;"
        "font-family:'Consolas','Courier New',monospace;border:none;background:transparent;");
    lay->addWidget(vl);
    QLabel *ul = new QLabel(unit);
    ul->setAlignment(Qt::AlignCenter);
    ul->setStyleSheet("color:#406888;font-size:10px;border:none;background:transparent;");
    lay->addWidget(ul);
    *valueLabel = vl; *unitLabel = ul;
    return card;
}

QWidget* MainWindow::createDashboardPanel()
{
    QWidget *p = new QWidget();
    QGridLayout *g = new QGridLayout(p);
    g->setContentsMargins(2,2,2,2);
    g->setSpacing(3);

    // --- SATIR 0: ANA SÜRÜŞ GÖSTERGELERİ ---
    // En kritik veriler en üstte
    g->addWidget(createGaugeCard("SPEED", "---", "km/h", &m_dashSpeedVal, &m_dashSpeedUnit), 0, 0);
    g->addWidget(createGaugeCard("GEAR",  "---", "",     &m_dashGearVal,  &m_dashGearUnit),  0, 1);
    g->addWidget(createGaugeCard("RPM",   "---", "rpm",  &m_dashMotRpmVal, &m_dashMotRpmUnit), 0, 2);
    g->addWidget(createGaugeCard("TURBIN","---", "rpm",  &m_dashRpmVal,    &m_dashRpmUnit),    0, 3);

    // --- SATIR 1: MOTOR PERFORMANS (ECU) ---
    // Hava ve yakıt verileri yan yana
    g->addWidget(createGaugeCard("BOOST", "---", "mbar", &m_dashMotBoostVal, &m_dashMotBoostUnit), 1, 0);
    g->addWidget(createGaugeCard("MAF",   "---", "mg/s", &m_dashMotMafVal,   &m_dashMotMafUnit),   1, 1);
    g->addWidget(createGaugeCard("RAIL",  "---", "bar",  &m_dashMotRailVal,  &m_dashMotRailUnit),  1, 2);
    g->addWidget(createGaugeCard("LIMP",  "---", "",     &m_dashLimpVal,     &m_dashLimpUnit),     1, 3);

    // --- SATIR 2: SİSTEM SAĞLIĞI & VOLTAJ ---
    // Sıcaklıklar ve elektrik durumu
    g->addWidget(createGaugeCard("M-TEMP","---", "C",    &m_dashMotCoolVal,  &m_dashMotCoolUnit), 2, 0); // Motor Su
    g->addWidget(createGaugeCard("T-TEMP","---", "C",    &m_dashCoolantVal,  &m_dashCoolantUnit),     2, 1); // Trans Yağ
    g->addWidget(createGaugeCard("BATT",  "---", "V",    &m_dashBatVoltVal,  &m_dashBatVoltUnit),     2, 2);
    g->addWidget(createGaugeCard("SOL V", "---", "V",    &m_dashSolVoltVal,  &m_dashSolVoltUnit),     2, 3);

    for(int c=0; c<4; ++c) g->setColumnStretch(c, 1);
    return p;
}

void MainWindow::setGaugeColor(QLabel *vl, const QString &c) {
    vl->setStyleSheet(QString("color:%1;font-size:20px;font-weight:bold;"
        "font-family:'Consolas','Courier New',monospace;border:none;background:transparent;").arg(c));
}

void MainWindow::updateDashboardFromLiveData(const QMap<uint8_t, double> &v)
{
    // TCM J1850 VPW PIDs (APK referansi)
    if(v.contains(0x01)){
        int g=(int)v[0x01];
        m_dashGearVal->setText(gearToString((TCMDiagnostics::Gear)g));
        setGaugeColor(m_dashGearVal, g>=3 ? "#00d4b4" : "#d0a040");
    }
    if(v.contains(0x20)) m_dashSpeedVal->setText(QString::number(v[0x20],'f',0));       // Vehicle Speed
    if(v.contains(0x10)) m_dashRpmVal->setText(QString::number(v[0x10],'f',0));          // Turbine RPM
    if(v.contains(0x16)){                                                                 // Solenoid Supply
        double sv=v[0x16];
        m_dashSolVoltVal->setText(QString::number(sv,'f',1));
        setGaugeColor(m_dashSolVoltVal, sv<9.0?"#e04040":sv<11.0?"#d09030":"#00d4b4");
    }
    // Limp mode: max gear <= 2 ise limp
    if(v.contains(0x03)){
        bool l = v[0x03] <= 2 && v.value(0x14, 0) > 100;
        m_dashLimpVal->setText(l?"ACTIVE!":"Normal");
        setGaugeColor(m_dashLimpVal, l?"#e04040":"#00d4b4");
    }
    // Trans temp
    if(v.contains(0x14)){
        double ct=v[0x14];
        m_dashCoolantVal->setText(QString::number(ct,'f',0));
        setGaugeColor(m_dashCoolantVal, ct>105?"#e04040":ct>95?"#d09030":"#00d4b4");
    }

    // Motor ECU KWP local ID'ler (dual mode veya ECU canli veri)
    // 0x20 = coolant temp block'undan gelir (KWP ReadDataByLocalID)
    if(v.contains(0xE0)){
        // 0xE0 = ECU coolant temp (ozel mapping, Motor ECU oturumundan)
        double ct = v[0xE0];
        m_dashMotCoolVal->setText(QString::number(ct,'f',0));
        setGaugeColor(m_dashMotCoolVal,
            ct > 105 ? "#e04040" : ct > 95 ? "#d09030" : "#00d4b4");
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
    layout->setSpacing(6);

    QGroupBox *connBox = new QGroupBox("Connection");
    connBox->setStyleSheet("QGroupBox{font-weight:bold;color:#70C8F0;font-size:14px;}");
    QGridLayout *connGrid = new QGridLayout(connBox);
    connGrid->setSpacing(4);

    // WiFi row
    connGrid->addWidget(new QLabel("WiFi:"), 0, 0);
    m_hostEdit = new QLineEdit("192.168.0.10");
    connGrid->addWidget(m_hostEdit, 0, 1);
    m_portSpin = new QSpinBox();
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(35000);
    m_portSpin->setMaximumWidth(80);
    connGrid->addWidget(m_portSpin, 0, 2);
    m_connectBtn = new QPushButton("Connect");
    m_connectBtn->setMinimumHeight(36);
    connGrid->addWidget(m_connectBtn, 0, 3);

    // BLE row: Connect + Disconnect
    connGrid->addWidget(new QLabel("BLE:"), 1, 0);
    m_btConnectBtn = new QPushButton("Connect");
    m_btConnectBtn->setMinimumHeight(36);
    connGrid->addWidget(m_btConnectBtn, 1, 1, 1, 2);
    m_disconnectBtn = new QPushButton("Disconnect");
    m_disconnectBtn->setMinimumHeight(36);
    m_disconnectBtn->setEnabled(false);
    connGrid->addWidget(m_disconnectBtn, 1, 3);

    // Status
    m_connStatusLabel = new QLabel("Disconnected");
    m_connStatusLabel->setStyleSheet("color:#e04040;font-weight:bold;font-size:13px;padding:2px;");
    m_connStatusLabel->setAlignment(Qt::AlignCenter);
    connGrid->addWidget(m_connStatusLabel, 2, 0, 1, 4);

    // Hidden internals
    m_btScanBtn = new QPushButton(); // hidden, scan triggered by connect
    m_btScanBtn->setVisible(false);
    m_btCombo = new QComboBox(); // hidden, device tracking
    m_btCombo->setVisible(false);

    layout->addWidget(connBox);

    // === Module List ===
    QGroupBox *modBox = new QGroupBox("Modules");
    modBox->setStyleSheet("QGroupBox{font-weight:bold;color:#70C8F0;font-size:14px;}");
    m_moduleListLayout = new QVBoxLayout(modBox);
    m_moduleListLayout->setSpacing(3);

    // Show verified modules prominently, others dimmed
    struct ModEntry {
        WJDiagnostics::Module id;
        QString label;
        QString detail;
        bool verified;
    };
    QList<ModEntry> modEntries = {
        {WJDiagnostics::Module::KLineTCM, "TCM (K-Line)", "NAG1 722.6 | ATSH8120F1 | KWP2000", true},
        {WJDiagnostics::Module::MotorECU, "Engine ECU", "Bosch EDC15C2 OM612 | ATSH8115F1 | KWP2000", true},
        {WJDiagnostics::Module::ABS, "ABS / ESP", "J1850 VPW | ATSH244022", true},
        {WJDiagnostics::Module::Airbag, "Airbag", "J1850 VPW | ATSH246022", true},
        {WJDiagnostics::Module::BCM, "Body Computer", "J1850 VPW | ATSH248022", false},
        {WJDiagnostics::Module::Cluster, "Instrument Cluster", "J1850 VPW | ATSH249022", false},
        {WJDiagnostics::Module::ATC, "Climate (HVAC)", "J1850 VPW | ATSH246822", false},
        {WJDiagnostics::Module::SKIM, "SKIM Immobilizer", "J1850 VPW | ATSH246222", false},
    };

    m_moduleButtons.clear();
    for (const auto &me : modEntries) {
        QPushButton *btn = new QPushButton();
        btn->setCheckable(true);
        btn->setMinimumHeight(48);
        btn->setProperty("moduleId", static_cast<int>(static_cast<uint8_t>(me.id)));

        // Two-line text: name + protocol detail
        btn->setText(me.label + "\n" + me.detail);
        btn->setStyleSheet(QString(
            "QPushButton{text-align:left;padding:6px 12px;background:%1;color:%2;"
            "border:1px solid %3;border-radius:5px;font-size:12px;}"
            "QPushButton:checked{background:#0a3830;color:#00d4b4;border:1px solid #00806a;font-weight:bold;}"
            "QPushButton:disabled{background:#0a0e14;color:#303840;border:1px solid #1a1e24;}")
            .arg(me.verified ? "#0e1828" : "#0a0e14")
            .arg(me.verified ? "#80b0d0" : "#405060")
            .arg(me.verified ? "#1a4060" : "#101820"));

        btn->setEnabled(false); // enabled after BT connect

        connect(btn, &QPushButton::clicked, this, [this, btn, me]() {
            // If already active, deactivate
            if (m_moduleSessionActive && m_activeModId == me.id) {
                m_moduleSessionActive = false;
                btn->setChecked(false);
                if (m_liveData->isPolling()) onStopLiveData();
                updateActiveHeaderLabel();
                statusBar()->showMessage("Module deactivated");
                return;
            }

            // Stop live data if running
            if (m_liveData->isPolling()) onStopLiveData();

            // Uncheck all other buttons
            for (auto *b : m_moduleButtons) b->setChecked(false);
            btn->setChecked(true);

            // Disable all buttons during switch
            for (auto *b : m_moduleButtons) b->setEnabled(false);
            statusBar()->showMessage("Switching to " + me.label + "...");

            m_tcm->switchToModule(me.id, [this, btn, me](bool ok) {
                // Re-enable all buttons
                for (auto *b : m_moduleButtons) b->setEnabled(true);

                if (ok) {
                    m_activeModId = me.id;
                    m_moduleSessionActive = true;
                    btn->setChecked(true);
                    m_readDtcBtn->setEnabled(true);
                    m_clearDtcBtn->setEnabled(true);
                    m_startLiveBtn->setEnabled(true);

                    // Auto-set live data mode
                    if (me.id == WJDiagnostics::Module::KLineTCM) {
                        m_liveData->setMode(LiveDataManager::TCM_ONLY);
                    } else if (me.id == WJDiagnostics::Module::MotorECU) {
                        m_liveData->setMode(LiveDataManager::ECU_ONLY);
                    } else {
                        // J1850 modules - no live data yet
                        m_startLiveBtn->setEnabled(false);
                    }

                    // Auto-set DTC source + update DTC tab buttons
                    if (me.id == WJDiagnostics::Module::KLineTCM)
                        m_dtcSourceIdx = 0;
                    else if (me.id == WJDiagnostics::Module::MotorECU)
                        m_dtcSourceIdx = 1;
                    else if (me.id == WJDiagnostics::Module::ABS)
                        m_dtcSourceIdx = 2;
                    else if (me.id == WJDiagnostics::Module::Airbag)
                        m_dtcSourceIdx = 3;
                    if (m_dtcTcmBtn) {
                        m_dtcTcmBtn->setChecked(m_dtcSourceIdx == 0);
                        m_dtcEcuBtn->setChecked(m_dtcSourceIdx == 1);
                        m_dtcAbsBtn->setChecked(m_dtcSourceIdx == 2);
                        m_dtcAirbagBtn->setChecked(m_dtcSourceIdx == 3);
                        static const char* names[] = {"TCM","ECU","ABS","Airbag"};
                        m_dtcCountLabel->setText(QString("Source: %1 - 0 fault codes").arg(names[m_dtcSourceIdx]));
                        m_dtcTable->setRowCount(0);
                    }

                    // Update live data table selections for this module
                    updateLiveTableForModule();

                    statusBar()->showMessage(me.label + " active");
                } else {
                    btn->setChecked(false);
                    m_moduleSessionActive = false;
                    statusBar()->showMessage(me.label + " connection failed!");
                }
                updateActiveHeaderLabel();
            });
        });

        m_moduleButtons.append(btn);
        m_moduleListLayout->addWidget(btn);
    }

    layout->addWidget(modBox);

    // Active Header
    m_activeHeaderLabel = new QLabel("---");
    m_activeHeaderLabel->setStyleSheet("background:#0e1828;padding:4px;border-radius:4px;"
                                       "color:#80a8c0;font-family:monospace;font-weight:bold;font-size:10px;");
    m_activeHeaderLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_activeHeaderLabel);

    layout->addStretch();

    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnect);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &MainWindow::onDisconnect);

    // BLE Connect: start scan, auto-connect on first match
    connect(m_btConnectBtn, &QPushButton::clicked, this, [this]() {
        m_btCombo->clear();
        m_btConnectBtn->setEnabled(false);
        m_connStatusLabel->setText("Scanning BLE...");
        m_connStatusLabel->setStyleSheet("color:orange;font-weight:bold;font-size:13px;padding:2px;");
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        requestBluetoothPermissions();
#else
        m_elm->scanBluetooth();
#endif
    });
#if HAS_BLUETOOTH
    connect(m_elm, &ELM327Connection::bluetoothDeviceFound, this,
            [this](const QString &name, const QString &addr) {
        m_btCombo->addItem(name + " [" + addr + "]", addr);
        if (m_btCombo->count() == 1) {
            m_btCombo->setCurrentIndex(0);
            // Auto-connect to first found device
            m_connStatusLabel->setText("Found: " + name + " — connecting...");
            m_connStatusLabel->setStyleSheet("color:orange;font-weight:bold;font-size:13px;padding:2px;");
            m_elm->connectBluetooth(addr);
        }
    });
    connect(m_elm, &ELM327Connection::bluetoothScanFinished, this, [this]() {
        if (m_btCombo->count() == 0) {
            m_connStatusLabel->setText("No BLE device found");
            m_connStatusLabel->setStyleSheet("color:#e04040;font-weight:bold;font-size:13px;padding:2px;");
            m_btConnectBtn->setEnabled(true);
        }
    });
#endif

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

    m_dtcTcmBtn = new QPushButton("TCM");
    m_dtcTcmBtn->setCheckable(true);
    m_dtcTcmBtn->setChecked(true);
    m_dtcTcmBtn->setMinimumHeight(36);
    m_dtcTcmBtn->setStyleSheet(
        "QPushButton{background:#1a3a5a;color:white;border:1px solid #3a6a9a;border-radius:4px;padding:8px 14px;}"
        "QPushButton:checked{background:#0a3830;color:#00d4b4;border:1px solid #00806a;font-weight:bold;}");

    m_dtcEcuBtn = new QPushButton("ECU");
    m_dtcEcuBtn->setCheckable(true);
    m_dtcEcuBtn->setChecked(false);
    m_dtcEcuBtn->setMinimumHeight(36);
    m_dtcEcuBtn->setStyleSheet(
        "QPushButton{background:#3a3a1a;color:white;border:1px solid #6a6a3a;border-radius:4px;padding:8px 14px;}"
        "QPushButton:checked{background:#0a3830;color:#00d4b4;border:1px solid #00806a;font-weight:bold;}");

    m_dtcAbsBtn = new QPushButton("ABS");
    m_dtcAbsBtn->setCheckable(true);
    m_dtcAbsBtn->setChecked(false);
    m_dtcAbsBtn->setMinimumHeight(36);
    m_dtcAbsBtn->setStyleSheet(
        "QPushButton{background:#1a2a3a;color:white;border:1px solid #3a5a7a;border-radius:4px;padding:8px 14px;}"
        "QPushButton:checked{background:#0a3830;color:#00d4b4;border:1px solid #00806a;font-weight:bold;}");

    m_dtcAirbagBtn = new QPushButton("Airbag");
    m_dtcAirbagBtn->setCheckable(true);
    m_dtcAirbagBtn->setChecked(false);
    m_dtcAirbagBtn->setMinimumHeight(36);
    m_dtcAirbagBtn->setStyleSheet(
        "QPushButton{background:#3a2a1a;color:white;border:1px solid #6a4a2a;border-radius:4px;padding:8px 14px;}"
        "QPushButton:checked{background:#0a3830;color:#00d4b4;border:1px solid #00806a;font-weight:bold;}");

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

    m_readDtcBtn  = new QPushButton("Read DTC");
    m_clearDtcBtn = new QPushButton("Clear DTC");
    m_dtcCountLabel = new QLabel("Source: TCM - 0 fault codes");

    m_readDtcBtn->setMinimumHeight(36);
    m_clearDtcBtn->setMinimumHeight(36);
    m_readDtcBtn->setEnabled(false);
    m_clearDtcBtn->setEnabled(false);

    btnLayout->addWidget(m_readDtcBtn);
    btnLayout->addWidget(m_clearDtcBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_dtcCountLabel);

    layout->addLayout(btnLayout);

    // DTC tablosu - 6 sutun (kaynak eklendi)
    m_dtcTable = new QTableWidget(0, 3);
    m_dtcTable->setHorizontalHeaderLabels({"Code", "Description", "State"});
    m_dtcTable->horizontalHeader()->setStretchLastSection(true);
    m_dtcTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_dtcTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dtcTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dtcTable->setAlternatingRowColors(true);
    QScroller::grabGesture(m_dtcTable->viewport(), QScroller::LeftMouseButtonGesture);

    layout->addWidget(m_dtcTable);

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

    m_startLiveBtn->setMinimumHeight(36);
    m_stopLiveBtn->setMinimumHeight(36);
    m_logBtn->setMinimumHeight(36);
    m_startLiveBtn->setEnabled(false);
    m_stopLiveBtn->setEnabled(false);

    btnLayout->addWidget(m_startLiveBtn);
    btnLayout->addWidget(m_stopLiveBtn);
    btnLayout->addWidget(m_logBtn);

    layout->addLayout(btnLayout);

    // Live data tablosu
    m_liveTable = new QTableWidget(0, 3);
    m_liveTable->setHorizontalHeaderLabels({
        "Sel", "Parameter", "Value"
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

        // Checkbox - unchecked by default, updateLiveTableForModule sets per module
        QTableWidgetItem *checkItem = new QTableWidgetItem();
        checkItem->setCheckState(Qt::Unchecked);
        m_liveTable->setItem(i, 0, checkItem);

        // Name
        m_liveTable->setItem(i, 1, new QTableWidgetItem(p.name));

        // Value
        QTableWidgetItem *valItem = new QTableWidgetItem("---");
        valItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QFont valFont;
        valFont.setFamily("Consolas");
        valFont.setPointSize(11);
        valItem->setFont(valFont);
        m_liveTable->setItem(i, 2, valItem);
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


QWidget* MainWindow::createLogTab()
{
    QWidget *w = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(w);

    m_logText = new QTextEdit();
    m_logText->setReadOnly(true);
    m_logText->setFont(QFont("Consolas", 9));
    m_logText->setStyleSheet("background: #0a1220; color: #60b8a0; font-size: 12px;");

    layout->addWidget(m_logText);

    // --- Log butonlari: 2 satirlik grid ---
    QGridLayout *logGrid = new QGridLayout();
    logGrid->setSpacing(4);

    // Row 0: Clear | Copy Log | Raw Data Read
    QPushButton *clearLogBtn = new QPushButton("Clear");
    clearLogBtn->setStyleSheet("padding:8px 12px;");
    connect(clearLogBtn, &QPushButton::clicked, m_logText, &QTextEdit::clear);
    logGrid->addWidget(clearLogBtn, 0, 0);

    QPushButton *saveLogBtn = new QPushButton("Copy Log");
    saveLogBtn->setStyleSheet("background:#0a2820; color:#00d4b4; font-weight:bold; padding:8px 12px; font-size:13px;");
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
    m_rawCmdEdit->setStyleSheet("background:#0e1828; color:#60b8a0; border:1px solid #1a3050; padding:6px; font-size:13px;");
    logGrid->addWidget(m_rawCmdEdit, 1, 0, 1, 2);

    m_rawSendBtn = new QPushButton("Send");
    m_rawSendBtn->setStyleSheet("background:#4a2858; color:#ff88ff; font-weight:bold; padding:4px 6px;");
    connect(m_rawSendBtn, &QPushButton::clicked, this, &MainWindow::onRawSendCustom);
    connect(m_rawCmdEdit, &QLineEdit::returnPressed, this, &MainWindow::onRawSendCustom);
    logGrid->addWidget(m_rawSendBtn, 1, 2);

    // Satir 2: Timeout ayari
    QLabel *toLabel = new QLabel("Timeout:");
    toLabel->setStyleSheet("color:#808890; font-size:11px;");
    logGrid->addWidget(toLabel, 2, 0);
    m_timeoutSpin = new QSpinBox();
    m_timeoutSpin->setRange(200, 10000);
    m_timeoutSpin->setSingleStep(100);
    m_timeoutSpin->setValue(3000);
    m_timeoutSpin->setSuffix(" ms");
    m_timeoutSpin->setStyleSheet("background:#0e1828; color:#60b8a0; border:1px solid #1a3050; padding:4px; font-size:12px;");
    connect(m_timeoutSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        m_elm->setDefaultTimeout(val);
        statusBar()->showMessage(QString("Timeout: %1 ms").arg(val), 2000);
    });
    logGrid->addWidget(m_timeoutSpin, 2, 1, 1, 2);

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
        m_connStatusLabel->setText("Disconnected");
        m_connStatusLabel->setStyleSheet("color:#e04040;font-weight:bold;font-size:13px;padding:2px;");
        m_connectBtn->setEnabled(true);
        m_btConnectBtn->setEnabled(true);
        m_disconnectBtn->setEnabled(false);
        for (auto *b : m_moduleButtons) { b->setEnabled(false); b->setChecked(false); }
        m_moduleSessionActive = false;
        m_readDtcBtn->setEnabled(false);
        m_clearDtcBtn->setEnabled(false);
        m_startLiveBtn->setEnabled(false);
        if (m_batteryTimer) m_batteryTimer->stop();
        updateActiveHeaderLabel();
        statusBar()->showMessage("Disconnected");
        break;

    case ELM327Connection::ConnectionState::Connecting:
        m_connStatusLabel->setText("Connecting...");
        m_connStatusLabel->setStyleSheet("color:orange;font-weight:bold;font-size:13px;padding:2px;");
        m_connectBtn->setEnabled(false);
        m_btConnectBtn->setEnabled(false);
        statusBar()->showMessage("Connecting...");
        break;

    case ELM327Connection::ConnectionState::Initializing:
        m_connStatusLabel->setText("Initializing ELM327...");
        m_connStatusLabel->setStyleSheet("color:yellow;font-weight:bold;font-size:13px;padding:2px;");
        statusBar()->showMessage("Initializing ELM327...");
        break;

    case ELM327Connection::ConnectionState::Ready: {
        QString devName = "ELM327";
        if (m_btCombo && m_btCombo->count() > 0)
            devName = m_btCombo->currentText().section('[', 0, 0).trimmed();
        m_connStatusLabel->setText(devName + " — Ready");
        m_connStatusLabel->setStyleSheet("color:lime;font-weight:bold;font-size:13px;padding:2px;");
        m_connectBtn->setEnabled(false);
        m_btConnectBtn->setEnabled(false);
        m_disconnectBtn->setEnabled(true);
        for (auto *b : m_moduleButtons) b->setEnabled(true);
        statusBar()->showMessage("Ready — Select a module");
        break;
    }

    case ELM327Connection::ConnectionState::Error:
        m_connStatusLabel->setText("Connection Error!");
        m_connStatusLabel->setStyleSheet("color:#e04040;font-weight:bold;font-size:13px;padding:2px;");
        m_connectBtn->setEnabled(true);
        m_btConnectBtn->setEnabled(true);
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
    default: mod = WJDiagnostics::Module::KLineTCM; break;
    }

    m_tcm->readDTCs(mod, [this, src](const QList<WJDiagnostics::DTCEntry> &dtcs) {
        m_dtcTable->setRowCount(0);
        for (const auto &d : dtcs) {
            int row = m_dtcTable->rowCount();
            m_dtcTable->insertRow(row);
            m_dtcTable->setItem(row, 0, new QTableWidgetItem(d.code));
            m_dtcTable->setItem(row, 1, new QTableWidgetItem(d.description));
            m_dtcTable->setItem(row, 2, new QTableWidgetItem(d.isActive ? "Active" : "Stored"));

            if (d.isActive) {
                for (int col = 0; col < 3; ++col) {
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
        this, "Clear DTC", warning,
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        m_clearDtcBtn->setEnabled(false);
        statusBar()->showMessage("Clearing " + src + " fault codes...");

        WJDiagnostics::Module mod;
        switch (m_dtcSourceIdx) {
        case 1:  mod = WJDiagnostics::Module::MotorECU; break;
        case 2:  mod = WJDiagnostics::Module::ABS; break;
        case 3:  mod = WJDiagnostics::Module::Airbag; break;
        default: mod = WJDiagnostics::Module::KLineTCM; break;
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
        QMessageBox::warning(this, "Parameter Selection",
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
            status.batteryVoltage < 11.5 ? "#e04040" : status.batteryVoltage < 12.5 ? "#d09030" : "#00d4b4");
    }
}

void MainWindow::onECUDataUpdated(const TCMDiagnostics::ECUStatus &ecu)
{
    // Motor RPM
    m_dashMotRpmVal->setText(QString::number(ecu.rpm, 'f', 0));
    setGaugeColor(m_dashMotRpmVal,
        ecu.rpm > 4500 ? "#e04040" : ecu.rpm > 3500 ? "#d09030" : "#00d4b4");

    // Boost Pressure (mbar)
    m_dashMotBoostVal->setText(QString::number(ecu.boostPressure, 'f', 0));
    setGaugeColor(m_dashMotBoostVal,
        ecu.boostPressure > 2000 ? "#e04040" : ecu.boostPressure > 1500 ? "#d09030" : "#00d4b4");

    // MAF
    m_dashMotMafVal->setText(QString::number(ecu.mafActual, 'f', 0));

    // Rail Pressure (bar)
    m_dashMotRailVal->setText(QString::number(ecu.railActual, 'f', 0));
    setGaugeColor(m_dashMotRailVal,
        ecu.railActual > 1400 ? "#e04040" : ecu.railActual > 1200 ? "#d09030" : "#00d4b4");

    // Su sicakligi (Motor ECU'dan gelen coolant)
    m_dashMotCoolVal->setText(QString::number(ecu.coolantTemp, 'f', 0));
    setGaugeColor(m_dashMotCoolVal,
        ecu.coolantTemp > 105 ? "#e04040" : ecu.coolantTemp > 95 ? "#d09030" : "#00d4b4");

    // Battery voltage (ECU'dan)
    if (ecu.batteryVoltage > 0) {
        m_dashBatVoltVal->setText(QString::number(ecu.batteryVoltage, 'f', 1));
        setGaugeColor(m_dashBatVoltVal,
            ecu.batteryVoltage < 11.5 ? "#e04040" : ecu.batteryVoltage < 12.5 ? "#d09030" : "#00d4b4");
    }
}


void MainWindow::onLogMessage(const QString &msg)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    m_logText->append(QString("[%1] %2").arg(timestamp, msg));
    m_logText->verticalScrollBar()->setValue(m_logText->verticalScrollBar()->maximum());
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
            st.coolantTemp>105?"#e04040":st.coolantTemp>95?"#d09030":"#00d4b4");
    }
    setGaugeColor(m_dashLimpVal, st.limpMode ? "#e04040" : "#00d4b4");
    setGaugeColor(m_dashSolVoltVal,
        st.solenoidSupply<9.0?"#e04040":st.solenoidSupply<11.0?"#d09030":"#00d4b4");
    setGaugeColor(m_dashCoolantVal,
        st.transTemp>105?"#e04040":st.transTemp>95?"#d09030":"#00d4b4");
}

void MainWindow::updateActiveHeaderLabel()
{
    if (!m_moduleSessionActive) {
        m_activeHeaderLabel->setText("---");
        m_activeHeaderLabel->setStyleSheet(
            "background:#0e1828;padding:4px;border-radius:4px;"
            "color:#606870;font-family:monospace;font-weight:bold;font-size:10px;");
        return;
    }
    auto info = WJDiagnostics::moduleInfo(m_activeModId);
    QString busStr = (info.bus == WJDiagnostics::BusType::KLine) ? "K-Line" : "J1850 VPW";
    QString text = QString("Active: %1 | %2 | %3").arg(info.shortName, busStr, info.atshHeader);
    m_activeHeaderLabel->setText(text);
    m_activeHeaderLabel->setStyleSheet(
        "background:#0a3830;padding:4px;border-radius:4px;"
        "color:#00d4b4;font-family:monospace;font-weight:bold;font-size:10px;");
}

void MainWindow::updateLiveTableForModule()
{
    auto params = m_tcm->liveDataParams();

    // TCM params: 0x01-0x30, ECU params: 0xF0-0xFF
    QSet<uint8_t> tcmDefaults = {0x01, 0x10, 0x13, 0x14, 0x16, 0x17, 0x18, 0x20};
    QSet<uint8_t> ecuDefaults = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8};

    bool isTCM = (m_activeModId == WJDiagnostics::Module::KLineTCM);
    bool isECU = (m_activeModId == WJDiagnostics::Module::MotorECU);

    for (int i = 0; i < params.size() && i < m_liveTable->rowCount(); ++i) {
        uint8_t id = params[i].localID;
        bool isTcmParam = (id < 0xE0);
        bool isEcuParam = (id >= 0xF0);

        bool checked = false;
        if (isTCM && isTcmParam)
            checked = tcmDefaults.contains(id);
        else if (isECU && isEcuParam)
            checked = ecuDefaults.contains(id);

        if (m_liveTable->item(i, 0))
            m_liveTable->item(i, 0)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);

        // Dim irrelevant params
        QColor fg = checked ? QColor(200, 220, 240) :
                    ((isTCM && isTcmParam) || (isECU && isEcuParam))
                        ? QColor(140, 160, 180) : QColor(60, 70, 80);
        for (int col = 0; col < 3; ++col) {
            if (m_liveTable->item(i, col))
                m_liveTable->item(i, col)->setForeground(fg);
        }

        // Reset value column
        if (m_liveTable->item(i, 2))
            m_liveTable->item(i, 2)->setText("---");
    }
}

QString MainWindow::gearToString(TCMDiagnostics::Gear gear)
{
    switch (gear) {
    case TCMDiagnostics::Gear::Park:    return "P";
    case TCMDiagnostics::Gear::Reverse: return "R";
    case TCMDiagnostics::Gear::Neutral: return "N";
    case TCMDiagnostics::Gear::Drive1:  return "D";
    case TCMDiagnostics::Gear::Drive2:  return "D2";
    case TCMDiagnostics::Gear::Drive3:  return "D3";
    case TCMDiagnostics::Gear::Drive4:  return "D4";
    case TCMDiagnostics::Gear::Drive5:  return "D5";
    case TCMDiagnostics::Gear::Limp:    return "LIMP!";
    default: return "?";
    }
}


// --- Raw Data Read - Test v9: Streamlined diagnostics ---
// Changes from v8:
//   - ATFI check: accept "BUS INIT" OR "OK" (real ELM327 says "BUS INIT: ...OK")
//   - TCM Phase 1: unlock + read all known + undiscovered blocks in one pass
//   - ECU Phase 2: no-security blocks + new block scan
//   - ECU Phase 3: security test with seed logging (12 combos, skip-on-NRC-level)
//   - J1850 Phase 4: ABS/Airbag/BCM/Cluster/HVAC/SKIM (compact)
//   - No code duplication: shared helper for post-unlock reads
//   - parseTCMBlock30 now writes TCC slip, line pressure to TCMStatus
void MainWindow::onRawBusDump()
{
    m_rawDumpBtn->setEnabled(false);
    m_rawDumpBtn->setText("Testing...");

    auto log = [this](const QString &color, const QString &msg) {
        QString ts = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
        m_logText->append(QString("<font color='%1'>[%2] %3</font>").arg(color, ts, msg));
        m_logText->verticalScrollBar()->setValue(m_logText->verticalScrollBar()->maximum());
    };

    auto done = [this]() {
        m_rawDumpBtn->setEnabled(true);
        m_rawDumpBtn->setText("Raw Data Read");
    };

    m_logText->append("<font color='white'>========== TEST v9 ==========</font>");

    // Run discovery phases
    runDiscoveryPhases(log, done);
}


// --- Test v9: Discovery Phases (sequential command runner) ---
void MainWindow::runDiscoveryPhases(
    std::function<void(const QString&, const QString&)> log,
    std::function<void()> done)
{
    struct Step {
        QString label;
        QString action;
    };

    auto steps = std::make_shared<QList<Step>>();

    // =====================================================================
    // PHASE 1: TCM (K-Line 0x20) - Security + Block Discovery
    // =====================================================================
    steps->append(Step{"", "header:=== Phase 1: TCM (K-Line 0x20) ==="});
    steps->append(Step{"", "switch:tcm"});
    // Confirmed live data block
    steps->append(Step{"TCM 0x30 (live)", "cmd:21 30"});
    // Undiscovered blocks - scan for new data
    steps->append(Step{"TCM 0x31", "cmd:21 31"});
    steps->append(Step{"TCM 0x32", "cmd:21 32"});
    steps->append(Step{"TCM 0x40", "cmd:21 40"});
    steps->append(Step{"TCM 0x50", "cmd:21 50"});
    steps->append(Step{"TCM 0x60", "cmd:21 60"});
    steps->append(Step{"TCM 0x70", "cmd:21 70"});
    steps->append(Step{"TCM 0x80", "cmd:21 80"});
    steps->append(Step{"TCM 0x90", "cmd:21 90"});
    steps->append(Step{"TCM 0xA0", "cmd:21 A0"});
    steps->append(Step{"TCM 0xB0", "cmd:21 B0"});
    steps->append(Step{"TCM 0xC0", "cmd:21 C0"});
    steps->append(Step{"TCM 0xD0", "cmd:21 D0"});
    steps->append(Step{"TCM 0xE0", "cmd:21 E0"});
    steps->append(Step{"TCM 0xE1", "cmd:21 E1"});
    steps->append(Step{"TCM 0xE2", "cmd:21 E2"});
    steps->append(Step{"TCM 0xF0", "cmd:21 F0"});
    // ECU identification
    steps->append(Step{"TCM 1A 86 (mfr)", "cmd:1A 86"});
    steps->append(Step{"TCM 1A 87 (id)", "cmd:1A 87"});
    steps->append(Step{"TCM 1A 90 (VIN)", "cmd:1A 90"});
    steps->append(Step{"TCM 1A 91 (HW)", "cmd:1A 91"});
    steps->append(Step{"TCM 1A 92 (SW)", "cmd:1A 92"});
    // IOControl / Adaptation / Routines (need security first but try anyway)
    steps->append(Step{"TCM IOCtrl 30 10", "cmd:30 10 07 00 02"});
    steps->append(Step{"TCM Adapt 3B 90", "cmd:3B 90"});

    // =====================================================================
    // PHASE 2: ECU (K-Line 0x15) - No-Security Blocks
    // =====================================================================
    steps->append(Step{"", "header:=== Phase 2: ECU (K-Line 0x15) No-Security ==="});
    steps->append(Step{"", "switch:ecu"});
    // Confirmed working blocks
    steps->append(Step{"ECU 0x12 (temp/sens)", "cmd:21 12"});
    steps->append(Step{"ECU 0x20 (MAF)", "cmd:21 20"});
    steps->append(Step{"ECU 0x22 (rail/map)", "cmd:21 22"});
    steps->append(Step{"ECU 0x28 (rpm/inj)", "cmd:21 28"});
    // Scan unconfirmed blocks (may need security)
    steps->append(Step{"ECU 0x10", "cmd:21 10"});
    steps->append(Step{"ECU 0x14", "cmd:21 14"});
    steps->append(Step{"ECU 0x16", "cmd:21 16"});
    steps->append(Step{"ECU 0x18", "cmd:21 18"});
    steps->append(Step{"ECU 0x1A", "cmd:21 1A"});
    steps->append(Step{"ECU 0x1C", "cmd:21 1C"});
    steps->append(Step{"ECU 0x1E", "cmd:21 1E"});
    steps->append(Step{"ECU 0x24", "cmd:21 24"});
    steps->append(Step{"ECU 0x26", "cmd:21 26"});
    steps->append(Step{"ECU 0x2A", "cmd:21 2A"});
    steps->append(Step{"ECU 0x30", "cmd:21 30"});
    steps->append(Step{"ECU 0x40", "cmd:21 40"});
    steps->append(Step{"ECU 0x50", "cmd:21 50"});
    steps->append(Step{"ECU 0x60", "cmd:21 60"});
    // Security-protected blocks (will get NRC 0x33 without unlock)
    steps->append(Step{"ECU 0x62 (EGR/WG)", "cmd:21 62"});
    steps->append(Step{"ECU 0xB0 (inj corr)", "cmd:21 B0"});
    steps->append(Step{"ECU 0xB1 (boost ad)", "cmd:21 B1"});
    steps->append(Step{"ECU 0xB2 (fuel ad)", "cmd:21 B2"});
    // ECU identification
    steps->append(Step{"ECU 1A 86", "cmd:1A 86"});
    steps->append(Step{"ECU 1A 87", "cmd:1A 87"});
    steps->append(Step{"ECU 1A 90 (VIN)", "cmd:1A 90"});
    steps->append(Step{"ECU 1A 91", "cmd:1A 91"});
    // Battery
    steps->append(Step{"Battery (ATRV)", "cmd:ATRV"});

    // =====================================================================
    // PHASE 3: ECU Security Key Test
    // =====================================================================
    steps->append(Step{"", "header:=== Phase 3: ECU Security Key Test ==="});
    steps->append(Step{"", "switch:ecu_sectest"});

    // =====================================================================
    // PHASE 4: J1850 VPW Module Scan
    // =====================================================================
    steps->append(Step{"", "header:=== Phase 4: J1850 VPW Modules ==="});
    steps->append(Step{"", "switch:j1850"});

    // --- ABS (0x40) - verified ---
    steps->append(Step{"", "header:--- ABS (0x40) ---"});
    steps->append(Step{"ABS DTC", "j1850cmd:24 00 00"});
    steps->append(Step{"ABS LF wheel", "j1850cmd:20 01 00"});
    steps->append(Step{"ABS RF wheel", "j1850cmd:20 02 00"});
    steps->append(Step{"ABS LR wheel", "j1850cmd:20 03 00"});
    steps->append(Step{"ABS RR wheel", "j1850cmd:20 04 00"});
    steps->append(Step{"ABS speed", "j1850cmd:20 10 00"});
    steps->append(Step{"ABS PID 05", "j1850cmd:20 05 00"});
    steps->append(Step{"ABS PID 06", "j1850cmd:20 06 00"});
    steps->append(Step{"ABS PID 07", "j1850cmd:20 07 00"});
    steps->append(Step{"ABS PID 08", "j1850cmd:20 08 00"});
    steps->append(Step{"ABS PID 09", "j1850cmd:20 09 00"});
    steps->append(Step{"ABS PID 0A", "j1850cmd:20 0A 00"});
    steps->append(Step{"ABS svc 1A 87", "j1850cmd:1A 87 00"});

    // --- Airbag (0x60) - verified ---
    steps->append(Step{"", "header:--- Airbag (0x60) ---"});
    steps->append(Step{"", "j1850hdr:ATSH246022"});
    steps->append(Step{"", "j1850hdr:ATRA60"});
    steps->append(Step{"Airbag DTC", "j1850cmd:28 37 00"});
    steps->append(Step{"Airbag svc 1A 87", "j1850cmd:1A 87 00"});

    // --- BCM (0x80) ---
    steps->append(Step{"", "header:--- BCM (0x80) ---"});
    steps->append(Step{"", "j1850hdr:ATSH248022"});
    steps->append(Step{"", "j1850hdr:ATRA80"});
    steps->append(Step{"BCM DTC 00", "j1850cmd:20 00 00"});
    steps->append(Step{"BCM DTC 37", "j1850cmd:20 37 00"});
    steps->append(Step{"BCM PID 01", "j1850cmd:20 01 00"});
    steps->append(Step{"BCM PID 02", "j1850cmd:20 02 00"});
    steps->append(Step{"BCM PID 03", "j1850cmd:20 03 00"});
    steps->append(Step{"BCM PID 04", "j1850cmd:20 04 00"});
    steps->append(Step{"BCM svc 1A 87", "j1850cmd:1A 87 00"});
    steps->append(Step{"BCM svc 22 01", "j1850cmd:22 00 01"});
    steps->append(Step{"BCM svc 22 02", "j1850cmd:22 00 02"});

    // --- Cluster (0x90) ---
    steps->append(Step{"", "header:--- Cluster (0x90) ---"});
    steps->append(Step{"", "j1850hdr:ATSH249022"});
    steps->append(Step{"", "j1850hdr:ATRA90"});
    steps->append(Step{"Cluster DTC 00", "j1850cmd:30 00 00"});
    steps->append(Step{"Cluster DTC 37", "j1850cmd:30 37 00"});
    steps->append(Step{"Cluster PID 01", "j1850cmd:30 01 00"});
    steps->append(Step{"Cluster PID 02", "j1850cmd:30 02 00"});
    steps->append(Step{"Cluster svc 1A 87", "j1850cmd:1A 87 00"});
    steps->append(Step{"Cluster svc 22 01", "j1850cmd:22 00 01"});

    // --- HVAC (0x68) ---
    steps->append(Step{"", "header:--- HVAC (0x68) ---"});
    steps->append(Step{"", "j1850hdr:ATSH246822"});
    steps->append(Step{"", "j1850hdr:ATRA68"});
    steps->append(Step{"HVAC DTC 00", "j1850cmd:28 00 00"});
    steps->append(Step{"HVAC DTC 37", "j1850cmd:28 37 00"});
    steps->append(Step{"HVAC PID 01", "j1850cmd:28 01 00"});
    steps->append(Step{"HVAC PID 02", "j1850cmd:28 02 00"});
    steps->append(Step{"HVAC PID 03", "j1850cmd:28 03 00"});
    steps->append(Step{"HVAC svc 1A 87", "j1850cmd:1A 87 00"});
    steps->append(Step{"HVAC svc 22 01", "j1850cmd:22 00 01"});

    // --- SKIM (0x62) ---
    steps->append(Step{"", "header:--- SKIM (0x62) ---"});
    steps->append(Step{"", "j1850hdr:ATSH246222"});
    steps->append(Step{"", "j1850hdr:ATRA62"});
    steps->append(Step{"SKIM DTC 00", "j1850cmd:22 00 00"});
    steps->append(Step{"SKIM DTC 37", "j1850cmd:22 37 00"});
    steps->append(Step{"SKIM PID 01", "j1850cmd:22 01 00"});
    steps->append(Step{"SKIM svc 1A 87", "j1850cmd:1A 87 00"});

    // --- Quick probes: Radio/EVIC/MemSeat/Liftgate ---
    steps->append(Step{"", "header:--- Other Modules ---"});
    steps->append(Step{"", "j1850hdr:ATSH248722"});
    steps->append(Step{"Radio probe", "j1850cmd:27 00 00"});
    steps->append(Step{"", "j1850hdr:ATSH242A22"});
    steps->append(Step{"EVIC probe", "j1850cmd:0A 00 00"});
    steps->append(Step{"", "j1850hdr:ATSH249822"});
    steps->append(Step{"MemSeat probe", "j1850cmd:38 00 00"});
    steps->append(Step{"", "j1850hdr:ATSH24A022"});
    steps->append(Step{"Liftgate probe", "j1850cmd:40 00 00"});

    // --- Final ---
    steps->append(Step{"", "header:--- Final ---"});
    steps->append(Step{"Battery", "cmd:ATRV"});

    // =====================================================================
    // Step Runner
    // =====================================================================
    auto idx = std::make_shared<int>(0);
    auto run = std::make_shared<std::function<void()>>();

    *run = [this, steps, idx, run, log, done]() {
        if (*idx >= steps->size()) {
            m_logText->append("<font color='white'>========== TEST v9 BITTI ==========</font>");
            log("#ffff00", "COPY LOG ile kopyala!");
            done();
            return;
        }

        auto &step = steps->at(*idx);
        (*idx)++;

        // --- Header ---
        if (step.action.startsWith("header:")) {
            log("#00ffcc", step.action.mid(7));
            (*run)();
            return;
        }

        // --- K-Line TCM switch ---
        if (step.action == "switch:tcm") {
            m_tcm->switchToModule(WJDiagnostics::Module::KLineTCM, [this, log, run](bool ok) {
                log(ok ? "#60b8a0" : "#ff3333", ok ? "TCM K-Line session OK" : "TCM K-Line FAIL");
                (*run)();
            });
            return;
        }

        // --- K-Line ECU switch ---
        if (step.action == "switch:ecu") {
            m_tcm->switchToModule(WJDiagnostics::Module::MotorECU, [this, log, run](bool ok) {
                log(ok ? "#60b8a0" : "#ff3333", ok ? "ECU K-Line session OK" : "ECU K-Line FAIL");
                (*run)();
            });
            return;
        }

        // --- ECU Security Test (Phase 3) ---
        if (step.action == "switch:ecu_sectest") {
            // EDC15C2 security: NRC 0x12 = subFunctionNotSupported (format error)
            // Need to find correct seed level + key format
            struct SecAttempt {
                const char* label;
                QString seedCmd;    // "27 01" or "27 03" or "27 41"
                uint8_t keyLevel;   // 0x02, 0x04, 0x42
                bool twoByteKey;    // true=2-byte key, false=4-byte key
                uint32_t xor1, xor2, magic;
            };
            auto attempts = std::make_shared<QVector<SecAttempt>>(QVector<SecAttempt>{
                // Level 01/02, 2-byte key
                {"Lvl1 2B EDC16",    "27 01", 0x02, true,  0x1289, 0x0A22, 0x1C60020},
                {"Lvl1 2B EDC15P",   "27 01", 0x02, true,  0xDA1C, 0xF781, 0x3800000},
                {"Lvl1 2B EDC15V",   "27 01", 0x02, true,  0x508D, 0xA647, 0x3800000},
                {"Lvl1 2B EDC15VM+", "27 01", 0x02, true,  0xF25E, 0x6533, 0x3800000},
                // Level 01/02, 4-byte key
                {"Lvl1 4B EDC16",    "27 01", 0x02, false, 0x1289, 0x0A22, 0x1C60020},
                {"Lvl1 4B EDC15P",   "27 01", 0x02, false, 0xDA1C, 0xF781, 0x3800000},
                // Level 03/04
                {"Lvl3 2B EDC16",    "27 03", 0x04, true,  0x1289, 0x0A22, 0x1C60020},
                {"Lvl3 4B EDC16",    "27 03", 0x04, false, 0x1289, 0x0A22, 0x1C60020},
                // Level 41/42 (Bosch bootloader)
                {"Lvl41 4B EDC15P",  "27 41", 0x42, false, 0xDA1C, 0xF781, 0x3800000},
                {"Lvl41 4B EDC15V",  "27 41", 0x42, false, 0x508D, 0xA647, 0x3800000},
                {"Lvl41 4B EDC15VM+","27 41", 0x42, false, 0xF25E, 0x6533, 0x3800000},
                {"Lvl41 4B EDC16",   "27 41", 0x42, false, 0x1289, 0x0A22, 0x1C60020},
            });
            auto aIdx = std::make_shared<int>(0);
            auto tryAttempt = std::make_shared<std::function<void()>>();

            // Helper: read protected blocks after successful unlock
            auto readProtected = [this, log, run]() {
                m_elm->sendCommand("21 62", [this, log, run](const QString &r) {
                log("#00ff88", "ECU 0x62: " + r.trimmed());
                m_elm->sendCommand("21 B0", [this, log, run](const QString &r) {
                log("#00ff88", "ECU 0xB0: " + r.trimmed());
                m_elm->sendCommand("21 B1", [this, log, run](const QString &r) {
                log("#00ff88", "ECU 0xB1: " + r.trimmed());
                m_elm->sendCommand("21 B2", [this, log, run](const QString &r) {
                log("#00ff88", "ECU 0xB2: " + r.trimmed());
                m_elm->sendCommand("1A 90", [this, log, run](const QString &r) {
                log("#00ff88", "ECU VIN: " + r.trimmed());
                (*run)();
                });});});});});
            };

            *tryAttempt = [this, attempts, aIdx, tryAttempt, log, run, readProtected]() {
                if (*aIdx >= attempts->size()) {
                    log("#ff8800", "ECU security: all attempts exhausted");
                    (*run)();
                    return;
                }
                auto &a = attempts->at(*aIdx);
                log("#c0c0ff", QString("[%1/%2] %3 (seed=%4, keyLvl=0x%5)")
                    .arg(*aIdx + 1).arg(attempts->size()).arg(a.label)
                    .arg(a.seedCmd).arg(a.keyLevel, 2, 16, QChar('0')));

                // Full K-Line reinit for each attempt
                m_elm->sendCommand("ATZ", [this, attempts, aIdx, tryAttempt, log, run, a, readProtected](const QString &) {
                QTimer::singleShot(500, this, [this, attempts, aIdx, tryAttempt, log, run, a, readProtected]() {
                m_elm->sendCommand("ATE1", [this, attempts, aIdx, tryAttempt, log, run, a, readProtected](const QString &) {
                m_elm->sendCommand("ATH1", [this, attempts, aIdx, tryAttempt, log, run, a, readProtected](const QString &) {
                m_elm->sendCommand("ATWM8115F13E", [this, attempts, aIdx, tryAttempt, log, run, a, readProtected](const QString &) {
                m_elm->sendCommand("ATSH8115F1", [this, attempts, aIdx, tryAttempt, log, run, a, readProtected](const QString &) {
                m_elm->sendCommand("ATSP5", [this, attempts, aIdx, tryAttempt, log, run, a, readProtected](const QString &) {
                m_elm->sendCommand("ATFI", [this, attempts, aIdx, tryAttempt, log, run, a, readProtected](const QString &fi) {
                    // Accept "BUS INIT: ...OK" or just "OK"
                    bool atfiOk = fi.contains("OK", Qt::CaseInsensitive) ||
                                  fi.contains("BUS INIT", Qt::CaseInsensitive);
                    if (!atfiOk || fi.contains("ERROR", Qt::CaseInsensitive)) {
                        log("#ff8800", "ATFI fail: " + fi.trimmed());
                        (*aIdx)++;
                        QTimer::singleShot(2000, this, [tryAttempt]() { (*tryAttempt)(); });
                        return;
                    }
                    log("#60b8a0", "ATFI: " + fi.trimmed());
                m_elm->sendCommand("81", [this, attempts, aIdx, tryAttempt, log, run, a, readProtected](const QString &sc) {
                    log("#60b8a0", "StartComm: " + sc.trimmed());
                m_elm->sendCommand(a.seedCmd, [this, attempts, aIdx, tryAttempt, log, run, a, readProtected](const QString &seedResp) {
                    log("#60b8a0", "Seed resp: " + seedResp.trimmed());
                    QStringList parts = seedResp.trimmed().split(' ');

                    // Find "67" positive response
                    bool hasSeed = false;
                    int seedStart = -1;
                    for (int i = 0; i < parts.size(); i++) {
                        if (parts[i].compare("67", Qt::CaseInsensitive) == 0) {
                            hasSeed = true; seedStart = i; break;
                        }
                    }

                    if (hasSeed && seedStart + 3 < parts.size()) {
                        bool ok1, ok2;
                        uint8_t s0 = parts[seedStart + 2].toUInt(&ok1, 16);
                        uint8_t s1 = parts[seedStart + 3].toUInt(&ok2, 16);
                        if (ok1 && ok2) {
                            // Log raw seed for analysis
                            log("#c080ff", QString("Raw seed: %1 %2 (0x%3)")
                                .arg(s0, 2, 16, QChar('0')).arg(s1, 2, 16, QChar('0'))
                                .arg((s0<<8)|s1, 4, 16, QChar('0')));

                            // EDC15 ProcessKey algorithm
                            uint32_t KR1 = (s0 << 8) | s1;
                            uint32_t KR2 = 0;
                            uint32_t Key3 = a.magic;
                            for (int i = 0; i < 5; i++) {
                                uint32_t KeyTemp = KR1 & 0x8000;
                                KR1 = (KR1 << 1) & 0xFFFFFFFF;
                                if ((KeyTemp & 0x0FFFF) == 0) {
                                    uint32_t t2 = KR2 & 0xFFFF;
                                    KeyTemp = t2 + (KeyTemp & 0xFFFF0000);
                                    KR1 &= 0xFFFE;
                                    t2 = (KeyTemp & 0xFFFF) >> 0x0F;
                                    KeyTemp = (KeyTemp & 0xFFFF0000) + t2;
                                    KR1 |= KeyTemp;
                                    KR2 = (KR2 << 1) & 0xFFFFFFFF;
                                } else {
                                    KeyTemp = (KR2 + KR2) & 0xFFFFFFFF;
                                    KR1 &= 0xFFFE;
                                    uint32_t t2 = (KeyTemp & 0xFF) | 1;
                                    Key3 = (t2 + (Key3 & 0xFFFFFF00)) & 0xFFFFFFFF;
                                    Key3 = (Key3 & 0xFFFF00FF) | KeyTemp;
                                    t2 = (KR2 & 0xFFFF) >> 0x0F;
                                    KeyTemp = (t2 + (KeyTemp & 0xFFFF0000)) | KR1;
                                    Key3 = (Key3 ^ a.xor1) & 0xFFFFFFFF;
                                    KeyTemp = (KeyTemp ^ a.xor2) & 0xFFFFFFFF;
                                    KR2 = Key3;
                                    KR1 = KeyTemp;
                                }
                            }
                            KR1 &= 0xFFFF;
                            KR2 &= 0xFFFF;

                            QString keyCmd;
                            if (a.twoByteKey) {
                                keyCmd = QString("27 %1 %2 %3")
                                    .arg(a.keyLevel, 2, 16, QChar('0'))
                                    .arg((KR1>>8)&0xFF, 2, 16, QChar('0'))
                                    .arg(KR1&0xFF, 2, 16, QChar('0')).toUpper();
                            } else {
                                keyCmd = QString("27 %1 %2 %3 %4 %5")
                                    .arg(a.keyLevel, 2, 16, QChar('0'))
                                    .arg((KR1>>8)&0xFF, 2, 16, QChar('0'))
                                    .arg(KR1&0xFF, 2, 16, QChar('0'))
                                    .arg((KR2>>8)&0xFF, 2, 16, QChar('0'))
                                    .arg(KR2&0xFF, 2, 16, QChar('0')).toUpper();
                            }
                            log("#c080ff", QString("Computed key: %1").arg(keyCmd));

                            m_elm->sendCommand(keyCmd, [this, attempts, aIdx, tryAttempt, log, run, readProtected](const QString &keyResp) {
                                // Check for positive response
                                if (keyResp.contains("67", Qt::CaseInsensitive) &&
                                    !keyResp.contains("7F", Qt::CaseInsensitive)) {
                                    log("#00ff00", QString("*** ECU UNLOCKED with %1! ***")
                                        .arg(attempts->at(*aIdx).label));
                                    readProtected();
                                    return;
                                }
                                // Extract NRC for logging
                                QString nrc;
                                QStringList rp = keyResp.trimmed().split(' ');
                                for (int i = 0; i < rp.size()-1; i++) {
                                    if (rp[i].compare("7F", Qt::CaseInsensitive) == 0 &&
                                        rp[i+1].compare("27", Qt::CaseInsensitive) == 0 &&
                                        i+2 < rp.size()) {
                                        nrc = rp[i+2];
                                    }
                                }
                                log("#ff8800", QString("%1 -> NRC 0x%2: %3")
                                    .arg(attempts->at(*aIdx).label, nrc, keyResp.trimmed()));
                                (*aIdx)++;
                                int delay = (nrc.compare("37", Qt::CaseInsensitive) == 0) ? 10000 : 2000;
                                QTimer::singleShot(delay, this, [tryAttempt]() { (*tryAttempt)(); });
                            });
                        } else {
                            log("#ff8800", "Seed parse fail");
                            (*aIdx)++;
                            QTimer::singleShot(1000, this, [tryAttempt]() { (*tryAttempt)(); });
                        }
                    } else {
                        // No seed - level not supported, skip all with same seedCmd
                        log("#ff8800", "No seed (level not supported): " + seedResp.trimmed());
                        QString skipCmd = a.seedCmd;
                        while (*aIdx < attempts->size() && attempts->at(*aIdx).seedCmd == skipCmd)
                            (*aIdx)++;
                        QTimer::singleShot(1000, this, [tryAttempt]() { (*tryAttempt)(); });
                    }
                });
                });
                });
                });
                });
                });
                });
                });
                });
                });
            };
            (*tryAttempt)();
            return;
        }

        // --- J1850 switch ---
        if (step.action == "switch:j1850") {
            m_elm->sendCommand("ATZ", [this, log, run](const QString &) {
            m_elm->sendCommand("ATE1", [this, log, run](const QString &) {
            m_elm->sendCommand("ATH1", [this, log, run](const QString &) {
            m_elm->sendCommand("ATIFR0", [this, log, run](const QString &) {
            m_elm->sendCommand("ATSP2", [this, log, run](const QString &) {
            m_elm->sendCommand("ATSH244022", [this, log, run](const QString &) {
            m_elm->sendCommand("ATRA40", [this, log, run](const QString &) {
                log("#60b8a0", "J1850 VPW ready (ABS header)");
                (*run)();
            });});});});});});});
            return;
        }

        // --- J1850 header change ---
        if (step.action.startsWith("j1850hdr:")) {
            QString hdr = step.action.mid(9);
            m_elm->sendCommand(hdr, [run](const QString &) {
                (*run)();
            });
            return;
        }

        // --- J1850 command ---
        if (step.action.startsWith("j1850cmd:")) {
            QString cmd = step.action.mid(9);
            m_elm->sendCommand(cmd, [this, log, run, step](const QString &resp) {
                bool noData = resp.contains("NO DATA") || resp.contains("ERROR") ||
                              resp.contains("UNABLE") || resp.contains("?") || resp.contains("TIMEOUT");
                log(noData ? "#666666" : "#00ff88", step.label + ": " + resp.trimmed());
                (*run)();
            }, 1500);
            return;
        }

        // --- K-Line command ---
        if (step.action.startsWith("cmd:")) {
            QString cmd = step.action.mid(4);
            m_elm->sendCommand(cmd, [this, log, run, step](const QString &resp) {
                bool noData = resp.contains("NO DATA") || resp.contains("ERROR") ||
                              resp.contains("7F") || resp.contains("TIMEOUT");
                log(noData ? "#666666" : "#00ff88", step.label + ": " + resp.trimmed());
                (*run)();
            });
            return;
        }

        // Unknown action, skip
        (*run)();
    };

    (*run)();
}

void MainWindow::onRawSendCustom()
{
    QString cmd = m_rawCmdEdit->text().trimmed();
    if (cmd.isEmpty()) return;
    m_rawCmdEdit->clear();
    if (cmd.startsWith("AT", Qt::CaseInsensitive)) {
        m_elm->sendCommand(cmd, [](const QString &) {});
    } else {
        QString hexClean = cmd.remove(' ');
        m_elm->sendCommand(hexClean, [](const QString &) {});
    }
}

// --- Bluetooth scan helper ---

void MainWindow::scanBluetoothDevices()
{
    m_btConnectBtn->setEnabled(false);
    m_connStatusLabel->setText("Scanning BLE...");
    m_connStatusLabel->setStyleSheet("color:orange;font-weight:bold;font-size:13px;padding:2px;");
    m_elm->scanBluetooth();
}

// --- Bluetooth permissions (Android/iOS) ---

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
void MainWindow::requestBluetoothPermissions()
{
    // Android: request location first, then bluetooth, then scan
    // Sequential to avoid QtAndroidAccessibility deadlock
#ifdef Q_OS_ANDROID
    QLocationPermission locationPermission;
    locationPermission.setAccuracy(QLocationPermission::Approximate);

    if (qApp->checkPermission(locationPermission) == Qt::PermissionStatus::Undetermined) {
        qApp->requestPermission(locationPermission, this,
            [this](const QPermission &) {
                // After location, request bluetooth
                QTimer::singleShot(300, this, [this]() {
                    requestBluetoothPermissionOnly();
                });
            });
        return;
    }
    requestBluetoothPermissionOnly();
#else
    requestBluetoothPermissionOnly();
#endif
}

void MainWindow::requestBluetoothPermissionOnly()
{
    QBluetoothPermission bluetoothPermission;
    bluetoothPermission.setCommunicationModes(QBluetoothPermission::Access);

    switch (qApp->checkPermission(bluetoothPermission)) {
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(bluetoothPermission, this,
            [this](const QPermission &permission) {
                if (qApp->checkPermission(permission) == Qt::PermissionStatus::Granted) {
                    QTimer::singleShot(200, this, [this]() { scanBluetoothDevices(); });
                } else {
                    onLogMessage("Bluetooth permission denied. Cannot scan.");
                }
            });
        break;
    case Qt::PermissionStatus::Granted:
        scanBluetoothDevices();
        break;
    case Qt::PermissionStatus::Denied:
        onLogMessage("Bluetooth permission denied. Please enable in Settings.");
        break;
    }
}
#endif
