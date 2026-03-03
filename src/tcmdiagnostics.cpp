#include "tcmdiagnostics.h"
#include <QDebug>

TCMDiagnostics::TCMDiagnostics(ELM327Connection *elm, QObject *parent)
    : QObject(parent), m_elm(elm)
{
    m_kwp = new KWP2000Handler(elm, this);

    connect(m_kwp, &KWP2000Handler::logMessage,
            this, &TCMDiagnostics::logMessage);

    initLiveDataParams();
}

void TCMDiagnostics::initLiveDataParams()
{
    // NAG1 722.6 TCM Live Data Parametreleri
    // LocalID'ler WJdiag ve Mercedes DAS/Xentry'den referans alınmıştır
    // Her parametre: {localID, isim, birim, min, max, çarpan, offset, byte, signed}

    m_liveParams = {
        // Temel vites ve hız bilgileri
        {0x01, "Mevcut Vites",               "",      0,   7,   1.0,    0, 1, false},
        {0x02, "Hedef Vites",                 "",      0,   7,   1.0,    0, 1, false},
        {0x03, "Vites Kolu Pozisyonu",        "",      0,  15,   1.0,    0, 1, false},
        {0x04, "Türbin (Giriş) RPM",          "rpm",   0, 8000, 1.0,    0, 2, false},
        {0x05, "Çıkış Mili RPM",              "rpm",   0, 8000, 1.0,    0, 2, false},
        {0x06, "Motor RPM (TCM)",             "rpm",   0, 8000, 1.0,    0, 2, false},
        {0x07, "Araç Hızı",                   "km/h",  0, 300,  1.0,    0, 2, false},

        // Sıcaklık ve voltaj
        {0x08, "Şanzıman Sıvı Sıcaklığı",   "°C",  -40, 200,  1.0, -40,  1, false},
        {0x09, "Selenoid Besleme Voltajı",    "V",     0,  20,  0.1,    0, 1, false},
        {0x0A, "Akü Voltajı (TCM)",           "V",     0,  20,  0.1,    0, 1, false},

        // Gaz ve tork
        {0x0B, "Gaz Pedal Pozisyonu",         "%",     0, 100,  0.39,   0, 1, false},
        {0x0C, "Motor Torku İsteği",           "Nm",    0, 500,  1.0,    0, 2, true},
        {0x0D, "Tork Konvertörü Kayması",     "rpm",   0, 5000, 1.0,    0, 2, true},

        // Basınç ve kontrol
        {0x0E, "Hat Basıncı (Modülasyon)",    "bar",   0,  20,  0.1,    0, 1, false},
        {0x0F, "TCC Basıncı",                 "bar",   0,  20,  0.1,    0, 1, false},
        {0x10, "Vites Değiştirme Basıncı",    "bar",   0,  20,  0.1,    0, 1, false},

        // PWM değerleri
        {0x11, "MOD PC Selenoid PWM",         "%",     0, 100,  0.39,   0, 1, false},
        {0x12, "TCC PC Selenoid PWM",          "%",     0, 100,  0.39,   0, 1, false},
        {0x13, "SPC (Shift Pressure) PWM",    "%",     0, 100,  0.39,   0, 1, false},

        // Durum bayrakları
        {0x14, "TCC Durumu",                   "",      0,   3,  1.0,    0, 1, false},
        {0x15, "Adaptasyon Değeri",            "",      0, 255,  1.0,    0, 1, false},
        {0x16, "Limp Mode Durumu",             "",      0,   1,  1.0,    0, 1, false},
    };
}

// --- Oturum Yönetimi ---

void TCMDiagnostics::startSession(std::function<void(bool)> callback)
{
    emit logMessage("TCM diagnostik oturumu başlatılıyor...");

    // Önce default session, sonra extended session
    m_kwp->startDiagnosticSession(KWP2000Handler::DefaultSession,
                                   [this, callback](bool success) {
        if (success) {
            emit logMessage("TCM diagnostik oturumu aktif");
            emit sessionReady(true);
            if (callback) callback(true);
        } else {
            // Alternatif: Extended session dene
            emit logMessage("Default session başarısız, extended deneniyor...");
            m_kwp->startDiagnosticSession(KWP2000Handler::ExtendedSession,
                                           [this, callback](bool ok) {
                emit sessionReady(ok);
                if (callback) callback(ok);
            });
        }
    });
}

void TCMDiagnostics::stopSession()
{
    // Oturumu kapat
    m_kwp->startDiagnosticSession(KWP2000Handler::DefaultSession, nullptr);
    emit logMessage("TCM oturumu kapatıldı");
}

// --- ECU Bilgileri ---

void TCMDiagnostics::readTCMInfo(std::function<void(const QMap<QString,QString>&)> callback)
{
    QMap<QString, QString> *info = new QMap<QString, QString>();
    int *pending = new int(3);

    auto checkDone = [info, pending, callback, this]() {
        (*pending)--;
        if (*pending <= 0) {
            if (callback) callback(*info);
            delete info;
            delete pending;
        }
    };

    // Option 0x01: ECU Identification
    m_kwp->readECUIdentification(0x01, [info, checkDone](const QByteArray &data) {
        if (data.size() > 2) {
            (*info)["PartNumber"] = QString::fromLatin1(data.mid(2));
        }
        checkDone();
    });

    // Option 0x02: Software Version
    m_kwp->readECUIdentification(0x02, [info, checkDone](const QByteArray &data) {
        if (data.size() > 2) {
            (*info)["SoftwareVersion"] = QString::fromLatin1(data.mid(2));
        }
        checkDone();
    });

    // Option 0x03: Hardware Version
    m_kwp->readECUIdentification(0x03, [info, checkDone](const QByteArray &data) {
        if (data.size() > 2) {
            (*info)["HardwareVersion"] = QString::fromLatin1(data.mid(2));
        }
        checkDone();
    });
}

// --- DTC İşlemleri ---

void TCMDiagnostics::readDTCs(std::function<void(const QList<KWP2000Handler::DTCInfo>&)> callback)
{
    m_kwp->readAllDTCs([this, callback](const QList<KWP2000Handler::DTCInfo> &dtcList) {
        emit dtcListReady(dtcList);
        if (callback) callback(dtcList);
    });
}

void TCMDiagnostics::clearDTCs(std::function<void(bool)> callback)
{
    m_kwp->clearAllDTCs(callback);
}

// --- Live Data ---

void TCMDiagnostics::readAllLiveData(std::function<void(const TCMStatus&)> callback)
{
    // Tüm kritik parametreleri sırayla oku
    // Asenkron zincir - her parametreyi oku ve status'u güncelle

    struct ReadContext {
        TCMStatus status;
        int paramIndex = 0;
        QList<LiveParam> params;
        std::function<void(const TCMStatus&)> callback;
    };

    auto ctx = std::make_shared<ReadContext>();
    ctx->params = m_liveParams;
    ctx->callback = callback;

    // Recursive lambda ile sıralı okuma
    auto readNext = std::make_shared<std::function<void()>>();
    *readNext = [this, ctx, readNext]() {
        if (ctx->paramIndex >= ctx->params.size()) {
            // Tüm parametreler okundu
            m_lastStatus = ctx->status;
            emit statusUpdated(m_lastStatus);
            if (ctx->callback) ctx->callback(m_lastStatus);
            return;
        }

        const LiveParam &param = ctx->params[ctx->paramIndex];

        m_kwp->readLocalData(param.localID,
                             [this, ctx, param, readNext](const QByteArray &data) {
            if (!data.isEmpty()) {
                double value = decodeParam(param, data);

                // Status yapısını güncelle
                switch (param.localID) {
                case 0x01: ctx->status.currentGear    = decodeGear(static_cast<uint8_t>(value)); break;
                case 0x02: ctx->status.targetGear     = decodeGear(static_cast<uint8_t>(value)); break;
                case 0x04: ctx->status.turbineRPM     = value; break;
                case 0x05: ctx->status.outputRPM      = value; break;
                case 0x06: ctx->status.engineRPM      = value; break;
                case 0x08: ctx->status.transTemp       = value; break;
                case 0x09: ctx->status.solenoidVoltage = value; break;  // ← P2602!
                case 0x0A: ctx->status.batteryVoltage  = value; break;
                case 0x0B: ctx->status.throttlePosition = value; break;
                case 0x0D: ctx->status.torqueConverterSlip = value; break;
                case 0x14: ctx->status.tccEngaged      = (value > 0); break;
                case 0x16: ctx->status.limpMode         = (value > 0); break;
                default: break;
                }
            }

            ctx->paramIndex++;
            // Biraz gecikme ekle - ELM327'yi boğmamak için
            QTimer::singleShot(20, *readNext);
        });
    };

    // İlk parametreyi oku
    (*readNext)();
}

void TCMDiagnostics::readSingleParam(uint8_t localID,
                                      std::function<void(double)> callback)
{
    // İlgili parametre tanımını bul
    LiveParam foundParam = {};
    bool found = false;

    for (const auto &p : m_liveParams) {
        if (p.localID == localID) {
            foundParam = p;
            found = true;
            break;
        }
    }

    m_kwp->readLocalData(localID, [this, foundParam, found, callback](const QByteArray &data) {
        double value = 0;
        if (found && !data.isEmpty()) {
            value = decodeParam(foundParam, data);
        }
        if (callback) callback(value);
    });
}

// --- I/O Kontrol ---

QList<TCMDiagnostics::IOState> TCMDiagnostics::ioDefinitions() const
{
    // NAG1 722.6 Selenoidler ve I/O
    return {
        {0x01, "Vites Selenoidi 1 (Y3/1s1)",       false, 0, "1-2 Shift"},
        {0x02, "Vites Selenoidi 2 (Y3/1s2)",       false, 0, "2-3 Shift"},
        {0x03, "Vites Selenoidi 3 (Y3/1s3)",       false, 0, "3-4 Shift"},
        {0x04, "Vites Selenoidi 4 (Y3/1s4)",       false, 0, "4-5 Shift / TCC"},
        {0x05, "MOD PC Selenoid (Y3/1y1)",         false, 0, "Basınç Modülasyonu (PWM)"},
        {0x06, "SPC Selenoid (Y3/1y2)",            false, 0, "Vites Değiştirme Basıncı (PWM)"},
        {0x07, "TCC PC Selenoid (Y3/1y3)",          false, 0, "Tork Konvertörü Kavrama (PWM)"},
        {0x08, "Y5 Selenoid",                       false, 0, "Ek Kontrol Selenoidi"},

        // Giriş sinyalleri
        {0x10, "Vites Kolu - P Konumu",              false, 0, "Park Anahtarı"},
        {0x11, "Vites Kolu - R Konumu",              false, 0, "Geri Vites Anahtarı"},
        {0x12, "Vites Kolu - N Konumu",              false, 0, "Boş Vites Anahtarı"},
        {0x13, "Vites Kolu - D Konumu",              false, 0, "Sürüş Anahtarı"},
        {0x14, "Transfer Case Sinyal",               false, 0, "4WD Sinyali"},
        {0x15, "Kickdown Anahtarı",                  false, 0, "Tam Gaz Anahtarı"},
        {0x16, "Fren Anahtarı",                      false, 0, "Fren Pedalı Sinyali"},
        {0x17, "Sport Mode",                          false, 0, "Sport/Comfort Seçimi"},

        // Diğer çıkışlar
        {0x20, "Geri Vites Lambası Rölesi",          false, 0, "Geri Lamba Kontrol"},
        {0x21, "Starter İnterlock Rölesi",            false, 0, "Marş Kilidi"},
        {0x22, "Downshift Request (ESP)",            false, 0, "ESP Vites Düşürme"},
        {0x23, "Torque Reduction Request",            false, 0, "Tork Azaltma İsteği"},
    };
}

void TCMDiagnostics::readIOStates(std::function<void(const QList<IOState>&)> callback)
{
    QList<IOState> states = ioDefinitions();

    // Selenoid durumlarını toplu oku
    // localID 0x30 genellikle tüm I/O durumlarını bir arada döner
    m_kwp->readLocalData(0x30, [this, states, callback](const QByteArray &data) {
        QList<IOState> updatedStates = states;

        if (data.size() >= 4) {
            // Her bit bir I/O'yu temsil eder
            for (int i = 0; i < updatedStates.size() && i / 8 < data.size(); ++i) {
                int byteIdx = i / 8;
                int bitIdx = i % 8;
                if (byteIdx + 2 < data.size()) { // +2 header atla
                    updatedStates[i].isActive =
                        (static_cast<uint8_t>(data[byteIdx + 2]) >> bitIdx) & 0x01;
                }
            }
        }

        if (callback) callback(updatedStates);
    });
}

void TCMDiagnostics::activateOutput(uint8_t localID, bool activate,
                                     std::function<void(bool)> callback)
{
    emit logMessage(QString("I/O Kontrol: ID=0x%1, %2")
                    .arg(localID, 2, 16, QChar('0'))
                    .arg(activate ? "AKTİF" : "DEVRE DIŞI"));

    QByteArray controlParam;
    controlParam.append(static_cast<char>(activate ? 0x01 : 0x00));

    m_kwp->controlIO(localID, controlParam,
                     [this, callback](bool success, const QByteArray &resp) {
        Q_UNUSED(resp)
        if (callback) callback(success);
    });
}

// --- Private Methods ---

double TCMDiagnostics::decodeParam(const LiveParam &param, const QByteArray &data)
{
    // İlk byte pozitif yanıt byte'ı (0x61 = ReadDataByLocalID response), atla
    int startIdx = 0;
    if (data.size() > 0 && static_cast<uint8_t>(data[0]) == 0x61) {
        startIdx = 2; // response SID + localID atla
    }

    if (data.size() - startIdx < param.byteLen) return 0;

    int32_t rawValue = 0;

    if (param.byteLen == 1) {
        rawValue = static_cast<uint8_t>(data[startIdx]);
        if (param.isSigned && (rawValue & 0x80)) {
            rawValue -= 256;
        }
    } else if (param.byteLen == 2) {
        rawValue = (static_cast<uint8_t>(data[startIdx]) << 8) |
                    static_cast<uint8_t>(data[startIdx + 1]);
        if (param.isSigned && (rawValue & 0x8000)) {
            rawValue -= 65536;
        }
    }

    return (rawValue * param.factor) + param.offset;
}

TCMDiagnostics::Gear TCMDiagnostics::decodeGear(uint8_t raw)
{
    switch (raw) {
    case 0: return Gear::Park;
    case 1: return Gear::Reverse;
    case 2: return Gear::Neutral;
    case 3: return Gear::Drive1;
    case 4: return Gear::Drive2;
    case 5: return Gear::Drive3;
    case 6: return Gear::Drive4;
    case 7: return Gear::Drive5;
    default: return Gear::Limp;
    }
}
