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
    connect(m_elm, &ELM327Connection::fakeELMDetected,
            this, [this](const QString &reason) {
        m_connStatusLabel->setText("Durum: Bagli (SAHTE ELM!)");
        m_connStatusLabel->setStyleSheet("color: #ff6600; font-weight: bold;");
        statusBar()->showMessage("UYARI: Sahte ELM327 - " + reason);
        onLogMessage("UYARI: Sahte/Klon ELM327! " + reason);
        onLogMessage("Jeep WJ 2.7 CRD icin orijinal ELM327 gerekli (ATFI, ATWM destegi)");
    });
    connect(m_tcm, &TCMDiagnostics::logMessage,
            this, &MainWindow::onLogMessage);
    connect(m_tcm->kwp(), &KWP2000Handler::logMessage,
            this, &MainWindow::onLogMessage);

    connect(m_liveData, &LiveDataManager::dataUpdated,
            this, &MainWindow::onLiveDataUpdated);
    connect(m_liveData, &LiveDataManager::fullStatusUpdated,
            this, &MainWindow::onFullStatusUpdated);

    setWindowTitle("WJ Diag - Jeep Grand Cherokee 2.7 CRD | NAG1 722.6 TCM");

#ifdef Q_OS_ANDROID
    // Android: tam ekran, ekran açık tut
    showMaximized();
    keepScreenOn(true);
#else
    resize(900, 700);
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

#ifdef Q_OS_ANDROID
    // Android: daha büyük font ve margin
    int btnH = 48;
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);
    QFont appFont = QApplication::font();
    appFont.setPointSize(11);
    QApplication::setFont(appFont);
#else
    int btnH = 0; // default
    Q_UNUSED(btnH)
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
    m_tabs->addTab(createConnectionTab(), "Bağlantı");
    m_tabs->addTab(createDTCTab(),        "Arıza Kodları");
    m_tabs->addTab(createLiveDataTab(),   "Canlı Veri");
    m_tabs->addTab(createIOTab(),         "I/O Kontrol");
    m_tabs->addTab(createLogTab(),        "İletişim Log");

    mainLayout->addWidget(m_tabs);
    setCentralWidget(central);

    // Durum çubuğu
    //statusBar()->showMessage("Bağlantı bekleniyor...");
}


// ================================================================
// Dashboard - 11 gauge responsive grid
// ================================================================

QFrame* MainWindow::createGaugeCard(const QString &title, const QString &initValue,
    const QString &unit, QLabel **valueLabel, QLabel **unitLabel)
{
    QFrame *card = new QFrame();
    card->setFrameShape(QFrame::StyledPanel);
    card->setStyleSheet("QFrame{background:#1a1a2e;border:1px solid #2a2a4a;border-radius:6px;padding:4px;}");
    card->setMinimumWidth(80);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout *lay = new QVBoxLayout(card);
    lay->setContentsMargins(4,2,4,2); lay->setSpacing(0);
    QLabel *tl = new QLabel(title);
    tl->setStyleSheet("color:#8888aa;font-size:9px;border:none;background:transparent;");
    tl->setAlignment(Qt::AlignCenter); lay->addWidget(tl);
    QLabel *vl = new QLabel(initValue);
    vl->setAlignment(Qt::AlignCenter);
    vl->setStyleSheet("color:#00ff88;font-size:20px;font-weight:bold;"
        "font-family:'Consolas','Courier New',monospace;border:none;background:transparent;");
    lay->addWidget(vl);
    QLabel *ul = new QLabel(unit);
    ul->setAlignment(Qt::AlignCenter);
    ul->setStyleSheet("color:#6666aa;font-size:8px;border:none;background:transparent;");
    lay->addWidget(ul);
    *valueLabel = vl; *unitLabel = ul;
    return card;
}

QWidget* MainWindow::createDashboardPanel()
{
    QWidget *p = new QWidget();
    QGridLayout *g = new QGridLayout(p);
    g->setContentsMargins(2,2,2,2); g->setSpacing(3);

    // Row 0: Vites, Hiz, RPM, Su Sicakligi
    g->addWidget(createGaugeCard("VITES","---","",&m_dashGearVal,&m_dashGearUnit), 0,0);
    g->addWidget(createGaugeCard("HIZ","---","km/h",&m_dashSpeedVal,&m_dashSpeedUnit), 0,1);
    g->addWidget(createGaugeCard("RPM","---","rpm",&m_dashRpmVal,&m_dashRpmUnit), 0,2);
    g->addWidget(createGaugeCard("SU SICAK","---","C",&m_dashCoolantVal,&m_dashCoolantUnit), 0,3);

    // Row 1: Turbo, MAF, MAP, Basinc
    g->addWidget(createGaugeCard("TURBO","---","bar",&m_dashBoostVal,&m_dashBoostUnit), 1,0);
    g->addWidget(createGaugeCard("MAF","---","g/s",&m_dashMafVal,&m_dashMafUnit), 1,1);
    g->addWidget(createGaugeCard("MAP","---","kPa",&m_dashMapVal,&m_dashMapUnit), 1,2);
    g->addWidget(createGaugeCard("BASINC","---","bar",&m_dashPressVal,&m_dashPressUnit), 1,3);

    // Row 2: Selenoid V, Aku V, Limp (3 cards spanning 4 cols)
    g->addWidget(createGaugeCard("SELENOID V","---","V",&m_dashSolVoltVal,&m_dashSolVoltUnit), 2,0);
    g->addWidget(createGaugeCard("AKU","---","V",&m_dashBatVoltVal,&m_dashBatVoltUnit), 2,1);
    g->addWidget(createGaugeCard("LIMP","---","",&m_dashLimpVal,&m_dashLimpUnit), 2,2,1,2);

    for(int c=0;c<4;++c) g->setColumnStretch(c,1);
    return p;
}

void MainWindow::setGaugeColor(QLabel *vl, const QString &c) {
    vl->setStyleSheet(QString("color:%1;font-size:20px;font-weight:bold;"
        "font-family:'Consolas','Courier New',monospace;border:none;background:transparent;").arg(c));
}

void MainWindow::updateDashboardFromLiveData(const QMap<uint8_t, double> &v)
{
    // TCM params
    if(v.contains(0x01)){
        int g=(int)v[0x01];
        m_dashGearVal->setText(gearToString((TCMDiagnostics::Gear)g));
        setGaugeColor(m_dashGearVal, g>=3 ? "#00ff88" : "#ffcc44");
    }
    if(v.contains(0x07)) m_dashSpeedVal->setText(QString::number(v[0x07],'f',0));
    if(v.contains(0x04)) m_dashRpmVal->setText(QString::number(v[0x04],'f',0));
    if(v.contains(0x0E)) m_dashPressVal->setText(QString::number(v[0x0E],'f',1));
    if(v.contains(0x09)){
        double sv=v[0x09];
        m_dashSolVoltVal->setText(QString::number(sv,'f',1));
        setGaugeColor(m_dashSolVoltVal, sv<9.0?"#ff4444":sv<11.0?"#ffaa00":"#00ff88");
    }
    if(v.contains(0x0A)){
        double bv=v[0x0A];
        m_dashBatVoltVal->setText(QString::number(bv,'f',1));
        setGaugeColor(m_dashBatVoltVal, bv<11.5?"#ff4444":bv<12.5?"#ffaa00":"#00ff88");
    }
    if(v.contains(0x16)){
        bool l=v[0x16]>0;
        m_dashLimpVal->setText(l?"AKTIF!":"Normal");
        setGaugeColor(m_dashLimpVal, l?"#ff4444":"#00ff88");
    }
    // ECU params
    if(v.contains(0x20)){
        double ct=v[0x20];
        m_dashCoolantVal->setText(QString::number(ct,'f',0));
        setGaugeColor(m_dashCoolantVal, ct>105?"#ff4444":ct>95?"#ffaa00":"#00ff88");
    }
    if(v.contains(0x21)){
        double tb=v[0x21];
        m_dashBoostVal->setText(QString::number(tb,'f',2));
        setGaugeColor(m_dashBoostVal, tb>2.2?"#ff4444":tb>1.8?"#ffaa00":"#00ff88");
    }
    if(v.contains(0x22)) m_dashMafVal->setText(QString::number(v[0x22],'f',1));
    if(v.contains(0x23)) m_dashMapVal->setText(QString::number(v[0x23],'f',0));
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
        "Motor/TCM: K-Line (ATSP5) | Diger: J1850 VPW (ATSP2)\n"
        "Orijinal ELM327 onerisi: ATFI, ATWM, ATSH destegi gerekli");
    compatInfo->setStyleSheet("background:#1a2a1a;padding:6px;border-radius:4px;"
                              "color:#88cc88;font-family:monospace;font-size:9px;");
    compatInfo->setWordWrap(true);
    layout->addWidget(compatInfo);


    QGroupBox *connBox = new QGroupBox("WiFi ELM327 Bağlantısı");
    QGridLayout *connGrid = new QGridLayout(connBox);

    connGrid->addWidget(new QLabel("IP Adresi:"), 0, 0);
    m_hostEdit = new QLineEdit("192.168.0.10");
    connGrid->addWidget(m_hostEdit, 0, 1);

    connGrid->addWidget(new QLabel("Port:"), 0, 2);
    m_portSpin = new QSpinBox();
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(35000);
    connGrid->addWidget(m_portSpin, 0, 3);

    m_connectBtn = new QPushButton("Bağlan");
    m_disconnectBtn = new QPushButton("Bağlantıyı Kes");
    m_connectBtn->setMinimumHeight(44);
    m_disconnectBtn->setMinimumHeight(44);
    m_disconnectBtn->setEnabled(false);


    connGrid->addWidget(m_connectBtn, 1, 0, 1, 2);
    connGrid->addWidget(m_disconnectBtn, 1, 2, 1, 2);

    m_connStatusLabel = new QLabel("Durum: Bağlı Değil");
    m_connStatusLabel->setStyleSheet("color: red; font-weight: bold;");
    connGrid->addWidget(m_connStatusLabel, 2, 0, 1, 4);

    layout->addWidget(connBox);

    // ELM327 Bilgileri
    QGroupBox *elmBox = new QGroupBox("ELM327 Bilgileri");
    QGridLayout *elmGrid = new QGridLayout(elmBox);

    m_elmVersionLabel = new QLabel("Versiyon: ---");
    m_batteryVoltLabel = new QLabel("Akü Voltajı: ---");

    elmGrid->addWidget(m_elmVersionLabel, 0, 0);
    elmGrid->addWidget(m_batteryVoltLabel, 0, 1);

    layout->addWidget(elmBox);

    // TCM Oturum
    // === TCM Diagnostik Oturum ===
    QGroupBox *tcmBox = new QGroupBox("TCM - NAG1 722.6 Sanziman");
    tcmBox->setStyleSheet("QGroupBox{font-weight:bold;color:#88ccff;}");
    QVBoxLayout *tcmLayout = new QVBoxLayout(tcmBox);

    m_startSessionBtn = new QPushButton("TCM Oturumu Baslat");
    m_startSessionBtn->setEnabled(false);
    m_startSessionBtn->setMinimumHeight(44);
    m_startSessionBtn->setStyleSheet(
        "QPushButton{background:#1a3a5a;color:white;border:1px solid #3a6a9a;border-radius:4px;}"
        "QPushButton:hover{background:#2a4a6a;}");
    tcmLayout->addWidget(m_startSessionBtn);

    QLabel *tcmProto = new QLabel(
        "Protokol: K-Line ISO 9141-2 (ATSP3)\n"
        "Adres: 0x10 (EGS - NAG1 722.6)\n"
        "Header: 81 10 F1  |  Init: 5-baud @ 0x10");
    tcmProto->setStyleSheet("background:#1a2a3a;padding:6px;border-radius:4px;"
                            "color:#88ff88;font-family:monospace;font-size:10px;");
    tcmLayout->addWidget(tcmProto);

    QPushButton *readInfoBtn = new QPushButton("TCM Bilgilerini Oku");
    tcmLayout->addWidget(readInfoBtn);

    m_tcmPartLabel = new QLabel("Part No: ---");
    m_tcmSwLabel   = new QLabel("Yazilim: ---");
    m_tcmHwLabel   = new QLabel("Donanim: ---");
    tcmLayout->addWidget(m_tcmPartLabel);
    tcmLayout->addWidget(m_tcmSwLabel);
    tcmLayout->addWidget(m_tcmHwLabel);

    layout->addWidget(tcmBox);

    // === ECU Diagnostik Oturum ===
    QGroupBox *ecuBox = new QGroupBox("ECU - Motor (OM612 2.7 CRD)");
    ecuBox->setStyleSheet("QGroupBox{font-weight:bold;color:#ffcc44;}");
    QVBoxLayout *ecuLayout = new QVBoxLayout(ecuBox);

    m_startEcuBtn = new QPushButton("ECU Oturumu Baslat");
    m_startEcuBtn->setEnabled(false);
    m_startEcuBtn->setMinimumHeight(44);
    m_startEcuBtn->setStyleSheet(
        "QPushButton{background:#3a3a1a;color:white;border:1px solid #6a6a3a;border-radius:4px;}"
        "QPushButton:hover{background:#4a4a2a;}");
    ecuLayout->addWidget(m_startEcuBtn);

    QLabel *ecuProto = new QLabel(
        "Protokol: K-Line ISO 9141-2 (ATSP3)\n"
        "Adres: 0x15 (Motor ECU - Bosch EDC15C2)\n"
        "Header: 81 15 F1  |  ATSP5 + ATFI");
    ecuProto->setStyleSheet("background:#2a2a1a;padding:6px;border-radius:4px;"
                            "color:#ffdd88;font-family:monospace;font-size:10px;");
    ecuLayout->addWidget(ecuProto);

    layout->addWidget(ecuBox);

    // === Aktif Header Gostergesi ===
    m_activeHeaderLabel = new QLabel("Aktif Header: ---  (Baglanti bekleniyor)");
    m_activeHeaderLabel->setStyleSheet("background:#2a2a3a;padding:8px;border-radius:4px;"
                                       "color:#aaaaaa;font-family:monospace;font-weight:bold;");
    m_activeHeaderLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_activeHeaderLabel);

    layout->addStretch();

    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnect);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &MainWindow::onDisconnect);
    connect(m_startSessionBtn, &QPushButton::clicked, this, [this]() {
        if (m_tcmSessionActive) {
            // Toggle off
            m_tcmSessionActive = false;
            m_startSessionBtn->setText("TCM Oturumu Baslat");
            m_startSessionBtn->setStyleSheet(
                "QPushButton{background:#1a3a5a;color:white;border:1px solid #3a6a9a;border-radius:4px;}"
                "QPushButton:hover{background:#2a4a6a;}");
            statusBar()->showMessage("TCM oturumu kapatildi");
            updateActiveHeaderLabel();
            return;
        }
        // Start session
        m_startSessionBtn->setEnabled(false);
        statusBar()->showMessage("TCM oturumu baslatiliyor...");
        m_tcm->startSession([this](bool success) {
            if (success) {
                m_tcmSessionActive = true;
                m_startSessionBtn->setText("TCM Aktif");
                m_startSessionBtn->setStyleSheet(
                    "QPushButton{background:#2a5a2a;color:#88ff88;border:1px solid #4a8a4a;border-radius:4px;font-weight:bold;}"
                    "QPushButton:hover{background:#3a6a3a;}");
                m_startSessionBtn->setEnabled(true);
                m_startEcuBtn->setEnabled(true);
                m_readDtcBtn->setEnabled(true);
                m_clearDtcBtn->setEnabled(true);
                m_startLiveBtn->setEnabled(true);
                m_readIOBtn->setEnabled(true);
                statusBar()->showMessage("TCM diagnostik oturumu aktif");
            } else {
                m_startSessionBtn->setEnabled(true);
                statusBar()->showMessage("TCM oturumu baslatilamadi!");
            }
            updateActiveHeaderLabel();
        });
    });
    connect(readInfoBtn, &QPushButton::clicked, this, &MainWindow::onReadTCMInfo);

    connect(m_startEcuBtn, &QPushButton::clicked, this, [this]() {
        m_ecuSessionActive = !m_ecuSessionActive;
        if (m_ecuSessionActive) {
            m_startEcuBtn->setText("ECU Aktif");
            m_startEcuBtn->setStyleSheet(
                "QPushButton{background:#2a5a2a;color:#88ff88;border:1px solid #4a8a4a;border-radius:4px;font-weight:bold;}"
                "QPushButton:hover{background:#3a6a3a;}");
            statusBar()->showMessage("ECU oturumu aktif - Motor verileri okunacak");
        } else {
            m_startEcuBtn->setText("ECU Oturumu Baslat");
            m_startEcuBtn->setStyleSheet(
                "QPushButton{background:#3a3a1a;color:white;border:1px solid #6a6a3a;border-radius:4px;}"
                "QPushButton:hover{background:#4a4a2a;}");
            statusBar()->showMessage("ECU oturumu kapatildi");
        }
        updateActiveHeaderLabel();
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
    QLabel *srcLabel = new QLabel("Kaynak:");
    srcLabel->setStyleSheet("font-weight:bold;");
    sourceLayout->addWidget(srcLabel);

    m_dtcTcmBtn = new QPushButton("TCM (Sanziman)");
    m_dtcTcmBtn->setCheckable(true);
    m_dtcTcmBtn->setChecked(true);
    m_dtcTcmBtn->setMinimumHeight(36);
    m_dtcTcmBtn->setStyleSheet(
        "QPushButton{background:#1a3a5a;color:white;border:1px solid #3a6a9a;border-radius:4px;padding:4px 12px;}"
        "QPushButton:checked{background:#2a5a2a;color:#88ff88;border:1px solid #4a8a4a;font-weight:bold;}");

    m_dtcEcuBtn = new QPushButton("ECU (Motor)");
    m_dtcEcuBtn->setCheckable(true);
    m_dtcEcuBtn->setChecked(false);
    m_dtcEcuBtn->setMinimumHeight(36);
    m_dtcEcuBtn->setStyleSheet(
        "QPushButton{background:#3a3a1a;color:white;border:1px solid #6a6a3a;border-radius:4px;padding:4px 12px;}"
        "QPushButton:checked{background:#2a5a2a;color:#88ff88;border:1px solid #4a8a4a;font-weight:bold;}");

    sourceLayout->addWidget(m_dtcTcmBtn);
    sourceLayout->addWidget(m_dtcEcuBtn);
    sourceLayout->addStretch();
    layout->addLayout(sourceLayout);

    // Toggle logic: radio-button style
    connect(m_dtcTcmBtn, &QPushButton::clicked, this, [this]() {
        m_dtcSourceECU = false;
        m_dtcTcmBtn->setChecked(true);
        m_dtcEcuBtn->setChecked(false);
        m_dtcTable->setRowCount(0);
        m_dtcCountLabel->setText("Kaynak: TCM (Sanziman) - 0 hata kodu");
        statusBar()->showMessage("DTC kaynak: TCM (Sanziman)");
    });
    connect(m_dtcEcuBtn, &QPushButton::clicked, this, [this]() {
        m_dtcSourceECU = true;
        m_dtcEcuBtn->setChecked(true);
        m_dtcTcmBtn->setChecked(false);
        m_dtcTable->setRowCount(0);
        m_dtcCountLabel->setText("Kaynak: ECU (Motor) - 0 hata kodu");
        statusBar()->showMessage("DTC kaynak: ECU (Motor)");
    });

    // === Butonlar ===
    QHBoxLayout *btnLayout = new QHBoxLayout();

    m_readDtcBtn  = new QPushButton("Ariza Kodlarini Oku");
    m_clearDtcBtn = new QPushButton("Ariza Kodlarini Sil");
    m_dtcCountLabel = new QLabel("Kaynak: TCM - 0 hata kodu");

    m_readDtcBtn->setMinimumHeight(44);
    m_clearDtcBtn->setMinimumHeight(44);
    m_readDtcBtn->setEnabled(false);
    m_clearDtcBtn->setEnabled(false);

    btnLayout->addWidget(m_readDtcBtn);
    btnLayout->addWidget(m_clearDtcBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_dtcCountLabel);

    layout->addLayout(btnLayout);

    // DTC tablosu - 6 sutun (kaynak eklendi)
    m_dtcTable = new QTableWidget(0, 6);
    m_dtcTable->setHorizontalHeaderLabels({
        "Kaynak", "Kod", "Aciklama", "Durum", "Aktif", "Tekrar"
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
        "NOT: P2602 (Selenoid Voltaji) genellikle zayif aku veya "
        "sanziman 13-pin soket kontagi nedeniyle olusur.");
    p2602Note->setWordWrap(true);
    p2602Note->setStyleSheet("background:#3a3a20;padding:8px;border-radius:4px;color:#ffcc44;");
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

    m_startLiveBtn = new QPushButton("Canlı Veriyi Başlat");
    m_stopLiveBtn  = new QPushButton("Durdur");
    m_logBtn       = new QPushButton("CSV'ye Kaydet...");

    m_startLiveBtn->setMinimumHeight(44);
    m_stopLiveBtn->setMinimumHeight(44);
    m_logBtn->setMinimumHeight(44);
    m_startLiveBtn->setEnabled(false);
    m_stopLiveBtn->setEnabled(false);

    btnLayout->addWidget(m_startLiveBtn);
    btnLayout->addWidget(m_stopLiveBtn);
    btnLayout->addWidget(m_logBtn);
    btnLayout->addStretch();

    layout->addLayout(btnLayout);

    // Live data tablosu
    m_liveTable = new QTableWidget(0, 5);
    m_liveTable->setHorizontalHeaderLabels({
        "Seç", "Parametre", "Değer", "Birim", "Local ID"
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

        // Değer
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
            m_logBtn->setText("CSV'ye Kaydet...");
        } else {
#ifdef Q_OS_ANDROID
            // Android: Documents klasörüne kaydet
            QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
            if (dir.isEmpty())
                dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            QString path = dir + "/wjdiag_"
                + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".csv";
            QDir().mkpath(dir);
#else
            QString path = QFileDialog::getSaveFileName(
                this, "Log Dosyası Kaydet", "wjdiag_log.csv", "CSV (*.csv)");
#endif
            if (!path.isEmpty()) {
                m_liveData->startLogging(path);
                m_logBtn->setText("Kaydı Durdur");
                statusBar()->showMessage("Log: " + path);
            }
        }
    });

    return w;
}

QWidget* MainWindow::createIOTab()
{
    QWidget *w = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(w);

    m_readIOBtn = new QPushButton("I/O Durumlarını Oku");
    m_readIOBtn->setMinimumHeight(44);
    m_readIOBtn->setEnabled(false);
    layout->addWidget(m_readIOBtn);

    m_ioTable = new QTableWidget(0, 4);
    m_ioTable->setHorizontalHeaderLabels({
        "I/O", "Açıklama", "Durum", "Detay"
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
        "DİKKAT: Selenoid aktüasyon testi sadece araç hareketsiz ve şanzıman P/N konumundayken yapın!\n"
        "Yanlış selenoid aktivasyonu şanzımana zarar verebilir."
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

    QHBoxLayout *logBtnLayout = new QHBoxLayout();
    QPushButton *clearLogBtn = new QPushButton("Temizle");
    connect(clearLogBtn, &QPushButton::clicked, m_logText, &QTextEdit::clear);
    logBtnLayout->addWidget(clearLogBtn);

    // --- Ham Bus Veri Dump butonu ---
    m_rawDumpBtn = new QPushButton("Ham Veri Oku (TCM+ECU)");
    m_rawDumpBtn->setStyleSheet("background:#2a4858; color:#00ffcc; font-weight:bold; padding:6px 12px;");
    m_rawDumpBtn->setToolTip("TCM ve ECU'dan tum local ID'leri okuyup ham hex veriyi log'a basar");
    connect(m_rawDumpBtn, &QPushButton::clicked, this, &MainWindow::onRawBusDump);
    logBtnLayout->addWidget(m_rawDumpBtn);

    logBtnLayout->addStretch();

    // --- Manuel komut gonderme ---
    QLabel *cmdLabel = new QLabel("Komut:");
    cmdLabel->setStyleSheet("color:#aaa;");
    logBtnLayout->addWidget(cmdLabel);

    m_rawCmdEdit = new QLineEdit();
    m_rawCmdEdit->setPlaceholderText("Ornek: 21 01  veya  ATRV");
    m_rawCmdEdit->setFixedWidth(200);
    m_rawCmdEdit->setStyleSheet("background:#1a1a2e; color:#00ff00; border:1px solid #444; padding:4px;");
    logBtnLayout->addWidget(m_rawCmdEdit);

    m_rawSendBtn = new QPushButton("Gonder");
    m_rawSendBtn->setStyleSheet("background:#4a2858; color:#ff88ff; font-weight:bold; padding:6px 12px;");
    connect(m_rawSendBtn, &QPushButton::clicked, this, &MainWindow::onRawSendCustom);
    connect(m_rawCmdEdit, &QLineEdit::returnPressed, this, &MainWindow::onRawSendCustom);
    logBtnLayout->addWidget(m_rawSendBtn);
    layout->addLayout(logBtnLayout);

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
        m_connStatusLabel->setText("Durum: Bağlı Değil");
        m_connStatusLabel->setStyleSheet("color: red; font-weight: bold;");
        m_connectBtn->setEnabled(true);
        m_disconnectBtn->setEnabled(false);
        m_startSessionBtn->setEnabled(false);
        m_readDtcBtn->setEnabled(false);
        m_clearDtcBtn->setEnabled(false);
        m_startLiveBtn->setEnabled(false);
        m_readIOBtn->setEnabled(false);
        statusBar()->showMessage("Bağlantı kesildi");
        break;

    case ELM327Connection::ConnectionState::Connecting:
        m_connStatusLabel->setText("Durum: Bağlanıyor...");
        m_connStatusLabel->setStyleSheet("color: orange; font-weight: bold;");
        statusBar()->showMessage("Bağlanıyor...");
        break;

    case ELM327Connection::ConnectionState::Initializing:
        m_connStatusLabel->setText("Durum: ELM327 Başlatılıyor...");
        m_connStatusLabel->setStyleSheet("color: yellow; font-weight: bold;");
        statusBar()->showMessage("ELM327 başlatılıyor...");
        break;

    case ELM327Connection::ConnectionState::Ready:
        m_connStatusLabel->setText("Durum: Hazır ✓");
        m_connStatusLabel->setStyleSheet("color: lime; font-weight: bold;");
        m_connectBtn->setEnabled(false);
        m_disconnectBtn->setEnabled(true);
        m_startSessionBtn->setEnabled(true);
        m_elmVersionLabel->setText("Versiyon: " + m_elm->elmVersion());
        m_batteryVoltLabel->setText("Akü Voltajı: " + m_elm->elmVoltage());
        statusBar()->showMessage("ELM327 hazır - Diagnostik oturum başlatabilirsiniz");
        break;

    case ELM327Connection::ConnectionState::Error:
        m_connStatusLabel->setText("Durum: HATA!");
        m_connStatusLabel->setStyleSheet("color: red; font-weight: bold;");
        m_connectBtn->setEnabled(true);
        m_disconnectBtn->setEnabled(true);
        statusBar()->showMessage("Bağlantı hatası!");
        break;

    default:
        break;
    }
}

void MainWindow::onReadDTCs()
{
    m_readDtcBtn->setEnabled(false);
    QString src = m_dtcSourceECU ? "ECU (Motor)" : "TCM (Sanziman)";
    statusBar()->showMessage("Ariza kodlari okunuyor: " + src + "...");

    // Header switch, then read DTCs
    auto doRead = [this, src]() {
        m_tcm->readDTCs([this, src](const QList<KWP2000Handler::DTCInfo> &dtcs) {
            m_dtcTable->setRowCount(dtcs.size());
            m_dtcCountLabel->setText(QString("Kaynak: %1 - %2 hata kodu").arg(src).arg(dtcs.size()));

            for (int i = 0; i < dtcs.size(); ++i) {
                const auto &dtc = dtcs[i];

                m_dtcTable->setItem(i, 0, new QTableWidgetItem(src));
                m_dtcTable->setItem(i, 1, new QTableWidgetItem(dtc.codeStr));
                m_dtcTable->setItem(i, 2, new QTableWidgetItem(dtc.description));
                m_dtcTable->setItem(i, 3, new QTableWidgetItem(
                    QString("0x%1").arg(dtc.status, 2, 16, QChar('0'))));
                m_dtcTable->setItem(i, 4, new QTableWidgetItem(
                    dtc.isActive ? "AKTIF" : "Kayitli"));
                m_dtcTable->setItem(i, 5, new QTableWidgetItem(
                    QString::number(dtc.occurrences)));

                // Kaynak rengi
                QColor srcColor = m_dtcSourceECU ? QColor(60, 60, 20) : QColor(20, 40, 60);
                m_dtcTable->item(i, 0)->setBackground(srcColor);

                if (dtc.isActive) {
                    for (int col = 1; col < 6; ++col) {
                        m_dtcTable->item(i, col)->setBackground(QColor(80, 20, 20));
                        m_dtcTable->item(i, col)->setForeground(QColor(255, 100, 100));
                    }
                }
                if (dtc.codeStr == "P2602") {
                    for (int col = 1; col < 6; ++col) {
                        m_dtcTable->item(i, col)->setBackground(QColor(80, 60, 0));
                        m_dtcTable->item(i, col)->setForeground(QColor(255, 200, 50));
                    }
                }
            }
            m_readDtcBtn->setEnabled(true);
            statusBar()->showMessage(QString("%1 ariza kodu okundu (%2)").arg(dtcs.size()).arg(src));
        });
    };

    if (m_dtcSourceECU) {
        m_tcm->switchToECU([doRead]() { doRead(); });
    } else {
        m_tcm->switchToTCM([doRead]() { doRead(); });
    }
}

void MainWindow::onClearDTCs()
{
    QString src = m_dtcSourceECU ? "ECU (Motor)" : "TCM (Sanziman)";
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Ariza Kodlarini Sil",
        QString("%1 ariza kodlarini silmek istediginizden emin misiniz?\n\n"
        "NOT: Aktif arizalar silinse bile sorun devam ediyorsa tekrar olusacaktir.").arg(src),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        m_clearDtcBtn->setEnabled(false);
        statusBar()->showMessage(src + " ariza kodlari siliniyor...");

        auto doClear = [this, src]() {
            m_tcm->clearDTCs([this, src](bool success) {
                m_clearDtcBtn->setEnabled(true);
                if (success) {
                    m_dtcTable->setRowCount(0);
                    m_dtcCountLabel->setText(QString("Kaynak: %1 - 0 hata kodu").arg(src));
                    statusBar()->showMessage(src + " ariza kodlari silindi");
                } else {
                    statusBar()->showMessage(src + " ariza kodlari silinemedi!");
                }
            });
        };

        if (m_dtcSourceECU) {
            m_tcm->switchToECU([doClear]() { doClear(); });
        } else {
            m_tcm->switchToTCM([doClear]() { doClear(); });
        }
    }
}

void MainWindow::onStartLiveData()
{
    // Seçili parametreleri topla
    QList<uint8_t> selected;
    auto params = m_tcm->liveDataParams();

    for (int i = 0; i < m_liveTable->rowCount() && i < params.size(); ++i) {
        if (m_liveTable->item(i, 0)->checkState() == Qt::Checked) {
            selected.append(params[i].localID);
        }
    }

    if (selected.isEmpty()) {
        QMessageBox::warning(this, "Parametre Seçimi",
                             "En az bir parametre seçmelisiniz!");
        return;
    }

    m_liveData->setSelectedParams(selected);
    m_liveData->startPolling(300); // 300ms aralık

    m_startLiveBtn->setEnabled(false);
    m_stopLiveBtn->setEnabled(true);
    statusBar()->showMessage("Canlı veri akışı başladı...");
}

void MainWindow::onStopLiveData()
{
    m_liveData->stopPolling();
    m_startLiveBtn->setEnabled(true);
    m_stopLiveBtn->setEnabled(false);
    statusBar()->showMessage("Canlı veri durduruldu");
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
}

void MainWindow::onReadIO()
{
    m_readIOBtn->setEnabled(false);
    statusBar()->showMessage("I/O durumları okunuyor...");

    m_tcm->readIOStates([this](const QList<TCMDiagnostics::IOState> &states) {
        for (int i = 0; i < states.size() && i < m_ioTable->rowCount(); ++i) {
            QString statusStr = states[i].isActive ? "AKTİF" : "Kapalı";
            m_ioTable->item(i, 2)->setText(statusStr);

            if (states[i].isActive) {
                m_ioTable->item(i, 2)->setBackground(QColor(20, 80, 20));
            } else {
                m_ioTable->item(i, 2)->setBackground(QColor(40, 40, 40));
            }
        }

        m_readIOBtn->setEnabled(true);
        statusBar()->showMessage("I/O durumları güncellendi ✓");
    });
}

void MainWindow::onReadTCMInfo()
{
    m_tcm->readTCMInfo([this](const QMap<QString, QString> &info) {
        m_tcmPartLabel->setText("Part No: " + info.value("PartNumber", "Okunamadı"));
        m_tcmSwLabel->setText("Yazılım: " + info.value("SoftwareVersion", "Okunamadı"));
        m_tcmHwLabel->setText("Donanım: " + info.value("HardwareVersion", "Okunamadı"));
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
    m_dashSolVoltVal->setText(QString::number(st.solenoidVoltage,'f',1));
    m_dashBatVoltVal->setText(QString::number(st.batteryVoltage,'f',1));
    m_dashCoolantVal->setText(QString::number(st.coolantTemp,'f',0));
    m_dashBoostVal->setText(QString::number(st.turboBoost,'f',2));
    m_dashMafVal->setText(QString::number(st.mafSensor,'f',1));
    m_dashMapVal->setText(QString::number(st.mapSensor,'f',0));
    m_dashPressVal->setText(QString::number(st.linePressure,'f',1));
    m_dashLimpVal->setText(st.limpMode ? "AKTIF!" : "Normal");
    setGaugeColor(m_dashLimpVal, st.limpMode ? "#ff4444" : "#00ff88");
    setGaugeColor(m_dashSolVoltVal,
        st.solenoidVoltage<9.0?"#ff4444":st.solenoidVoltage<11.0?"#ffaa00":"#00ff88");
    setGaugeColor(m_dashCoolantVal,
        st.coolantTemp>105?"#ff4444":st.coolantTemp>95?"#ffaa00":"#00ff88");
    setGaugeColor(m_dashBoostVal,
        st.turboBoost>2.2?"#ff4444":st.turboBoost>1.8?"#ffaa00":"#00ff88");
}

void MainWindow::updateActiveHeaderLabel()
{
    QString text;
    QString style;
    if (m_tcmSessionActive && m_ecuSessionActive) {
        text = "Aktif: TCM + ECU  |  Dual Mode  |  Otomatik Header Switch";
        style = "background:#2a3a2a;padding:8px;border-radius:4px;"
                "color:#88ffaa;font-family:monospace;font-weight:bold;";
    } else if (m_tcmSessionActive) {
        text = "Aktif: TCM  |  ATSH 81 10 F1  |  NAG1 722.6";
        style = "background:#1a3a5a;padding:8px;border-radius:4px;"
                "color:#88ccff;font-family:monospace;font-weight:bold;";
    } else if (m_ecuSessionActive) {
        text = "Aktif: ECU  |  ATSH 81 15 F1  |  Motor OM612 (EDC15C2)";
        style = "background:#3a3a1a;padding:8px;border-radius:4px;"
                "color:#ffcc44;font-family:monospace;font-weight:bold;";
    } else {
        text = "Aktif Header: ---  (Oturum baslatilmadi)";
        style = "background:#2a2a3a;padding:8px;border-radius:4px;"
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
    m_rawDumpBtn->setText("Okunuyor...");

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

    QList<uint8_t> tcmIDs = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,
                              0x0B,0x0C,0x0D,0x0E,0x0F,0x10,0x11,0x12,0x13,0x14,
                              0x15,0x16,0x20,0x21,0x22,0x23,0x30};
    QList<uint8_t> ecuIDs = {0x12,0x20,0x22,0x26,0x28};

    auto ctx = std::make_shared<int>(0);
    auto allIDs = std::make_shared<QList<QPair<QString,uint8_t>>>();
    for (auto id : tcmIDs) allIDs->append({"#00cccc", id});
    allIDs->append({"switch", 0xFF});
    for (auto id : ecuIDs) allIDs->append({"#ffcc00", id});

    m_logText->append("<font color='white'>========== HAM BUS DUMP BASLADI ==========</font>");
    m_logText->append("<font color='#00cccc'>--- TCM (0x10) Header: ATSH8110F1 ---</font>");

    auto readNext = std::make_shared<std::function<void()>>();
    *readNext = [this, ctx, allIDs, readNext, logHex]() {
        if (*ctx >= allIDs->size()) {
            m_tcm->switchToTCM([this]() {
                m_logText->append("<font color='white'>========== HAM BUS DUMP TAMAMLANDI ==========</font>");
                m_rawDumpBtn->setEnabled(true);
                m_rawDumpBtn->setText("Ham Veri Oku (TCM+ECU)");
            });
            return;
        }
        auto pair = allIDs->at(*ctx);
        if (pair.first == "switch") {
            (*ctx)++;
            m_tcm->switchToECU([this, readNext]() {
                m_logText->append("<font color='#ffcc00'>--- ECU (0x15) Header: ATSH8115F1 ---</font>");
                QTimer::singleShot(50, *readNext);
            });
            return;
        }
        QString color = pair.first;
        uint8_t lid = pair.second;
        QString cmdStr = QString("21 %1").arg(lid, 2, 16, QChar('0')).toUpper();
        m_tcm->kwp()->readLocalData(lid, [this, ctx, readNext, logHex, color, cmdStr](const QByteArray &data) {
            logHex(color, cmdStr, data);
            (*ctx)++;
            QTimer::singleShot(40, *readNext);
        });
    };
    m_tcm->switchToTCM([readNext]() { (*readNext)(); });
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
