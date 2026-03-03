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
    QGroupBox *statusBox = new QGroupBox("TCM Durum");
    QGridLayout *statusGrid = new QGridLayout(statusBox);

    m_gearLabel    = new QLabel("Vites: ---");
    m_rpmLabel     = new QLabel("Türbin: --- rpm");
    m_tempLabel    = new QLabel("Sıcaklık: --- °C");
    m_solVoltLabel = new QLabel("Selenoid V: --- V");
    m_limpLabel    = new QLabel("Limp: ---");
    m_throttleBar  = new QProgressBar();
    m_throttleBar->setRange(0, 100);
    m_throttleBar->setFormat("Gaz: %v%");

    QFont bigFont;
    bigFont.setPointSize(14);
    bigFont.setBold(true);
    m_gearLabel->setFont(bigFont);
    m_solVoltLabel->setFont(bigFont);

    statusGrid->addWidget(m_gearLabel,    0, 0);
    statusGrid->addWidget(m_rpmLabel,     0, 1);
    statusGrid->addWidget(m_tempLabel,    0, 2);
    statusGrid->addWidget(m_solVoltLabel, 1, 0);
    statusGrid->addWidget(m_limpLabel,    1, 1);
    statusGrid->addWidget(m_throttleBar,  1, 2);

    mainLayout->addWidget(statusBox);

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

QWidget* MainWindow::createConnectionTab()
{
    QScrollArea *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture);

    QWidget *w = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(w);

    // Bağlantı ayarları
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
    m_disconnectBtn->setEnabled(false);

    m_connectBtn->setMinimumHeight(44);
    m_disconnectBtn->setMinimumHeight(44);

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
    QGroupBox *sessionBox = new QGroupBox("TCM Diagnostik Oturum");
    QVBoxLayout *sessionLayout = new QVBoxLayout(sessionBox);

    m_startSessionBtn = new QPushButton("Diagnostik Oturum Başlat (K-Line ISO 9141-2)");
    m_startSessionBtn->setEnabled(false);
    m_startSessionBtn->setMinimumHeight(44);
    sessionLayout->addWidget(m_startSessionBtn);

    QHBoxLayout *tcmInfoLayout = new QHBoxLayout();
    QPushButton *readInfoBtn = new QPushButton("TCM Bilgilerini Oku");
    readInfoBtn->setObjectName("readInfoBtn");
    tcmInfoLayout->addWidget(readInfoBtn);

    m_tcmPartLabel = new QLabel("Part No: ---");
    m_tcmSwLabel   = new QLabel("Yazılım: ---");
    m_tcmHwLabel   = new QLabel("Donanım: ---");

    QVBoxLayout *infoLabels = new QVBoxLayout();
    infoLabels->addWidget(m_tcmPartLabel);
    infoLabels->addWidget(m_tcmSwLabel);
    infoLabels->addWidget(m_tcmHwLabel);

    sessionLayout->addLayout(tcmInfoLayout);
    sessionLayout->addLayout(infoLabels);

    // Protokol bilgisi
    QLabel *protoInfo = new QLabel(
        "Protokol: K-Line ISO 9141-2 (ELM327 ATSP3)\n"
        "TCM Adresi: 0x10 (EGS - NAG1 722.6)\n"
        "Tester Adresi: 0xF1\n"
        "Header: 81 10 F1\n"
        "Init: 5-baud @ 0x10"
    );
    protoInfo->setStyleSheet("background: #2a2a3a; padding: 8px; border-radius: 4px; "
                             "color: #88ff88; font-family: monospace;");
    sessionLayout->addWidget(protoInfo);

    layout->addWidget(sessionBox);
    layout->addStretch();

    // Bağlantı sinyalleri
    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnect);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &MainWindow::onDisconnect);
    connect(m_startSessionBtn, &QPushButton::clicked, this, &MainWindow::onStartSession);
    connect(readInfoBtn, &QPushButton::clicked, this, &MainWindow::onReadTCMInfo);

    scroll->setWidget(w);
    return scroll;
}

QWidget* MainWindow::createDTCTab()
{
    QWidget *w = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(w);

    QHBoxLayout *btnLayout = new QHBoxLayout();

    m_readDtcBtn  = new QPushButton("Arıza Kodlarını Oku");
    m_clearDtcBtn = new QPushButton("Arıza Kodlarını Sil");
    m_dtcCountLabel = new QLabel("Toplam: 0 hata kodu");

    m_readDtcBtn->setMinimumHeight(44);
    m_clearDtcBtn->setMinimumHeight(44);
    m_readDtcBtn->setEnabled(false);
    m_clearDtcBtn->setEnabled(false);

    btnLayout->addWidget(m_readDtcBtn);
    btnLayout->addWidget(m_clearDtcBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_dtcCountLabel);

    layout->addLayout(btnLayout);

    // DTC tablosu
    m_dtcTable = new QTableWidget(0, 5);
    m_dtcTable->setHorizontalHeaderLabels({
        "Kod", "Açıklama", "Durum", "Aktif", "Tekrar"
    });
    m_dtcTable->horizontalHeader()->setStretchLastSection(true);
    m_dtcTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_dtcTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dtcTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dtcTable->setAlternatingRowColors(true);
    QScroller::grabGesture(m_dtcTable->viewport(), QScroller::LeftMouseButtonGesture);

    layout->addWidget(m_dtcTable);

    // P2602 hakkında bilgi notu
    QLabel *p2602Note = new QLabel(
        "NOT: P2602 (Selenoid Besleme Voltajı Aralık Dışı) genellikle zayıf akü "
        "veya şanzıman 13-pin soket adaptöründeki kötü temas nedeniyle oluşur.\n"
        "Live Data sekmesinde 'Selenoid Besleme Voltajı' değerini gerçek zamanlı izleyebilirsiniz."
    );
    p2602Note->setWordWrap(true);
    p2602Note->setStyleSheet("background: #3a3a20; padding: 8px; border-radius: 4px; color: #ffcc44;");
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
    logBtnLayout->addStretch();
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

void MainWindow::onStartSession()
{
    m_startSessionBtn->setEnabled(false);
    statusBar()->showMessage("TCM oturumu başlatılıyor...");

    m_tcm->startSession([this](bool success) {
        if (success) {
            m_readDtcBtn->setEnabled(true);
            m_clearDtcBtn->setEnabled(true);
            m_startLiveBtn->setEnabled(true);
            m_readIOBtn->setEnabled(true);
            statusBar()->showMessage("TCM diagnostik oturumu aktif ✓");
        } else {
            m_startSessionBtn->setEnabled(true);
            statusBar()->showMessage("TCM oturumu başlatılamadı!");
            QMessageBox::warning(this, "Oturum Hatası",
                "TCM ile diagnostik oturum başlatılamadı.\n\n"
                "Olası nedenler:\n"
                "• Kontak açık değil\n"
                "• ELM327 K-Line protokolünü desteklemiyor\n"
                "• TCM yanıt vermiyor\n"
                "• Yanlış protokol ayarı");
        }
    });
}

void MainWindow::onReadDTCs()
{
    m_readDtcBtn->setEnabled(false);
    statusBar()->showMessage("Arıza kodları okunuyor...");

    m_tcm->readDTCs([this](const QList<KWP2000Handler::DTCInfo> &dtcs) {
        m_dtcTable->setRowCount(dtcs.size());
        m_dtcCountLabel->setText(QString("Toplam: %1 hata kodu").arg(dtcs.size()));

        for (int i = 0; i < dtcs.size(); ++i) {
            const auto &dtc = dtcs[i];

            m_dtcTable->setItem(i, 0, new QTableWidgetItem(dtc.codeStr));
            m_dtcTable->setItem(i, 1, new QTableWidgetItem(dtc.description));
            m_dtcTable->setItem(i, 2, new QTableWidgetItem(
                QString("0x%1").arg(dtc.status, 2, 16, QChar('0'))));
            m_dtcTable->setItem(i, 3, new QTableWidgetItem(
                dtc.isActive ? "AKTİF" : "Kayıtlı"));
            m_dtcTable->setItem(i, 4, new QTableWidgetItem(
                QString::number(dtc.occurrences)));

            // Aktif hataları kırmızı göster
            if (dtc.isActive) {
                for (int col = 0; col < 5; ++col) {
                    m_dtcTable->item(i, col)->setBackground(QColor(80, 20, 20));
                    m_dtcTable->item(i, col)->setForeground(QColor(255, 100, 100));
                }
            }

            // P2602'yi özel vurgula
            if (dtc.codeStr == "P2602") {
                for (int col = 0; col < 5; ++col) {
                    m_dtcTable->item(i, col)->setBackground(QColor(80, 60, 0));
                    m_dtcTable->item(i, col)->setForeground(QColor(255, 200, 50));
                }
            }
        }

        m_readDtcBtn->setEnabled(true);
        statusBar()->showMessage(QString("%1 arıza kodu okundu").arg(dtcs.size()));
    });
}

void MainWindow::onClearDTCs()
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Arıza Kodlarını Sil",
        "Tüm TCM arıza kodlarını silmek istediğinizden emin misiniz?\n\n"
        "NOT: Aktif arızalar silinse bile sorun devam ediyorsa tekrar oluşacaktır.",
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        m_clearDtcBtn->setEnabled(false);
        m_tcm->clearDTCs([this](bool success) {
            m_clearDtcBtn->setEnabled(true);
            if (success) {
                m_dtcTable->setRowCount(0);
                m_dtcCountLabel->setText("Toplam: 0 hata kodu");
                statusBar()->showMessage("Arıza kodları silindi ✓");
            } else {
                statusBar()->showMessage("Arıza kodları silinemedi!");
            }
        });
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

                m_solVoltLabel->setText(QString("Selenoid V: %1 V").arg(val, 0, 'f', 1));
                if (val < 9.0) {
                    m_solVoltLabel->setStyleSheet("color: red; font-weight: bold;");
                } else if (val < 11.0) {
                    m_solVoltLabel->setStyleSheet("color: orange; font-weight: bold;");
                } else {
                    m_solVoltLabel->setStyleSheet("color: lime; font-weight: bold;");
                }
            }
        }
    }
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

void MainWindow::updateStatusLabels(const TCMDiagnostics::TCMStatus &status)
{
    m_gearLabel->setText("Vites: " + gearToString(status.currentGear));
    m_rpmLabel->setText(QString("Türbin: %1 rpm").arg(status.turbineRPM, 0, 'f', 0));
    m_tempLabel->setText(QString("Sıcaklık: %1 °C").arg(status.transTemp, 0, 'f', 1));
    m_throttleBar->setValue(static_cast<int>(status.throttlePosition));

    m_solVoltLabel->setText(QString("Selenoid V: %1 V").arg(status.solenoidVoltage, 0, 'f', 1));

    // Limp mode uyarısı
    if (status.limpMode) {
        m_limpLabel->setText("Limp: AKTİF!");
        m_limpLabel->setStyleSheet("color: red; font-weight: bold; font-size: 14px;");
    } else {
        m_limpLabel->setText("Limp: Normal");
        m_limpLabel->setStyleSheet("color: lime;");
    }

    // Selenoid voltajı renklendirme
    if (status.solenoidVoltage < 9.0) {
        m_solVoltLabel->setStyleSheet("color: red; font-weight: bold;");
    } else if (status.solenoidVoltage < 11.0) {
        m_solVoltLabel->setStyleSheet("color: orange; font-weight: bold;");
    } else {
        m_solVoltLabel->setStyleSheet("color: lime; font-weight: bold;");
    }
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
