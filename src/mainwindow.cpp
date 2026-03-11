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
#include <QPointer>
#include <QRegularExpression>

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
    m_tabs->addTab(createControlsTab(),   "Controls");
    m_tabs->addTab(createLogTab(),        "Log");

    mainLayout->addWidget(m_tabs);
    setCentralWidget(central);

    // Status bar
    statusBar()->showMessage("Waiting for connection...");
}


// ================================================================
// Dashboard - module-dependent gauge layout
// TCM: big gear + surrounding TCM gauges
// ECU: engine-specific gauges (no TCM)
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
    m_dashStack = new QWidget();
    m_dashLayout = new QVBoxLayout(m_dashStack);
    m_dashLayout->setContentsMargins(0,0,0,0);
    m_dashLayout->setSpacing(0);
    rebuildDashboard();
    return m_dashStack;
}

void MainWindow::rebuildDashboard()
{
    // Clear old dashboard
    QLayoutItem *item;
    while ((item = m_dashLayout->takeAt(0)) != nullptr) {
        if (item->widget()) { item->widget()->deleteLater(); }
        delete item;
    }

    // Null all gauge pointers
    m_dashGearVal=m_dashGearUnit=nullptr;
    m_dashSpeedVal=m_dashSpeedUnit=nullptr;
    m_dashRpmVal=m_dashRpmUnit=nullptr;
    m_dashCoolantVal=m_dashCoolantUnit=nullptr;
    m_dashSolVoltVal=m_dashSolVoltUnit=nullptr;
    m_dashBatVoltVal=m_dashBatVoltUnit=nullptr;
    m_dashMotCoolVal=m_dashMotCoolUnit=nullptr;
    m_dashLimpVal=m_dashLimpUnit=nullptr;
    m_dashMotRpmVal=m_dashMotRpmUnit=nullptr;
    m_dashMotBoostVal=m_dashMotBoostUnit=nullptr;
    m_dashMotMafVal=m_dashMotMafUnit=nullptr;
    m_dashMotRailVal=m_dashMotRailUnit=nullptr;
    m_dashEgrVal=m_dashEgrUnit=nullptr;
    m_dashWgVal=m_dashWgUnit=nullptr;
    m_dashInjAdaptVal=m_dashInjAdaptUnit=nullptr;
    m_dashFuelAdaptVal=m_dashFuelAdaptUnit=nullptr;
    m_dashBoostAdaptVal=m_dashBoostAdaptUnit=nullptr;
    m_dashOilPressVal=m_dashOilPressUnit=nullptr;

    bool isTCM = (m_moduleSessionActive && m_activeModId == WJDiagnostics::Module::KLineTCM);
    bool isECU = (m_moduleSessionActive && m_activeModId == WJDiagnostics::Module::MotorECU)
                 || !m_moduleSessionActive;  // default = ECU layout

    QWidget *panel = new QWidget();
    QGridLayout *g = new QGridLayout(panel);
    g->setContentsMargins(2,2,2,2);
    g->setSpacing(3);

    if (isTCM) {
        // === TCM DASHBOARD: Big gear center, surrounding gauges ===
        // Row 0: Speed | GEAR(2x2) | Turbine RPM
        g->addWidget(createGaugeCard("SPEED", "---", "km/h", &m_dashSpeedVal, &m_dashSpeedUnit), 0, 0);

        // Big gear display (spans 2 rows, 2 cols)
        QFrame *gearCard = new QFrame();
        gearCard->setFrameShape(QFrame::StyledPanel);
        gearCard->setStyleSheet("QFrame{background:#0a1420;border:2px solid #00806a;border-radius:10px;}");
        QVBoxLayout *gl = new QVBoxLayout(gearCard);
        gl->setContentsMargins(4,2,4,2); gl->setSpacing(0);
        QLabel *gt = new QLabel("GEAR");
        gt->setAlignment(Qt::AlignCenter);
        gt->setStyleSheet("color:#5888a8;font-size:12px;border:none;background:transparent;");
        gl->addWidget(gt);
        m_dashGearVal = new QLabel("---");
        m_dashGearVal->setAlignment(Qt::AlignCenter);
        m_dashGearVal->setStyleSheet("color:#00ffa0;font-size:72px;font-weight:bold;"
            "font-family:'Consolas','Courier New',monospace;border:none;background:transparent;");
        gl->addWidget(m_dashGearVal);
        m_dashGearUnit = new QLabel("");
        m_dashGearUnit->setAlignment(Qt::AlignCenter);
        m_dashGearUnit->setStyleSheet("color:#406888;font-size:10px;border:none;background:transparent;");
        gl->addWidget(m_dashGearUnit);
        g->addWidget(gearCard, 0, 1, 2, 2);

        g->addWidget(createGaugeCard("TURBIN", "---", "rpm", &m_dashRpmVal, &m_dashRpmUnit), 0, 3);

        // Row 1: T-Temp | (gear spans) | Limp
        g->addWidget(createGaugeCard("T-TEMP", "---", "C", &m_dashCoolantVal, &m_dashCoolantUnit), 1, 0);
        g->addWidget(createGaugeCard("LIMP", "---", "", &m_dashLimpVal, &m_dashLimpUnit), 1, 3);

        // Row 2: LinePr | TCC-Slip | Sol-V | Batt
        g->addWidget(createGaugeCard("LINE-P", "---", "mbar", &m_dashMotBoostVal, &m_dashMotBoostUnit), 2, 0);
        g->addWidget(createGaugeCard("TCC", "---", "rpm", &m_dashMotMafVal, &m_dashMotMafUnit), 2, 1);
        g->addWidget(createGaugeCard("SOL V", "---", "V", &m_dashSolVoltVal, &m_dashSolVoltUnit), 2, 2);
        g->addWidget(createGaugeCard("BATT", "---", "V", &m_dashBatVoltVal, &m_dashBatVoltUnit), 2, 3);

        for(int c=0; c<4; ++c) g->setColumnStretch(c, 1);

    } else if (isECU) {
        // === ECU DASHBOARD: Big FUEL center, engine gauges around ===
        // Layout (4 cols):
        //   Row 0:  RPM     | FUEL(2x2)      | INJ-Q
        //   Row 1:  BOOST   | FUEL(cont)     | RAIL
        //   Row 2:  M-TEMP  | MAF  | TPS     | BATT

        // Row 0
        g->addWidget(createGaugeCard("RPM", "---", "rpm", &m_dashMotRpmVal, &m_dashMotRpmUnit), 0, 0);

        // Big FUEL display (spans 2 rows, 2 cols) — like TCM gear
        QFrame *fuelCard = new QFrame();
        fuelCard->setFrameShape(QFrame::StyledPanel);
        fuelCard->setStyleSheet("QFrame{background:#0a1420;border:2px solid #00806a;border-radius:10px;}");
        QVBoxLayout *fl = new QVBoxLayout(fuelCard);
        fl->setContentsMargins(4,2,4,2); fl->setSpacing(0);
        QLabel *ft = new QLabel("FUEL");
        ft->setAlignment(Qt::AlignCenter);
        ft->setStyleSheet("color:#5888a8;font-size:12px;border:none;background:transparent;");
        fl->addWidget(ft);
        m_dashFuelAdaptVal = new QLabel("---");
        m_dashFuelAdaptVal->setAlignment(Qt::AlignCenter);
        m_dashFuelAdaptVal->setStyleSheet("color:#00ffa0;font-size:72px;font-weight:bold;"
            "font-family:'Consolas','Courier New',monospace;border:none;background:transparent;");
        fl->addWidget(m_dashFuelAdaptVal);
        m_dashFuelAdaptUnit = new QLabel("L/h");
        m_dashFuelAdaptUnit->setAlignment(Qt::AlignCenter);
        m_dashFuelAdaptUnit->setStyleSheet("color:#406888;font-size:12px;border:none;background:transparent;");
        fl->addWidget(m_dashFuelAdaptUnit);
        g->addWidget(fuelCard, 0, 1, 2, 2);

        g->addWidget(createGaugeCard("INJ-Q", "---", "mg/str", &m_dashSpeedVal, &m_dashSpeedUnit), 0, 3);

        // Row 1
        g->addWidget(createGaugeCard("BOOST", "---", "mbar", &m_dashMotBoostVal, &m_dashMotBoostUnit), 1, 0);
        // FUEL spans here
        g->addWidget(createGaugeCard("RAIL", "---", "bar", &m_dashMotRailVal, &m_dashMotRailUnit), 1, 3);

        // Row 2: M-Temp | MAF | TPS | Batt
        g->addWidget(createGaugeCard("M-TEMP", "---", "C", &m_dashMotCoolVal, &m_dashMotCoolUnit), 2, 0);
        g->addWidget(createGaugeCard("MAF", "---", "mg/s", &m_dashMotMafVal, &m_dashMotMafUnit), 2, 1);
        g->addWidget(createGaugeCard("TPS", "---", "%", &m_dashLimpVal, &m_dashLimpUnit), 2, 2);
        g->addWidget(createGaugeCard("BATT", "---", "V", &m_dashBatVoltVal, &m_dashBatVoltUnit), 2, 3);

        for(int c=0; c<4; ++c) g->setColumnStretch(c, 1);

    } else {
        // === DEFAULT: Mixed dashboard (no module active) ===
        g->addWidget(createGaugeCard("SPEED", "---", "km/h", &m_dashSpeedVal, &m_dashSpeedUnit), 0, 0);
        g->addWidget(createGaugeCard("GEAR", "---", "", &m_dashGearVal, &m_dashGearUnit), 0, 1);
        g->addWidget(createGaugeCard("RPM", "---", "rpm", &m_dashMotRpmVal, &m_dashMotRpmUnit), 0, 2);
        g->addWidget(createGaugeCard("TURBIN", "---", "rpm", &m_dashRpmVal, &m_dashRpmUnit), 0, 3);

        g->addWidget(createGaugeCard("BOOST", "---", "mbar", &m_dashMotBoostVal, &m_dashMotBoostUnit), 1, 0);
        g->addWidget(createGaugeCard("MAF", "---", "mg/s", &m_dashMotMafVal, &m_dashMotMafUnit), 1, 1);
        g->addWidget(createGaugeCard("RAIL", "---", "bar", &m_dashMotRailVal, &m_dashMotRailUnit), 1, 2);
        g->addWidget(createGaugeCard("LIMP", "---", "", &m_dashLimpVal, &m_dashLimpUnit), 1, 3);

        g->addWidget(createGaugeCard("M-TEMP", "---", "C", &m_dashMotCoolVal, &m_dashMotCoolUnit), 2, 0);
        g->addWidget(createGaugeCard("T-TEMP", "---", "C", &m_dashCoolantVal, &m_dashCoolantUnit), 2, 1);
        g->addWidget(createGaugeCard("BATT", "---", "V", &m_dashBatVoltVal, &m_dashBatVoltUnit), 2, 2);
        g->addWidget(createGaugeCard("SOL V", "---", "V", &m_dashSolVoltVal, &m_dashSolVoltUnit), 2, 3);

        for(int c=0; c<4; ++c) g->setColumnStretch(c, 1);
    }

    m_dashLayout->addWidget(panel);
}

void MainWindow::setGaugeColor(QLabel *vl, const QString &c) {
    if (!vl) return;
    // Only change color, preserve existing font size/weight
    QPalette pal = vl->palette();
    pal.setColor(QPalette::WindowText, QColor(c));
    vl->setPalette(pal);
    // Stylesheet approach: extract current font-size from stylesheet, only change color
    QString ss = vl->styleSheet();
    // Replace color value in existing stylesheet
    static QRegularExpression colorRx("color:#[0-9a-fA-F]{6}");
    if (ss.contains(colorRx)) {
        ss.replace(colorRx, QString("color:%1").arg(c));
    } else {
        ss.prepend(QString("color:%1;").arg(c));
    }
    vl->setStyleSheet(ss);
}

void MainWindow::updateDashboardFromLiveData(const QMap<uint8_t, double> &v)
{
    // Null-safe macro: only update if label exists
    #define DASH_SET(label, text) if(label) label->setText(text)
    #define DASH_COLOR(label, color) if(label) setGaugeColor(label, color)

    // TCM data
    if(v.contains(0x01) && m_dashGearVal){
        int g=(int)v[0x01];
        DASH_SET(m_dashGearVal, gearToString((TCMDiagnostics::Gear)g));
        DASH_COLOR(m_dashGearVal, g>=3 ? "#00ffa0" : "#d0a040");
    }
    if(v.contains(0x20)) DASH_SET(m_dashSpeedVal, QString::number(v[0x20],'f',0));
    if(v.contains(0x10)) DASH_SET(m_dashRpmVal, QString::number(v[0x10],'f',0));
    if(v.contains(0x16) && m_dashSolVoltVal){
        double sv=v[0x16];
        DASH_SET(m_dashSolVoltVal, QString::number(sv,'f',1));
        DASH_COLOR(m_dashSolVoltVal, sv<9.0?"#e04040":sv<11.0?"#d09030":"#00d4b4");
    }
    if(v.contains(0x03) && m_dashLimpVal){
        bool isTCM = (m_activeModId == WJDiagnostics::Module::KLineTCM);
        if (isTCM) {
            bool l = v[0x03] <= 2 && v.value(0x14, 0) > 100;
            DASH_SET(m_dashLimpVal, l?"ACTIVE!":"Normal");
            DASH_COLOR(m_dashLimpVal, l?"#e04040":"#00d4b4");
        }
    }
    if(v.contains(0x14) && m_dashCoolantVal){
        double ct=v[0x14];
        DASH_SET(m_dashCoolantVal, QString::number(ct,'f',0));
        DASH_COLOR(m_dashCoolantVal, ct>105?"#e04040":ct>95?"#d09030":"#00d4b4");
    }
    // TCM: line pressure -> m_dashMotBoostVal (reused in TCM mode)
    if(v.contains(0x15) && m_dashMotBoostVal && m_activeModId == WJDiagnostics::Module::KLineTCM){
        DASH_SET(m_dashMotBoostVal, QString::number(v[0x15],'f',0));
    }
    // TCM: TCC slip actual -> m_dashMotMafVal (reused in TCM mode)
    if(v.contains(0x18) && m_dashMotMafVal && m_activeModId == WJDiagnostics::Module::KLineTCM){
        DASH_SET(m_dashMotMafVal, QString::number(v[0x18],'f',0));
    }

    // ECU data
    if(v.contains(0xE0) && m_dashMotCoolVal){
        double ct = v[0xE0];
        DASH_SET(m_dashMotCoolVal, QString::number(ct,'f',0));
        DASH_COLOR(m_dashMotCoolVal, ct > 105 ? "#e04040" : ct > 95 ? "#d09030" : "#00d4b4");
    }

    #undef DASH_SET
    #undef DASH_COLOR
}

QWidget* MainWindow::createConnectionTab()
{
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

    // === Module List with scroll ===
    QGroupBox *modBox = new QGroupBox("Modules");
    modBox->setStyleSheet("QGroupBox{font-weight:bold;color:#70C8F0;font-size:14px;}");
    modBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    QVBoxLayout *modBoxLayout = new QVBoxLayout(modBox);
    modBoxLayout->setContentsMargins(2,2,2,2);
    m_modScroll = new QScrollArea();
    m_modScroll->setWidgetResizable(true);
    m_modScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_modScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_modScroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_modScroll->setStyleSheet("QScrollArea{border:none;background:transparent;}"
        "QScrollBar:vertical{background:#0a0e14;width:6px;border-radius:3px;}"
        "QScrollBar::handle:vertical{background:#1a4060;border-radius:3px;min-height:30px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}");
    QWidget *modListWidget = new QWidget();
    m_moduleListLayout = new QVBoxLayout(modListWidget);
    m_moduleListLayout->setSpacing(3);
    m_moduleListLayout->setContentsMargins(0,0,0,0);

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
        {WJDiagnostics::Module::ATC, "Climate (HVAC)", "J1850 VPW | ATSH246822", true},
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
                rebuildDashboard();
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
                // Reset scroll to top
                if (m_modScroll) m_modScroll->verticalScrollBar()->setValue(0);

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
                    rebuildDashboard();

                    statusBar()->showMessage(me.label + " active");
                } else {
                    btn->setChecked(false);
                    m_moduleSessionActive = false;
                    rebuildDashboard();
                    statusBar()->showMessage(me.label + " connection failed!");
                }
                updateActiveHeaderLabel();
            });
        });

        m_moduleButtons.append(btn);
        m_moduleListLayout->addWidget(btn);
    }

    m_moduleListLayout->addStretch();
    m_modScroll->setWidget(modListWidget);
    modBoxLayout->addWidget(m_modScroll);
    layout->addWidget(modBox, 1); // stretch factor 1 = take remaining space

    // Active Header
    m_activeHeaderLabel = new QLabel("---");
    m_activeHeaderLabel->setStyleSheet("background:#0e1828;padding:4px;border-radius:4px;"
                                       "color:#80a8c0;font-family:monospace;font-weight:bold;font-size:10px;");
    m_activeHeaderLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_activeHeaderLabel);

    // Enable touch scrolling on module list
    QScroller::grabGesture(m_modScroll->viewport(), QScroller::LeftMouseButtonGesture);

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

    return w;
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

    // === Buttons ===
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


// ================================================================
// Controls Tab — Window relay controls via J1850 DriverDoor module
// ================================================================
// Chrysler WJ window control:
//   J1850 VPW, DriverDoor module 0x40, mode 0x2F (IOControlByLocalIdentifier)
//   Header: ATSH24402F, ATRA40
//   Front Window UP:   38 07 01 (ON) / 38 07 00 (OFF)
//   Front Window DOWN: 38 08 01 (ON) / 38 08 00 (OFF)
//   Rear Window UP:    38 0C 02 (ON) / 38 0C 00 (OFF)
//   Rear Window DOWN:  38 08 02 (ON) / 38 08 00 (OFF)
//   Release all:       3A 02 FF

void MainWindow::sendWindowCmd(const QString &label, const QString &relayCmd, bool on, const QString &hdr)
{
    if (!m_elm || !m_elm->isConnected()) {
        if (m_ctrlStatusLabel) m_ctrlStatusLabel->setText("Not connected");
        return;
    }
    if (m_ctrlStatusLabel) m_ctrlStatusLabel->setText(label + (on ? " ON..." : " OFF..."));

    auto sendRelay = [this, label, relayCmd, on]() {
        m_elm->sendCommand(relayCmd, [this, label, on](const QString &resp) {
            bool ok = !resp.contains("NO DATA") && !resp.contains("ERROR");
            QString status = label + (on ? " ON: " : " OFF: ") + (ok ? "OK" : "FAIL");
            if (m_ctrlStatusLabel) m_ctrlStatusLabel->setText(status);
            onLogMessage(status + " -> " + resp.trimmed());
        }, 2000);
    };

    // Fast path: header already set + session active, just send relay command
    if (m_ctrlActiveHdr == hdr) {
        sendRelay();
        return;
    }

    m_ctrlActiveHdr = hdr;
    QString targetHex = hdr.mid(6, 2);  // "40", "A0"
    QString sessionHdr = "ATSH24" + targetHex + "11";  // mode 0x11 = DiagSession
    QString atra = "ATRA" + targetHex;

    // Full init: ATSP2 -> DiagSession(0x11) -> ATRA -> 01 01 00 -> IOControl(0x2F) -> relay
    m_elm->sendCommand("ATSP2", [this, sessionHdr, atra, hdr, sendRelay](const QString &) {
    m_elm->sendCommand(sessionHdr, [this, atra, hdr, sendRelay](const QString &) {
    m_elm->sendCommand(atra, [this, hdr, sendRelay](const QString &) {
    m_elm->sendCommand("01 01 00", [this, hdr, sendRelay](const QString &resp) {
        onLogMessage("DiagSession: " + resp.trimmed());
    m_elm->sendCommand(hdr, [sendRelay](const QString &) {
        sendRelay();
    });});});});});
}

QWidget* MainWindow::createControlsTab()
{
    QWidget *w = new QWidget();
    QVBoxLayout *lay = new QVBoxLayout(w);
    lay->setContentsMargins(6,4,6,4);
    lay->setSpacing(6);

    m_ctrlStatusLabel = new QLabel("Ready");
    m_ctrlStatusLabel->setAlignment(Qt::AlignCenter);
    m_ctrlStatusLabel->setStyleSheet("color:#aaaaaa;font-size:11px;padding:2px;background:#0a1420;border-radius:4px;");
    lay->addWidget(m_ctrlStatusLabel);

    auto makeHoldBtn = [this](const QString &text, const QString &onCmd, const QString &offCmd,
                              const QString &label, const QString &hdr) -> QPushButton* {
        QPushButton *btn = new QPushButton(text);
        btn->setMinimumHeight(60);
        btn->setStyleSheet(
            "QPushButton{background:#1a3050;color:#e0e0e0;border:1px solid #2a5070;"
            "border-radius:8px;font-size:14px;font-weight:bold;padding:8px;}"
            "QPushButton:pressed{background:#00806a;border-color:#00d4b4;}");
        connect(btn, &QPushButton::pressed, this, [this, label, onCmd, hdr]() {
            // Send first ON immediately
            sendWindowCmd(label, onCmd, true, hdr);
            // Start repeating ON every 400ms while held
            if (!m_ctrlRepeatTimer) {
                m_ctrlRepeatTimer = new QTimer(this);
                m_ctrlRepeatTimer->setTimerType(Qt::PreciseTimer);
            }
            m_ctrlRepeatTimer->disconnect();
            connect(m_ctrlRepeatTimer, &QTimer::timeout, this, [this, label, onCmd, hdr]() {
                sendWindowCmd(label, onCmd, true, hdr);
            });
            m_ctrlRepeatTimer->start(400);
        });
        connect(btn, &QPushButton::released, this, [this, label, offCmd, hdr]() {
            // Stop repeat timer
            if (m_ctrlRepeatTimer) m_ctrlRepeatTimer->stop();
            // Send OFF
            sendWindowCmd(label, offCmd, false, hdr);
        });
        return btn;
    };

    // All actuators use DriverDoor 0x40 (mode 0x2F) — VERIFIED on real vehicle
    QString hdrDD = "ATSH24402F";
    QString hdrPD = "ATSH24A02F";
    QString grpStyle = "QGroupBox{color:#5888a8;font-size:12px;border:1px solid #2a5070;"
                       "border-radius:6px;margin-top:6px;padding-top:14px;}";

    // ====== LEFT (DRIVER) WINDOW — DriverDoor 0x40 ======
    QGroupBox *leftGrp = new QGroupBox("Left (Driver)");
    leftGrp->setStyleSheet(grpStyle);
    QVBoxLayout *leftLay = new QVBoxLayout(leftGrp);
    leftLay->setSpacing(6); leftLay->setContentsMargins(4,4,4,4);
    leftLay->addWidget(makeHoldBtn("LEFT UP",   "38 07 01", "38 07 00", "Left Window Up", hdrDD));
    leftLay->addWidget(makeHoldBtn("LEFT DOWN", "38 08 01", "38 08 00", "Left Window Down", hdrDD));

    // Express-down: 300ms pulse
    QPushButton *autoDownBtn = new QPushButton("AUTO-DOWN");
    autoDownBtn->setMinimumHeight(48);
    autoDownBtn->setStyleSheet(
        "QPushButton{background:#1a3050;color:#ffcc44;border:1px solid #806020;"
        "border-radius:8px;font-size:13px;font-weight:bold;padding:6px;}"
        "QPushButton:pressed{background:#604010;border-color:#ffcc44;}");
    connect(autoDownBtn, &QPushButton::clicked, this, [this, hdrDD]() {
        sendWindowCmd("Left Auto-Down", "38 08 01", true, hdrDD);
        QTimer::singleShot(300, this, [this, hdrDD]() {
            sendWindowCmd("Left Auto-Down", "38 08 00", false, hdrDD);
        });
    });
    leftLay->addWidget(autoDownBtn);

    // ====== RIGHT (PASSENGER) WINDOW — PassengerDoor 0xA0 ======
    QGroupBox *rightGrp = new QGroupBox("Right (Passenger)");
    rightGrp->setStyleSheet(grpStyle);
    QVBoxLayout *rightLay = new QVBoxLayout(rightGrp);
    rightLay->setSpacing(6); rightLay->setContentsMargins(4,4,4,4);
    rightLay->addWidget(makeHoldBtn("RIGHT UP",   "38 01 12", "38 01 00", "Right Window Up", hdrPD));
    rightLay->addWidget(makeHoldBtn("RIGHT DOWN", "38 00 12", "38 00 00", "Right Window Down", hdrPD));

    QHBoxLayout *winRow = new QHBoxLayout();
    winRow->setSpacing(6);
    winRow->addWidget(leftGrp);
    winRow->addWidget(rightGrp);
    lay->addLayout(winRow);

    // ====== BODY CONTROLS ======
    QString hdrBCM = "ATSH24802F";
    QGroupBox *bodyGrp = new QGroupBox("Body Controls");
    bodyGrp->setStyleSheet(grpStyle);
    QGridLayout *bodyLay = new QGridLayout(bodyGrp);
    bodyLay->setSpacing(6); bodyLay->setContentsMargins(4,4,4,4);

    // LOCK — DriverDoor 0x40 (triggers hazard flash + horn chirp)
    QPushButton *lockBtn = new QPushButton("LOCK");
    lockBtn->setMinimumHeight(60);
    lockBtn->setStyleSheet(
        "QPushButton{background:#1a3050;color:#ff8844;border:1px solid #804020;"
        "border-radius:8px;font-size:14px;font-weight:bold;padding:8px;}"
        "QPushButton:pressed{background:#804020;border-color:#ff8844;}");
    connect(lockBtn, &QPushButton::clicked, this, [this, hdrDD]() {
        sendWindowCmd("Lock", "38 06 02", true, hdrDD);
        QTimer::singleShot(500, this, [this, hdrDD]() {
            sendWindowCmd("Lock", "38 06 00", false, hdrDD);
        });
    });

    // UNLOCK — DriverDoor 0x40
    QPushButton *unlockBtn = new QPushButton("UNLOCK");
    unlockBtn->setMinimumHeight(60);
    unlockBtn->setStyleSheet(
        "QPushButton{background:#1a3050;color:#00d4b4;border:1px solid #006050;"
        "border-radius:8px;font-size:14px;font-weight:bold;padding:8px;}"
        "QPushButton:pressed{background:#006050;border-color:#00d4b4;}");
    connect(unlockBtn, &QPushButton::clicked, this, [this, hdrDD]() {
        sendWindowCmd("Unlock", "3A 02 FF", true, hdrDD);
    });

    // HAZARD — BCM 0x80 mode 0x2F: 38 01 00 ON / 38 01 01 OFF
    QPushButton *hazardBtn = new QPushButton("HAZARD\nOFF");
    hazardBtn->setMinimumHeight(60);
    hazardBtn->setCheckable(true);
    hazardBtn->setStyleSheet(
        "QPushButton{background:#1a3050;color:#e0e0e0;border:1px solid #2a5070;"
        "border-radius:8px;font-size:13px;font-weight:bold;padding:8px;}"
        "QPushButton:checked{background:#802020;color:#ff4444;border-color:#ff4444;}");
    connect(hazardBtn, &QPushButton::toggled, this, [this, hazardBtn, hdrBCM](bool on) {
        hazardBtn->setText(on ? "HAZARD\nON" : "HAZARD\nOFF");
        sendWindowCmd("Hazard", on ? "38 01 00" : "38 01 01", on, hdrBCM);
    });

    // HORN — BCM 0x80 mode 0x2F: 38 00 CC ON / 38 00 00 OFF (hold)
    bodyLay->addWidget(makeHoldBtn("HORN", "38 00 CC", "38 00 00", "Horn", hdrBCM), 0, 1);

    bodyLay->addWidget(lockBtn, 0, 0);
    bodyLay->addWidget(unlockBtn, 1, 0);
    bodyLay->addWidget(hazardBtn, 1, 1);

    lay->addWidget(bodyGrp);

    lay->addStretch();
    return w;
}


QWidget* MainWindow::createLogTab()
{
    QWidget *w = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(w);
    layout->setSpacing(3);
#if defined(Q_OS_IOS)
    // iPhone safe area: extra bottom padding for home indicator
    layout->setContentsMargins(2, 2, 2, 20);
#elif defined(Q_OS_ANDROID)
    layout->setContentsMargins(2, 2, 2, 4);
#else
    layout->setContentsMargins(4, 4, 4, 4);
#endif

    // Log text: create FIRST (buttons reference it)
    m_logText = new QTextEdit();
    m_logText->setReadOnly(true);
    m_logText->setFont(QFont("Consolas", 9));
    m_logText->setStyleSheet(
        "background: #0a1220; color: #60b8a0; font-size: 12px;"
        "border: 1px solid #1a3050; border-radius: 4px;");

    // --- Controls: 2 rows, compact, ABOVE log text ---
    QGridLayout *logGrid = new QGridLayout();
    logGrid->setSpacing(3);
    logGrid->setContentsMargins(0, 0, 0, 0);

    // Row 0: Clear | Copy Log | Raw Data
    QPushButton *clearLogBtn = new QPushButton("Clear");
    clearLogBtn->setMinimumHeight(36);
    connect(clearLogBtn, &QPushButton::clicked, m_logText, &QTextEdit::clear);
    logGrid->addWidget(clearLogBtn, 0, 0);

    QPushButton *saveLogBtn = new QPushButton("Copy Log");
    saveLogBtn->setMinimumHeight(36);
    saveLogBtn->setStyleSheet("background:#0a2820; color:#00d4b4; font-weight:bold;");
    connect(saveLogBtn, &QPushButton::clicked, this, [this]() {
        QString text = m_logText->toPlainText();
        if (text.isEmpty()) {
            statusBar()->showMessage("Log empty");
            return;
        }
        QGuiApplication::clipboard()->setText(text);
        statusBar()->showMessage(QString("Copied %1 lines").arg(text.count('\n') + 1));
    });
    logGrid->addWidget(saveLogBtn, 0, 1);

    m_rawDumpBtn = new QPushButton("Raw Data");
    m_rawDumpBtn->setMinimumHeight(36);
    m_rawDumpBtn->setStyleSheet("background:#2a4858; color:#00ffcc; font-weight:bold;");
    connect(m_rawDumpBtn, &QPushButton::clicked, this, &MainWindow::onRawBusDump);
    logGrid->addWidget(m_rawDumpBtn, 0, 2);

    // Row 1: Command input + Send
    m_rawCmdEdit = new QLineEdit();
    m_rawCmdEdit->setPlaceholderText("21 01 or ATRV");
    m_rawCmdEdit->setMinimumHeight(36);
    m_rawCmdEdit->setStyleSheet("background:#0e1828; color:#60b8a0; border:1px solid #1a3050; padding:4px;");
    logGrid->addWidget(m_rawCmdEdit, 1, 0, 1, 2);

    m_rawSendBtn = new QPushButton("Send");
    m_rawSendBtn->setMinimumHeight(36);
    m_rawSendBtn->setStyleSheet("background:#4a2858; color:#ff88ff; font-weight:bold;");
    connect(m_rawSendBtn, &QPushButton::clicked, this, &MainWindow::onRawSendCustom);
    connect(m_rawCmdEdit, &QLineEdit::returnPressed, this, &MainWindow::onRawSendCustom);
    logGrid->addWidget(m_rawSendBtn, 1, 2);

    // Timeout spinner hidden - default 3000ms
    m_timeoutSpin = new QSpinBox();
    m_timeoutSpin->setRange(200, 10000);
    m_timeoutSpin->setValue(3000);
    m_timeoutSpin->setVisible(false);

    layout->addLayout(logGrid);

    // Log text added below buttons - stretch=1 fills remaining space
    layout->addWidget(m_logText, 1);

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
    if (!m_elm->isConnected()) {
        statusBar()->showMessage("Not connected!");
        return;
    }
    m_readDtcBtn->setEnabled(false);
    m_clearDtcBtn->setEnabled(false);  // prevent clear during read
    static const char* names[] = {"TCM","ECU","ABS","Airbag"};
    int srcIdx = qBound(0, m_dtcSourceIdx, 3);
    QString src = names[srcIdx];
    statusBar()->showMessage("Reading fault codes: " + src + "...");

    WJDiagnostics::Module mod;
    switch (srcIdx) {
    case 1:  mod = WJDiagnostics::Module::MotorECU; break;
    case 2:  mod = WJDiagnostics::Module::ABS; break;
    case 3:  mod = WJDiagnostics::Module::Airbag; break;
    default: mod = WJDiagnostics::Module::KLineTCM; break;
    }

    QPointer<MainWindow> guard(this);
    m_tcm->readDTCs(mod, [guard, src](const QList<WJDiagnostics::DTCEntry> &dtcs) {
        if (!guard) return;  // widget destroyed
        auto *self = guard.data();
        self->m_dtcTable->setRowCount(0);
        for (const auto &d : dtcs) {
            int row = self->m_dtcTable->rowCount();
            self->m_dtcTable->insertRow(row);
            self->m_dtcTable->setItem(row, 0, new QTableWidgetItem(d.code));
            self->m_dtcTable->setItem(row, 1, new QTableWidgetItem(d.description));
            self->m_dtcTable->setItem(row, 2, new QTableWidgetItem(d.isActive ? "Active" : "Stored"));

            if (d.isActive) {
                for (int col = 0; col < 3; ++col) {
                    self->m_dtcTable->item(row, col)->setBackground(QColor(80, 20, 20));
                    self->m_dtcTable->item(row, col)->setForeground(QColor(255, 100, 100));
                }
            }
        }
        self->m_readDtcBtn->setEnabled(true);
        self->m_clearDtcBtn->setEnabled(true);
        self->m_dtcCountLabel->setText(QString("Source: %1 - %2 fault codes").arg(src).arg(dtcs.size()));
        self->statusBar()->showMessage(QString("%1 fault codes read (%2)").arg(dtcs.size()).arg(src));
    });
}

void MainWindow::onClearDTCs()
{
    if (!m_elm->isConnected()) {
        statusBar()->showMessage("Not connected!");
        return;
    }
    static const char* names[] = {"TCM","ECU","ABS","Airbag"};
    int srcIdx = qBound(0, m_dtcSourceIdx, 3);
    QString src = names[srcIdx];

    m_readDtcBtn->setEnabled(false);
    m_clearDtcBtn->setEnabled(false);
    statusBar()->showMessage("Clearing " + src + " fault codes...");

    WJDiagnostics::Module mod;
    switch (srcIdx) {
    case 1:  mod = WJDiagnostics::Module::MotorECU; break;
    case 2:  mod = WJDiagnostics::Module::ABS; break;
    case 3:  mod = WJDiagnostics::Module::Airbag; break;
    default: mod = WJDiagnostics::Module::KLineTCM; break;
    }

    auto info = WJDiagnostics::moduleInfo(mod);
    if (info.bus == WJDiagnostics::BusType::KLine) {
        m_tcm->setActiveBus(WJDiagnostics::BusType::None);
    }

    QPointer<MainWindow> guard(this);
    m_tcm->clearDTCs(mod, [guard, src](bool success) {
        if (!guard) return;
        auto *self = guard.data();
        self->m_readDtcBtn->setEnabled(true);
        self->m_clearDtcBtn->setEnabled(true);
        if (success) {
            self->m_dtcTable->setRowCount(0);
            self->m_dtcCountLabel->setText(QString("Source: %1 - 0 fault codes").arg(src));
            self->statusBar()->showMessage(src + " fault codes cleared");
        } else {
            self->statusBar()->showMessage(src + " fault codes could not be cleared!");
        }
    });
}

void MainWindow::onStartLiveData()
{
    m_ctrlActiveHdr.clear();  // invalidate controls header cache
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
    // Battery voltage from ATRV
    if (status.batteryVoltage > 0 && m_dashBatVoltVal) {
        m_dashBatVoltVal->setText(QString::number(status.batteryVoltage, 'f', 1));
        setGaugeColor(m_dashBatVoltVal,
            status.batteryVoltage < 11.5 ? "#e04040" : status.batteryVoltage < 12.5 ? "#d09030" : "#00d4b4");
    }
}

void MainWindow::onECUDataUpdated(const TCMDiagnostics::ECUStatus &ecu)
{
    #define ESET(lbl, txt) if(lbl) lbl->setText(txt)
    #define ECOL(lbl, col) if(lbl) setGaugeColor(lbl, col)

    // Motor RPM
    ESET(m_dashMotRpmVal, QString::number(ecu.rpm, 'f', 0));
    ECOL(m_dashMotRpmVal, ecu.rpm > 4500 ? "#e04040" : ecu.rpm > 3500 ? "#d09030" : "#00d4b4");

    // Boost Pressure (mbar)
    ESET(m_dashMotBoostVal, QString::number(ecu.boostPressure, 'f', 0));
    ECOL(m_dashMotBoostVal, ecu.boostPressure > 2000 ? "#e04040" : ecu.boostPressure > 1500 ? "#d09030" : "#00d4b4");

    // MAF
    ESET(m_dashMotMafVal, QString::number(ecu.mafActual, 'f', 0));

    // Rail Pressure (bar)
    ESET(m_dashMotRailVal, QString::number(ecu.railActual, 'f', 0));
    ECOL(m_dashMotRailVal, ecu.railActual > 1400 ? "#e04040" : ecu.railActual > 1200 ? "#d09030" : "#00d4b4");

    // Coolant temp
    ESET(m_dashMotCoolVal, QString::number(ecu.coolantTemp, 'f', 0));
    ECOL(m_dashMotCoolVal, ecu.coolantTemp > 105 ? "#e04040" : ecu.coolantTemp > 95 ? "#d09030" : "#00d4b4");

    // IAT -> m_dashCoolantVal (reused in ECU mode)
    ESET(m_dashCoolantVal, QString::number(ecu.iat, 'f', 0));

    // TPS -> m_dashLimpVal (reused in ECU mode)
    ESET(m_dashLimpVal, QString::number(ecu.tps, 'f', 1));

    // Injection quantity -> m_dashSpeedVal (reused in ECU mode)
    ESET(m_dashSpeedVal, QString::number(ecu.injectionQty, 'f', 1));

    // Protected data gauges (only populated if security unlocked)
    ESET(m_dashEgrVal, QString::number(ecu.egrDuty, 'f', 0));
    ESET(m_dashWgVal, QString::number(ecu.wastegate, 'f', 0));
    // Fuel consumption L/h
    ESET(m_dashFuelAdaptVal, QString::number(ecu.fuelFlowLH, 'f', 1));
    if (m_dashFuelAdaptVal) {
        ECOL(m_dashFuelAdaptVal, ecu.fuelFlowLH > 15.0 ? "#e04040" : ecu.fuelFlowLH > 8.0 ? "#d09030" : "#00d4b4");
    }

    // Battery voltage (ATRV)
    if (ecu.batteryVoltage > 0) {
        ESET(m_dashBatVoltVal, QString::number(ecu.batteryVoltage, 'f', 1));
        ECOL(m_dashBatVoltVal, ecu.batteryVoltage < 11.5 ? "#e04040" : ecu.batteryVoltage < 12.5 ? "#d09030" : "#00d4b4");
    }

    #undef ESET
    #undef ECOL
}


void MainWindow::onLogMessage(const QString &msg)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    m_logText->append(QString("[%1] %2").arg(timestamp, msg));
    m_logText->moveCursor(QTextCursor::End);
    m_logText->ensureCursorVisible();
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




// --- Raw Data Read - Test v11: correct SID per module ---
void MainWindow::onRawBusDump()
{
    m_rawDumpBtn->setEnabled(false);
    m_rawDumpBtn->setText("Testing...");

    auto log = [this](const QString &color, const QString &msg) {
        QString ts = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
        m_logText->append(QString("<font color='%1'>[%2] %3</font>").arg(color, ts, msg));
        m_logText->moveCursor(QTextCursor::End);
        m_logText->ensureCursorVisible();
    };

    auto done = [this]() {
        m_rawDumpBtn->setEnabled(true);
        m_rawDumpBtn->setText("Raw Data Read");
    };

    m_logText->append("<font color='white'>========== WINDOW TEST ==========</font>");
    runDiscoveryPhases(log, done);
}

void MainWindow::runDiscoveryPhases(
    std::function<void(const QString&, const QString&)> log,
    std::function<void()> done)
{
    struct Step { QString label; QString action; };
    auto steps = std::make_shared<QList<Step>>();

    // =================================================================
    // QUICK WINDOW TEST — with DiagnosticSession activation
    // APK sends ATSH244011 -> 01 01 00 before IOControl commands
    // Without this, relay returns OK but doesn't physically activate
    // =================================================================
    steps->append(Step{"", "switch:j1850"});

    // --- Left Window (DriverDoor 0x40) ---
    steps->append(Step{"", "header:--- Left Window (DD 0x40 + session) ---"});
    // Step 1: Activate diagnostic session on DriverDoor
    steps->append(Step{"", "j1850hdr:ATSH244011"});
    steps->append(Step{"", "j1850hdr:ATRA40"});
    steps->append(Step{"DD Session",    "j1850cmd:01 01 00"});
    // Step 2: IOControl relay commands
    steps->append(Step{"", "j1850hdr:ATSH24402F"});
    steps->append(Step{"L-WinUp ON 1",  "j1850cmd:38 07 01"});
    steps->append(Step{"L-WinUp ON 2",  "j1850cmd:38 07 01"});
    steps->append(Step{"L-WinUp ON 3",  "j1850cmd:38 07 01"});
    steps->append(Step{"L-WinUp ON 4",  "j1850cmd:38 07 01"});
    steps->append(Step{"L-WinUp ON 5",  "j1850cmd:38 07 01"});
    steps->append(Step{"L-WinUp ON 6",  "j1850cmd:38 07 01"});
    steps->append(Step{"L-WinUp ON 7",  "j1850cmd:38 07 01"});
    steps->append(Step{"L-WinUp ON 8",  "j1850cmd:38 07 01"});
    steps->append(Step{"L-WinUp ON 9",  "j1850cmd:38 07 01"});
    steps->append(Step{"L-WinUp ON 10", "j1850cmd:38 07 01"});
    steps->append(Step{"L-WinUp OFF",   "j1850cmd:38 07 00"});
    // Down
    steps->append(Step{"L-WinDn ON 1",  "j1850cmd:38 08 01"});
    steps->append(Step{"L-WinDn ON 2",  "j1850cmd:38 08 01"});
    steps->append(Step{"L-WinDn ON 3",  "j1850cmd:38 08 01"});
    steps->append(Step{"L-WinDn ON 4",  "j1850cmd:38 08 01"});
    steps->append(Step{"L-WinDn ON 5",  "j1850cmd:38 08 01"});
    steps->append(Step{"L-WinDn ON 6",  "j1850cmd:38 08 01"});
    steps->append(Step{"L-WinDn ON 7",  "j1850cmd:38 08 01"});
    steps->append(Step{"L-WinDn ON 8",  "j1850cmd:38 08 01"});
    steps->append(Step{"L-WinDn ON 9",  "j1850cmd:38 08 01"});
    steps->append(Step{"L-WinDn ON 10", "j1850cmd:38 08 01"});
    steps->append(Step{"L-WinDn OFF",   "j1850cmd:38 08 00"});
    steps->append(Step{"DD Release",    "j1850cmd:3A 02 FF"});

    // --- Right Window (PassengerDoor 0xA0) ---
    steps->append(Step{"", "header:--- Right Window (PD 0xA0 + session) ---"});
    steps->append(Step{"", "j1850hdr:ATSH24A011"});
    steps->append(Step{"", "j1850hdr:ATRAA0"});
    steps->append(Step{"PD Session",    "j1850cmd:01 01 00"});
    steps->append(Step{"", "j1850hdr:ATSH24A02F"});
    steps->append(Step{"R-WinUp ON 1",  "j1850cmd:38 01 12"});
    steps->append(Step{"R-WinUp ON 2",  "j1850cmd:38 01 12"});
    steps->append(Step{"R-WinUp ON 3",  "j1850cmd:38 01 12"});
    steps->append(Step{"R-WinUp ON 4",  "j1850cmd:38 01 12"});
    steps->append(Step{"R-WinUp ON 5",  "j1850cmd:38 01 12"});
    steps->append(Step{"R-WinUp OFF",   "j1850cmd:38 01 00"});
    steps->append(Step{"R-WinDn ON 1",  "j1850cmd:38 00 12"});
    steps->append(Step{"R-WinDn ON 2",  "j1850cmd:38 00 12"});
    steps->append(Step{"R-WinDn ON 3",  "j1850cmd:38 00 12"});
    steps->append(Step{"R-WinDn ON 4",  "j1850cmd:38 00 12"});
    steps->append(Step{"R-WinDn ON 5",  "j1850cmd:38 00 12"});
    steps->append(Step{"R-WinDn OFF",   "j1850cmd:38 00 00"});
    steps->append(Step{"PD Release",    "j1850cmd:3A 02 FF"});

    // --- Body Controls (DD lock + BCM hazard/horn with DiagSession) ---
    steps->append(Step{"", "header:--- Body Controls ---"});
    // Lock via DriverDoor (session already active from window test)
    steps->append(Step{"", "j1850hdr:ATSH24402F"});
    steps->append(Step{"DD Lock ON",    "j1850cmd:38 06 02"});
    steps->append(Step{"DD Lock OFF",   "j1850cmd:38 06 00"});
    steps->append(Step{"DD Unlock",     "j1850cmd:3A 02 FF"});
    // BCM 0x80 with DiagSession — test if session fixes NO DATA
    steps->append(Step{"", "j1850hdr:ATSH248011"});
    steps->append(Step{"", "j1850hdr:ATRA80"});
    steps->append(Step{"BCM Session",   "j1850cmd:01 01 00"});
    steps->append(Step{"", "j1850hdr:ATSH24802F"});
    steps->append(Step{"BCM Horn ON",   "j1850cmd:38 00 CC"});
    steps->append(Step{"BCM Horn OFF",  "j1850cmd:38 00 00"});
    steps->append(Step{"BCM Hazard ON", "j1850cmd:38 01 00"});
    steps->append(Step{"BCM Hazard OFF","j1850cmd:38 01 01"});

    steps->append(Step{"", "header:--- Final ---"});
    steps->append(Step{"Battery", "cmd:ATRV"});

#if 0
    // =================================================================
    // FULL TEST v14 — disabled for quick window test
    // =================================================================
    steps->append(Step{"", "header:=== Phase 1: ECU ArvutaKoodi + blocks ==="});
    steps->append(Step{"", "switch:ecu_arvuta"});
    // After ecu_arvuta: K-Line on ECU, security unlocked, 0x62/B0/B1/B2/VIN already read
    // Now read remaining standard blocks (no security needed)
    for (int b : {0x10,0x12,0x14,0x16,0x18,0x20,0x22,0x24,0x26,0x28,0x30,
                  0x32,0x34,0x38,0x40,0x42,0x44,0x48}) {
        steps->append(Step{
            QString("ECU 0x%1").arg(b, 2, 16, QChar('0')).toUpper(),
            QString("cmd:21 %1").arg(b, 2, 16, QChar('0')).toUpper()
        });
    }
    steps->append(Step{"ECU 1A 91", "cmd:1A 91"});
    steps->append(Step{"ECU 1A 86", "cmd:1A 86"});
    steps->append(Step{"ECU TesterPres", "cmd:3E"});

    // =================================================================
    // PHASE 2: TCM — verify K-Line switch works (ECU->TCM)
    // =================================================================
    steps->append(Step{"", "header:=== Phase 2: TCM (K-Line switch from ECU) ==="});
    steps->append(Step{"", "switch:tcm"});
    // TesterPresent to verify we're on TCM now
    steps->append(Step{"TCM TesterPres", "cmd:3E"});
    steps->append(Step{"TCM 0x30", "cmd:21 30"});
    // Verify source address in response — should contain "F1 20" not "F1 15"
    steps->append(Step{"TCM 0x23 DTC", "cmd:21 23"});
    for (int b : {0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E}) {
        steps->append(Step{
            QString("TCM 0x%1").arg(b, 2, 16, QChar('0')).toUpper(),
            QString("cmd:21 %1").arg(b, 2, 16, QChar('0')).toUpper()
        });
    }

    // =================================================================
    // PHASE 3: Switch back to ECU — verify K-Line switch (TCM->ECU)
    // =================================================================
    steps->append(Step{"", "header:=== Phase 3: ECU verify (TCM->ECU switch) ==="});
    steps->append(Step{"", "switch:ecu"});
    steps->append(Step{"ECU TesterPres", "cmd:3E"});
    steps->append(Step{"ECU 0x12 check", "cmd:21 12"});

    // =================================================================
    // PHASE 4: J1850 Module Discovery
    // =================================================================
    steps->append(Step{"", "header:=== Phase 4: J1850 Module Discovery ==="});
    steps->append(Step{"", "switch:j1850"});

    // --- BCM (0x80) — APK uses SID 0x32 and 0x36, NOT 0x2E ---
    steps->append(Step{"", "header:--- BCM 0x80 (SID 0x32/0x36) ---"});
    steps->append(Step{"", "j1850hdr:ATSH248022"});
    steps->append(Step{"", "j1850hdr:ATRA80"});
    // SID 0x32 — primary BCM read (from APK BodyComputer init)
    for (int pid : {0x00,0x02,0x04,0x05,0x14,0x15,0x16,0x17,0x18,0x21,0x26,0x27,0x28}) {
        steps->append(Step{
            QString("BCM 32 %1").arg(pid, 2, 16, QChar('0')).toUpper(),
            QString("j1850cmd:32 %1 00").arg(pid, 2, 16, QChar('0')).toUpper()
        });
    }
    // SID 0x36 — secondary BCM read
    for (int pid : {0x00,0x02,0x03,0x0C,0x0D,0x0E,0x0F}) {
        steps->append(Step{
            QString("BCM 36 %1").arg(pid, 2, 16, QChar('0')).toUpper(),
            QString("j1850cmd:36 %1 00").arg(pid, 2, 16, QChar('0')).toUpper()
        });
    }
    // SID 0x2E (only 0x0D per APK)
    steps->append(Step{"BCM 2E 0D", "j1850cmd:2E 0D 00"});
    steps->append(Step{"BCM 1A 87", "j1850cmd:1A 87 00"});

    // --- Cluster (0x90) SID 0x32 ---
    steps->append(Step{"", "header:--- Cluster 0x90 ---"});
    steps->append(Step{"", "j1850hdr:ATSH249022"});
    steps->append(Step{"", "j1850hdr:ATRA90"});
    steps->append(Step{"Clust 32 00", "j1850cmd:32 00 00"});
    steps->append(Step{"Clust 1A 87", "j1850cmd:1A 87 00"});

    // --- ABS (0x40) SID 0x36 — APK uses 36 xx for ABS live data ---
    steps->append(Step{"", "header:--- ABS 0x40 (SID 0x36/0x32) ---"});
    steps->append(Step{"", "j1850hdr:ATSH244022"});
    steps->append(Step{"", "j1850hdr:ATRA40"});
    // SID 0x36 — ABS switch/sensor data (from APK ABS init)
    for (int pid : {0x00,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x10,0x11,0x12,0x13,0x14,0x15,0x16}) {
        steps->append(Step{
            QString("ABS 36 %1").arg(pid, 2, 16, QChar('0')).toUpper(),
            QString("j1850cmd:36 %1 00").arg(pid, 2, 16, QChar('0')).toUpper()
        });
    }
    // SID 0x32 — ABS pump voltage, wheel speeds
    for (int pid : {0x00,0x10,0x11,0x13}) {
        steps->append(Step{
            QString("ABS 32 %1").arg(pid, 2, 16, QChar('0')).toUpper(),
            QString("j1850cmd:32 %1 00").arg(pid, 2, 16, QChar('0')).toUpper()
        });
    }
    // SID 0x24 — DTC read
    steps->append(Step{"ABS 24 00", "j1850cmd:24 00 00"});

    // --- Overhead Console (0x28) SID 0x2A ---
    // Uses multiple mode bytes: A0, 10, 22, 20, A3, 14, 30
    steps->append(Step{"", "header:--- Overhead 0x28 (multi-mode) ---"});
    steps->append(Step{"", "j1850hdr:ATSH242822"});
    steps->append(Step{"", "j1850hdr:ATRA28"});
    steps->append(Step{"OHC-22 2A 03", "j1850cmd:2A 03 00"});
    steps->append(Step{"OHC-22 1A 87", "j1850cmd:1A 87 00"});
    // Mode 0xA0 — Primary read mode
    steps->append(Step{"", "j1850hdr:ATSH2428A0"});
    steps->append(Step{"OHC-A0 20 08", "j1850cmd:20 08 00"});
    steps->append(Step{"OHC-A0 20 02", "j1850cmd:20 02 00"});
    steps->append(Step{"OHC-A0 24 00", "j1850cmd:24 00 00"});
    // Mode 0x10
    steps->append(Step{"", "j1850hdr:ATSH242810"});
    steps->append(Step{"OHC-10 20 00", "j1850cmd:20 00 00"});
    // Mode 0x20
    steps->append(Step{"", "j1850hdr:ATSH242820"});
    steps->append(Step{"OHC-20 20 07", "j1850cmd:20 07 00"});

    // --- MemSeat (0x98) SID 0x38 ---
    steps->append(Step{"", "header:--- MemSeat 0x98 ---"});
    steps->append(Step{"", "j1850hdr:ATSH249822"});
    steps->append(Step{"", "j1850hdr:ATRA98"});
    steps->append(Step{"Seat 38 00", "j1850cmd:38 00 00"});
    steps->append(Step{"Seat 1A 87", "j1850cmd:1A 87 00"});

    // --- SKIM (0x62) SID 0x38/0x3A ---
    steps->append(Step{"", "header:--- SKIM 0x62 ---"});
    steps->append(Step{"", "j1850hdr:ATSH246222"});
    steps->append(Step{"", "j1850hdr:ATRA62"});
    steps->append(Step{"SKIM 38 00", "j1850cmd:38 00 01"});
    steps->append(Step{"SKIM 3A 00", "j1850cmd:3A 00 01"});
    steps->append(Step{"SKIM 1A 87", "j1850cmd:1A 87 00"});

    // --- VTSS (0xC0) ---
    // Uses mode 0x27 and 0x22
    steps->append(Step{"", "header:--- VTSS 0xC0 ---"});
    steps->append(Step{"", "j1850hdr:ATSH24C022"});
    steps->append(Step{"", "j1850hdr:ATRAC0"});
    steps->append(Step{"VTSS 2E 00", "j1850cmd:2E 00 00"});
    steps->append(Step{"VTSS 1A 87", "j1850cmd:1A 87 00"});
    steps->append(Step{"", "j1850hdr:ATSH24C027"});
    steps->append(Step{"VTSS-27 28 00", "j1850cmd:28 00 00"});

    // --- Airbag (0x60) ---
    // Uses mode 0xA0 for reading, 0x22 for diag, 0x27 for security, 0xA3
    steps->append(Step{"", "header:--- Airbag 0x60 (multi-mode) ---"});
    steps->append(Step{"", "j1850hdr:ATSH246022"});
    steps->append(Step{"", "j1850hdr:ATRA60"});
    steps->append(Step{"Air-22 28 37 01", "j1850cmd:28 37 01"});
    steps->append(Step{"Air-22 28 0D 00", "j1850cmd:28 0D 00"});
    // Mode 0xA0 — Primary read mode
    steps->append(Step{"", "j1850hdr:ATSH2460A0"});
    steps->append(Step{"Air-A0 20 00", "j1850cmd:20 00 00"});
    steps->append(Step{"Air-A0 24 00", "j1850cmd:24 00 00"});
    // Security mode 0x27
    steps->append(Step{"", "j1850hdr:ATSH246027"});
    steps->append(Step{"AirSec 28 37", "j1850cmd:28 37 00"});
    steps->append(Step{"AirSec 27 01", "j1850cmd:27 01 00"});
    // Mode 0xA3
    steps->append(Step{"", "j1850hdr:ATSH2460A3"});
    steps->append(Step{"Air-A3 02 00", "j1850cmd:02 00 00"});
    // Routine mode 0x31
    steps->append(Step{"", "j1850hdr:ATSH246031"});
    steps->append(Step{"AirRtn 31 25", "j1850cmd:31 25 00"});
    steps->append(Step{"", "j1850hdr:ATSH246022"});

    // --- Radio (0x87) ---
    steps->append(Step{"", "header:--- Radio 0x87 ---"});
    steps->append(Step{"", "j1850hdr:ATSH248722"});
    steps->append(Step{"", "j1850hdr:ATRA87"});
    steps->append(Step{"Radio 2F 01", "j1850cmd:2F 01 00"});
    steps->append(Step{"Radio 1A 87", "j1850cmd:1A 87 00"});

    // --- Module 0xA1 ---
    steps->append(Step{"", "header:--- Module 0xA1 ---"});
    steps->append(Step{"", "j1850hdr:ATSH24A122"});
    steps->append(Step{"", "j1850hdr:ATRAA1"});
    steps->append(Step{"0xA1 2E 00", "j1850cmd:2E 00 00"});
    // Uses mode 0x31 and 0x33
    steps->append(Step{"", "j1850hdr:ATSH24A131"});
    steps->append(Step{"0xA1-31 0D 10", "j1850cmd:0D 10 00"});
    steps->append(Step{"", "j1850hdr:ATSH24A133"});
    steps->append(Step{"0xA1-33 0D 10", "j1850cmd:0D 10 00"});

    // --- Liftgate (0xA0) ---
    steps->append(Step{"", "header:--- Liftgate 0xA0 ---"});
    steps->append(Step{"", "j1850hdr:ATSH24A022"});
    steps->append(Step{"", "j1850hdr:ATRAA0"});
    steps->append(Step{"Liftgate 2E 00", "j1850cmd:2E 00 00"});

    // --- EVIC (0x2A) ---
    steps->append(Step{"", "header:--- EVIC 0x2A ---"});
    steps->append(Step{"", "j1850hdr:ATSH242A22"});
    steps->append(Step{"", "j1850hdr:ATRA2A"});
    steps->append(Step{"EVIC 2A 03", "j1850cmd:2A 03 00"});

    // --- HVAC (0x68) multiple modes ---
    // Uses 0x22, 0x31, 0x33, 0x11
    steps->append(Step{"", "header:--- HVAC 0x68 (multi-mode) ---"});
    steps->append(Step{"", "j1850hdr:ATSH246822"});
    steps->append(Step{"", "j1850hdr:ATRA68"});
    for (int pid = 0x00; pid <= 0x10; pid++) {
        steps->append(Step{
            QString("HVAC %1").arg(pid, 2, 16, QChar('0')).toUpper(),
            QString("j1850cmd:28 %1 00").arg(pid, 2, 16, QChar('0')).toUpper()
        });
    }
    // Mode 0x31 (routine)
    steps->append(Step{"", "j1850hdr:ATSH246831"});
    steps->append(Step{"HVAC-31 28 00", "j1850cmd:28 00 00"});
    // Mode 0x33
    steps->append(Step{"", "j1850hdr:ATSH246833"});
    steps->append(Step{"HVAC-33 2E 02", "j1850cmd:2E 02 00"});

    // =================================================================
    // PHASE 5: Actuator Control Test (verified on real vehicle)
    // =================================================================
    steps->append(Step{"", "header:=== Phase 5: Actuator Control Test ==="});

    // --- DriverDoor 0x40, mode 0x2F (ALL VERIFIED — positive responses) ---
    steps->append(Step{"", "header:--- DriverDoor 0x40 (mode 0x2F) ---"});
    steps->append(Step{"", "j1850hdr:ATSH24402F"});
    steps->append(Step{"", "j1850hdr:ATRA40"});
    steps->append(Step{"DD FrontWinDn ON",  "j1850cmd:38 08 01"});
    steps->append(Step{"DD FrontWinDn OFF", "j1850cmd:38 08 00"});
    steps->append(Step{"DD FrontWinUp ON",  "j1850cmd:38 07 01"});
    steps->append(Step{"DD FrontWinUp OFF", "j1850cmd:38 07 00"});
    steps->append(Step{"DD Lock ON",        "j1850cmd:38 06 02"});
    steps->append(Step{"DD Lock OFF",       "j1850cmd:38 06 00"});
    steps->append(Step{"DD MirrorDn ON",    "j1850cmd:38 02 01"});
    steps->append(Step{"DD MirrorDn OFF",   "j1850cmd:38 02 00"});
    steps->append(Step{"DD MirrorHeat ON",  "j1850cmd:38 06 08"});
    steps->append(Step{"DD MirrorHeat OFF", "j1850cmd:38 06 00"});
    steps->append(Step{"DD MirrorL ON",     "j1850cmd:38 06 10"});
    steps->append(Step{"DD MirrorL OFF",    "j1850cmd:38 06 00"});
    steps->append(Step{"DD MirrorR ON",     "j1850cmd:38 06 20"});
    steps->append(Step{"DD MirrorR OFF",    "j1850cmd:38 06 00"});
    steps->append(Step{"DD MirrorUp ON",    "j1850cmd:38 0C 02"});
    steps->append(Step{"DD MirrorUp OFF",   "j1850cmd:38 0C 00"});
    steps->append(Step{"DD RearWinDn ON",   "j1850cmd:38 08 02"});
    steps->append(Step{"DD RearWinDn OFF",  "j1850cmd:38 08 00"});
    steps->append(Step{"DD RearWinUp ON",   "j1850cmd:38 06 04"});
    steps->append(Step{"DD RearWinUp OFF",  "j1850cmd:38 06 00"});
    steps->append(Step{"DD Illum ON",       "j1850cmd:38 0D 01"});
    steps->append(Step{"DD Illum OFF",      "j1850cmd:38 0D 00"});
    steps->append(Step{"DD Unlock/Release", "j1850cmd:3A 02 FF"});

    // --- PassengerDoor 0xA0, mode 0x2F (ALL VERIFIED) ---
    steps->append(Step{"", "header:--- PassengerDoor 0xA0 (mode 0x2F) ---"});
    steps->append(Step{"", "j1850hdr:ATSH24A02F"});
    steps->append(Step{"", "j1850hdr:ATRAA0"});
    steps->append(Step{"PD FrontWinDn ON",  "j1850cmd:38 00 12"});
    steps->append(Step{"PD FrontWinDn OFF", "j1850cmd:38 00 00"});
    steps->append(Step{"PD FrontWinUp ON",  "j1850cmd:38 01 12"});
    steps->append(Step{"PD FrontWinUp OFF", "j1850cmd:38 01 00"});
    steps->append(Step{"PD RearWinDn ON",   "j1850cmd:38 08 12"});
    steps->append(Step{"PD RearWinDn OFF",  "j1850cmd:38 08 00"});
    steps->append(Step{"PD RearWinUp ON",   "j1850cmd:38 09 12"});
    steps->append(Step{"PD RearWinUp OFF",  "j1850cmd:38 09 00"});
    steps->append(Step{"PD Release ALL",    "j1850cmd:3A 02 FF"});

    // --- BCM 0x80 Header Discovery ---
    // BCM 0x80 mode 0x2F returned NO DATA in v14 test.
    // But relay clicks were heard during Controls button test (commands
    // broadcast on bus without ATRA). Try alternative header formats.
    steps->append(Step{"", "header:--- BCM 0x80 Header Discovery ---"});
    // Standard mode 0x2F (failed before — confirm)
    steps->append(Step{"", "j1850hdr:ATSH24802F"});
    steps->append(Step{"", "j1850hdr:ATRA80"});
    steps->append(Step{"BCM-2F Horn",       "j1850cmd:38 00 CC"});
    steps->append(Step{"BCM-2F Horn OFF",   "j1850cmd:38 00 00"});
    // Mode 0xB4 (APK uses this for some BCM relays)
    steps->append(Step{"", "j1850hdr:ATSH2480B4"});
    steps->append(Step{"BCM-B4 28 0D 01",   "j1850cmd:28 0D 01"});
    steps->append(Step{"BCM-B4 38 02 02",   "j1850cmd:38 02 02"});
    steps->append(Step{"BCM-B4 38 04 02",   "j1850cmd:38 04 02"});
    steps->append(Step{"BCM-B4 38 00 CC",   "j1850cmd:38 00 CC"});
    // Mode 0xA3 (found in Free APK — BCM wiper relay uses this)
    steps->append(Step{"", "j1850hdr:ATSH2480A3"});
    steps->append(Step{"BCM-A3 38 04 02",   "j1850cmd:38 04 02"});
    steps->append(Step{"BCM-A3 38 00 CC",   "j1850cmd:38 00 CC"});
    steps->append(Step{"BCM-A3 38 01 00",   "j1850cmd:38 01 00"});
    // Try with no receive filter (ATAR to clear filter)
    steps->append(Step{"", "j1850hdr:ATSH24802F"});
    steps->append(Step{"", "j1850hdr:ATAR"});
    steps->append(Step{"BCM-noRA Horn",     "j1850cmd:38 00 CC"});
    steps->append(Step{"BCM-noRA Hazard",   "j1850cmd:38 01 00"});
    steps->append(Step{"BCM-noRA Wiper",    "j1850cmd:38 04 02"});
    // Try functional broadcast header (no target filter)
    steps->append(Step{"", "j1850hdr:ATSH686AF1"});
    steps->append(Step{"", "j1850hdr:ATAR"});
    steps->append(Step{"FUNC 38 00 CC",     "j1850cmd:38 00 CC"});
    steps->append(Step{"FUNC 38 01 00",     "j1850cmd:38 01 00"});
    // Try mode 0x22 read first then 0x2F (session activation)
    steps->append(Step{"", "j1850hdr:ATSH248022"});
    steps->append(Step{"", "j1850hdr:ATAR"});
    steps->append(Step{"BCM-22 28 0D 00",   "j1850cmd:28 0D 00"});
    steps->append(Step{"BCM-22 2E 00 00",   "j1850cmd:2E 00 00"});
    steps->append(Step{"", "j1850hdr:ATSH24802F"});
    steps->append(Step{"BCM-sess Horn",     "j1850cmd:38 00 CC"});
    steps->append(Step{"BCM-sess Hazard",   "j1850cmd:38 01 00"});

    // NOTE: BCM 0x80 mode 0x2F returned NO DATA in v14 test
    // Hazard flash + horn chirp triggered by DD Lock relay (38 06 02)
#endif

    // =================================================================
    // STEP RUNNER
    // =================================================================
    auto idx = std::make_shared<int>(0);
    auto run = std::make_shared<std::function<void()>>();

    *run = [this, steps, idx, run, log, done]() {
        if (*idx >= steps->size()) {
            m_logText->append("<font color='white'>========== TEST v14 COMPLETE ==========</font>");
            log("#ffff00", "Use COPY LOG to copy results!");
            done();
            return;
        }
        auto &step = steps->at(*idx);
        (*idx)++;

        if (step.action.startsWith("header:")) {
            log("#00ffcc", step.action.mid(7));
            (*run)(); return;
        }
        if (step.action == "switch:tcm") {
            m_tcm->switchToModule(WJDiagnostics::Module::KLineTCM, [this, log, run](bool ok) {
                log(ok ? "#60b8a0" : "#ff3333", ok ? "TCM OK" : "TCM FAIL");
                (*run)();
            }); return;
        }
        if (step.action == "switch:ecu") {
            m_tcm->switchToModule(WJDiagnostics::Module::MotorECU, [this, log, run](bool ok) {
                log(ok ? "#60b8a0" : "#ff3333", ok ? "ECU OK" : "ECU FAIL");
                (*run)();
            }); return;
        }
        if (step.action == "switch:ecu_arvuta") {
            // ArvutaKoodi — lookup-table seed-key algorithm
            // Discovered through trial-and-error testing
            static const uint8_t T1[] = {0xC0,0xD0,0xE0,0xF0,0x00,0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80,0x90,0xA0,0xB0};
            static const uint8_t T2[] = {0x02,0x03,0x00,0x01,0x06,0x07,0x04,0x05,0x0A,0x0B,0x08,0x09,0x0E,0x0F,0x0C,0x0D};
            static const uint8_t T3[] = {0x90,0x80,0xF0,0xE0,0xD0,0xC0,0x30,0x20,0x10,0x00,0x70,0x60,0x50,0x40,0xB0,0xA0};
            static const uint8_t T4[] = {0x0D,0x0C,0x0F,0x0E,0x09,0x08,0x0B,0x0A,0x05,0x04,0x07,0x06,0x01,0x00,0x03,0x02};
            m_elm->sendCommand("ATZ", [this, log, run](const QString &) {
            QTimer::singleShot(500, this, [this, log, run]() {
            m_elm->sendCommand("ATE1", [this, log, run](const QString &) {
            m_elm->sendCommand("ATH1", [this, log, run](const QString &) {
            m_elm->sendCommand("ATWM8115F13E", [this, log, run](const QString &) {
            m_elm->sendCommand("ATSH8115F1", [this, log, run](const QString &) {
            m_elm->sendCommand("ATSP5", [this, log, run](const QString &) {
            m_elm->sendCommand("ATFI", [this, log, run](const QString &fi) {
                if (fi.contains("ERROR") || (!fi.contains("OK") && !fi.contains("BUS INIT"))) {
                    log("#ff3333", "ATFI fail"); (*run)(); return;
                }
            m_elm->sendCommand("81", [this, log, run](const QString &) {
            m_elm->sendCommand("27 01", [this, log, run](const QString &sr) {
                log("#c0c0ff", "Seed resp: " + sr.trimmed());
                QStringList p = sr.trimmed().split(' ');
                int si = -1;
                for (int i = 0; i < p.size(); i++)
                    if (p[i].compare("67", Qt::CaseInsensitive) == 0) { si = i; break; }
                if (si < 0 || si + 3 >= p.size()) {
                    log("#ff8800", "No seed found"); (*run)(); return;
                }
                bool o1, o2;
                uint8_t s0 = p[si+2].toUInt(&o1, 16);
                uint8_t s1 = p[si+3].toUInt(&o2, 16);
                if (!o1 || !o2) { log("#ff8800", "Seed parse fail"); (*run)(); return; }

                // ArvutaKoodi computation
                uint8_t v1 = (s1 + 0x0B) & 0xFF;
                uint8_t keyLo = T1[(v1 >> 4) & 0xF] | T2[v1 & 0xF];
                uint8_t cond = (s1 > 0x34) ? 1 : 0;
                uint8_t v2 = (s0 + cond + 1) & 0xFF;
                uint8_t keyHi = T3[(v2 >> 4) & 0xF] | T4[v2 & 0xF];

                QString kc = QString("27 02 %1 %2")
                    .arg(keyHi, 2, 16, QChar('0'))
                    .arg(keyLo, 2, 16, QChar('0')).toUpper();
                log("#c080ff", QString("ArvutaKoodi: seed=%1%2 -> key=%3 %4")
                    .arg(s0,2,16,QChar('0')).arg(s1,2,16,QChar('0'))
                    .arg(keyHi,2,16,QChar('0')).arg(keyLo,2,16,QChar('0')));

                m_elm->sendCommand(kc, [this, log, run](const QString &kr) {
                    log("#c0c0ff", "Key resp: " + kr.trimmed());
                    if (kr.contains("67", Qt::CaseInsensitive) &&
                        !kr.contains("7F", Qt::CaseInsensitive)) {
                        log("#00ff00", "*** ECU UNLOCKED (ArvutaKoodi) ***");
                        // Read protected blocks with parsed output
                        m_elm->sendCommand("21 62", [this, log, run](const QString &r) {
                        QString raw = r.trimmed();
                        log("#00ff88", "ECU 0x62: " + raw);
                        // Parse: find "61 62" then 4 data bytes
                        QStringList p62 = raw.split(' ');
                        int si62 = -1;
                        for (int i=0;i<p62.size();i++) if(p62[i]=="61"&&i+1<p62.size()&&p62[i+1]=="62"){si62=i+2;break;}
                        if (si62>=0 && si62+3<p62.size()) {
                            uint8_t egr=p62[si62].toUInt(nullptr,16), wg=p62[si62+1].toUInt(nullptr,16);
                            uint8_t b2=p62[si62+2].toUInt(nullptr,16), b3=p62[si62+3].toUInt(nullptr,16);
                            log("#80ffcc", QString("  -> EGR=%1% WG=%2% byte2=0x%3(%4) byte3=0x%5(%6)")
                                .arg(egr).arg(wg).arg(b2,2,16,QChar('0')).arg(b2).arg(b3,2,16,QChar('0')).arg(b3));
                        }
                        m_elm->sendCommand("21 B0", [this, log, run](const QString &r) {
                        QString raw = r.trimmed();
                        log("#00ff88", "ECU 0xB0: " + raw);
                        QStringList pb = raw.split(' ');
                        int si=-1;
                        for(int i=0;i<pb.size();i++) if(pb[i]=="61"&&i+1<pb.size()&&pb[i+1].compare("B0",Qt::CaseInsensitive)==0){si=i+2;break;}
                        if(si>=0&&si+1<pb.size()) {
                            uint8_t b0=pb[si].toUInt(nullptr,16), b1=pb[si+1].toUInt(nullptr,16);
                            log("#80ffcc", QString("  -> B0: byte0=0x%1(%2) byte1=0x%3(%4)")
                                .arg(b0,2,16,QChar('0')).arg(b0).arg(b1,2,16,QChar('0')).arg(b1));
                        }
                        m_elm->sendCommand("21 B1", [this, log, run](const QString &r) {
                        log("#00ff88", "ECU 0xB1: " + r.trimmed());
                        QStringList pb = r.trimmed().split(' ');
                        int si=-1;
                        for(int i=0;i<pb.size();i++) if(pb[i]=="61"&&i+1<pb.size()&&pb[i+1].compare("B1",Qt::CaseInsensitive)==0){si=i+2;break;}
                        if(si>=0&&si+1<pb.size()) {
                            uint8_t b0=pb[si].toUInt(nullptr,16), b1=pb[si+1].toUInt(nullptr,16);
                            log("#80ffcc", QString("  -> B1: byte0=0x%1(%2) byte1=0x%3(%4)")
                                .arg(b0,2,16,QChar('0')).arg(b0).arg(b1,2,16,QChar('0')).arg(b1));
                        }
                        m_elm->sendCommand("21 B2", [this, log, run](const QString &r) {
                        log("#00ff88", "ECU 0xB2: " + r.trimmed());
                        QStringList pb = r.trimmed().split(' ');
                        int si=-1;
                        for(int i=0;i<pb.size();i++) if(pb[i]=="61"&&i+1<pb.size()&&pb[i+1].compare("B2",Qt::CaseInsensitive)==0){si=i+2;break;}
                        if(si>=0&&si+1<pb.size()) {
                            uint8_t b0=pb[si].toUInt(nullptr,16), b1=pb[si+1].toUInt(nullptr,16);
                            int16_t s16=((int16_t)b0<<8)|b1;
                            log("#80ffcc", QString("  -> B2: byte0=0x%1(%2) byte1=0x%3(%4) s16=%5 /100=%6")
                                .arg(b0,2,16,QChar('0')).arg(b0).arg(b1,2,16,QChar('0')).arg(b1)
                                .arg(s16).arg(s16/100.0,0,'f',2));
                        }
                        m_elm->sendCommand("1A 90", [this, log, run](const QString &r) {
                        log("#00ff88", "ECU VIN: " + r.trimmed());
                        (*run)();
                        });});});});});
                    } else {
                        log("#ff3333", "ArvutaKoodi FAILED: " + kr.trimmed());
                        (*run)();
                    }
                });
            });});});});});});});});});});
            return;
        }
        if (step.action == "switch:j1850") {
            m_elm->sendCommand("ATZ", [this, log, run](const QString &) {
            m_elm->sendCommand("ATE1", [this, log, run](const QString &) {
            m_elm->sendCommand("ATH1", [this, log, run](const QString &) {
            m_elm->sendCommand("ATIFR0", [this, log, run](const QString &) {
            m_elm->sendCommand("ATSP2", [this, log, run](const QString &) {
                log("#60b8a0", "J1850 VPW ready");
                (*run)();
            });});});});});
            return;
        }
        if (step.action.startsWith("j1850hdr:")) {
            m_elm->sendCommand(step.action.mid(9), [run](const QString &) { (*run)(); });
            return;
        }
        if (step.action.startsWith("j1850cmd:")) {
            m_elm->sendCommand(step.action.mid(9), [this, log, run, step](const QString &resp) {
                bool nd = resp.contains("NO DATA")||resp.contains("ERROR")||resp.contains("UNABLE")||resp.contains("?")||resp.contains("TIMEOUT");
                bool nrc = resp.contains("7F");
                log(nd ? "#666666" : (nrc ? "#ff8800" : "#00ff88"), step.label + ": " + resp.trimmed());
                (*run)();
            }, 2000);
            return;
        }
        if (step.action.startsWith("cmd:")) {
            m_elm->sendCommand(step.action.mid(4), [this, log, run, step](const QString &resp) {
                bool bad = resp.contains("NO DATA")||resp.contains("ERROR")||resp.contains("7F")||resp.contains("TIMEOUT");
                QString color = bad ? "#666666" : "#00ff88";
                QString extra;
                // Source address validation
                if (resp.contains("F1 15") && step.label.startsWith("TCM")) {
                    color = "#ff3333";
                    extra = " [!!! SOURCE=ECU(15) NOT TCM(20)]";
                } else if (resp.contains("F1 20") && step.label.startsWith("ECU")) {
                    color = "#ff3333";
                    extra = " [!!! SOURCE=TCM(20) NOT ECU(15)]";
                }
                log(color, step.label + ": " + resp.trimmed() + extra);
                (*run)();
            });
            return;
        }
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
