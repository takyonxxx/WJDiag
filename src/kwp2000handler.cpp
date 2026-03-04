#include "kwp2000handler.h"
#include <QDebug>
#include <QTimer>

QMap<uint16_t, QString> KWP2000Handler::s_dtcDescriptions;

KWP2000Handler::KWP2000Handler(ELM327Connection *elm, QObject *parent)
    : QObject(parent), m_elm(elm)
{
    initDTCTable();

    // TesterPresent zamanlayıcı - her 2 saniyede bir oturum canlı tut
    m_testerPresentTimer = new QTimer(this);
    m_testerPresentTimer->setInterval(2000);
    connect(m_testerPresentTimer, &QTimer::timeout,
            this, &KWP2000Handler::sendTesterPresent);
}

// --- Oturum Yönetimi ---

void KWP2000Handler::startDiagnosticSession(SessionType session,
                                              std::function<void(bool)> callback)
{
    QByteArray data;
    data.append(static_cast<char>(session));

    sendKWPRequest(StartDiagSession, data, [this, callback](const QByteArray &resp) {
        bool success = isPositiveResponse(resp, StartDiagSession);
        m_sessionActive = success;

        if (success) {
            m_testerPresentTimer->start();
            emit logMessage("Diagnostic session started");
        } else {
            emit logMessage("Session start failed");
        }

        emit sessionStarted(success);
        if (callback) callback(success);
    });
}

void KWP2000Handler::sendTesterPresent()
{
    if (!m_sessionActive) return;

    // 3E 01 - TesterPresent, responseRequired=false (bazı ECU'lar 3E 00 bekler)
    QByteArray data;
    data.append(static_cast<char>(0x01));

    sendKWPRequest(TesterPresent, data, nullptr, 1000);
}

// --- DTC İşlemleri ---

void KWP2000Handler::readAllDTCs(std::function<void(const QList<DTCInfo>&)> callback)
{
    emit logMessage("Reading TCM fault codes...");

    // KWP2000 ReadDTCByStatus
    // Sub-function: 0x02 = reportDTCByStatusMask
    // Status mask: 0xFF = hepsi (aktif + geçmiş)
    QByteArray data;
    data.append(static_cast<char>(0x02));  // reportDTCByStatusMask
    data.append(static_cast<char>(0xFF));  // status mask - all DTCs

    sendKWPRequest(ReadDTCByStatus, data,
                   [this, callback](const QByteArray &resp) {
        QByteArray payload = stripHeader(resp);

        if (payload.isEmpty() || !isPositiveResponse(resp, ReadDTCByStatus)) {
            // Negatif yanıt veya boş - Chrysler/WJ alternatif deneyebiliriz
            // Chrysler bazen SID 0x17 kullanır (ReadDTCByStatus eski versiyon)
            emit logMessage("Standard DTC read failed, trying Chrysler mode...");

            QByteArray altData;
            altData.append(static_cast<char>(0xFF)); // tüm hata kodları

            sendKWPRequest(0x17, altData,
                           [this, callback](const QByteArray &altResp) {
                QByteArray altPayload = stripHeader(altResp);
                QList<DTCInfo> dtcList = parseDTCResponse(altPayload);
                emit dtcListReceived(dtcList);
                if (callback) callback(dtcList);
            }, 10000);
            return;
        }

        QList<DTCInfo> dtcList = parseDTCResponse(payload);
        emit dtcListReceived(dtcList);
        if (callback) callback(dtcList);
    }, 10000);
}

void KWP2000Handler::clearAllDTCs(std::function<void(bool)> callback)
{
    emit logMessage("Clearing TCM fault codes...");

    // ClearDiagnosticInformation (0x14)
    // Group: FF FF = all DTCs
    QByteArray data;
    data.append(static_cast<char>(0xFF));
    data.append(static_cast<char>(0xFF));

    sendKWPRequest(ClearDTC, data, [this, callback](const QByteArray &resp) {
        bool success = isPositiveResponse(resp, ClearDTC);

        if (success) {
            emit logMessage("Fault codes cleared!");
        } else {
            uint8_t nrc = 0;
            if (isNegativeResponse(resp, &nrc)) {
                emit logMessage("DTC clear failed: " + nrcToString(nrc));
            }
        }

        emit dtcCleared(success);
        if (callback) callback(success);
    });
}

// --- ECU Bilgileri ---

void KWP2000Handler::readECUIdentification(uint8_t option,
                                             std::function<void(const QByteArray&)> callback)
{
    QByteArray data;
    data.append(static_cast<char>(option));

    sendKWPRequest(ReadECUID, data, [this, callback](const QByteArray &resp) {
        QByteArray payload = stripHeader(resp);
        if (callback) callback(payload);
    });
}

// --- Live Data ---

void KWP2000Handler::readLocalData(uint8_t localID,
                                    std::function<void(const QByteArray&)> callback)
{
    QByteArray data;
    data.append(static_cast<char>(localID));

    sendKWPRequest(ReadDataByLocalID, data, [this, localID, callback](const QByteArray &resp) {
        QByteArray payload = stripHeader(resp);
        emit liveDataReceived(localID, payload);
        if (callback) callback(payload);
    }, 2000);
}

void KWP2000Handler::readCommonData(uint16_t commonID,
                                     std::function<void(const QByteArray&)> callback)
{
    QByteArray data;
    data.append(static_cast<char>((commonID >> 8) & 0xFF));
    data.append(static_cast<char>(commonID & 0xFF));

    sendKWPRequest(ReadDataByCommonID, data, [this, callback](const QByteArray &resp) {
        QByteArray payload = stripHeader(resp);
        if (callback) callback(payload);
    }, 2000);
}

// --- I/O Control ---

void KWP2000Handler::controlIO(uint8_t localID, const QByteArray &controlParam,
                                std::function<void(bool, const QByteArray&)> callback)
{
    QByteArray data;
    data.append(static_cast<char>(localID));
    data.append(controlParam);

    sendKWPRequest(IOControlByLocalID, data,
                   [this, callback](const QByteArray &resp) {
        QByteArray payload = stripHeader(resp);
        bool success = isPositiveResponse(resp, IOControlByLocalID);
        if (callback) callback(success, payload);
    });
}

// --- Security Access ---

void KWP2000Handler::requestSecuritySeed(std::function<void(const QByteArray&)> callback)
{
    QByteArray data;
    data.append(static_cast<char>(0x01)); // requestSeed

    sendKWPRequest(SecurityAccess, data,
                   [this, callback](const QByteArray &resp) {
        QByteArray payload = stripHeader(resp);

        // Pozitif yanıt: 67 01 [seed bytes...]
        if (payload.size() >= 3 && static_cast<uint8_t>(payload[0]) == 0x67) {
            QByteArray seed = payload.mid(2); // seed baytları
            emit logMessage(QString("Security seed received (%1 bytes)")
                           .arg(seed.size()));
            if (callback) callback(seed);
        } else {
            emit logMessage("Security seed request failed");
            if (callback) callback(QByteArray());
        }
    });
}

void KWP2000Handler::sendSecurityKey(const QByteArray &key,
                                      std::function<void(bool)> callback)
{
    QByteArray data;
    data.append(static_cast<char>(0x02)); // sendKey
    data.append(key);

    sendKWPRequest(SecurityAccess, data,
                   [this, callback](const QByteArray &resp) {
        bool success = isPositiveResponse(resp, SecurityAccess);
        if (success) {
            emit logMessage("Security Access successful!");
        } else {
            emit logMessage("Security Access reddedildi");
        }
        if (callback) callback(success);
    });
}

// --- Private Methods ---

void KWP2000Handler::sendKWPRequest(uint8_t serviceID, const QByteArray &data,
                                     std::function<void(const QByteArray&)> callback,
                                     int timeoutMs)
{
    // KWP2000 mesajı oluştur -> ELM327'ye hex string olarak gönder
    // ELM327 header'ı otomatik ekler (ATSH ile ayarlandı)
    // Biz sadece SID + data gönderiyoruz, checksum ELM327 hesaplar

    QByteArray hexCmd;
    hexCmd.append(QString("%1").arg(serviceID, 2, 16, QChar('0')).toUpper().toLatin1());

    for (int i = 0; i < data.size(); ++i) {
        hexCmd.append(QString("%1").arg(static_cast<uint8_t>(data[i]), 2, 16, QChar('0'))
                      .toUpper().toLatin1());
    }

    m_elm->sendOBDCommand(hexCmd, callback, timeoutMs);
}

bool KWP2000Handler::isPositiveResponse(const QByteArray &resp, uint8_t serviceID)
{
    // Pozitif yanıt: SID + 0x40
    QByteArray payload = stripHeader(resp);
    if (payload.isEmpty()) return false;

    uint8_t expectedResponse = serviceID + 0x40;
    return static_cast<uint8_t>(payload[0]) == expectedResponse;
}

bool KWP2000Handler::isNegativeResponse(const QByteArray &resp, uint8_t *nrc)
{
    QByteArray payload = stripHeader(resp);
    if (payload.size() >= 3 && static_cast<uint8_t>(payload[0]) == 0x7F) {
        if (nrc && payload.size() >= 3) {
            *nrc = static_cast<uint8_t>(payload[2]);
        }
        return true;
    }
    return false;
}

QByteArray KWP2000Handler::stripHeader(const QByteArray &rawData)
{
    // ISO 9141-2 header: Format byte, Target, Source
    // Format byte bit 7:6 = address mode
    // bit 5:0 = data length (if < 64) veya ek length byte

    if (rawData.size() < 4) return rawData;

    uint8_t formatByte = static_cast<uint8_t>(rawData[0]);

    // Header uzunluğu: genellikle 3 byte (fmt, tgt, src)
    // Bazı durumlarda ek length byte olabilir
    int headerLen = 3;

    // Format byte'ta length bilgisi 0 ise, ek length byte var
    uint8_t lenInFormat = formatByte & 0x3F;
    if (lenInFormat == 0 && rawData.size() > 3) {
        headerLen = 4; // ek length byte
    }

    // Son byte checksum, onu da at
    if (rawData.size() > headerLen + 1) {
        return rawData.mid(headerLen, rawData.size() - headerLen - 1);
    }

    // ELM327 genellikle header'sız (sadece data) gönderir ATH0 modunda
    // ATH1 modundayız, ama ELM327 bazen farklı davranabilir
    return rawData;
}

QString KWP2000Handler::nrcToString(uint8_t nrc)
{
    switch (nrc) {
    case 0x10: return "Genel Red (General Reject)";
    case 0x11: return "Servis Desteklenmiyor";
    case 0x12: return "Alt Fonksiyon Desteklenmiyor";
    case 0x21: return "Busy - Retry";
    case 0x22: return "Conditions Not Correct";
    case 0x24: return "Request Sequence Error";
    case 0x31: return "Request Out Of Range";
    case 0x33: return "Security Access Denied";
    case 0x35: return "Invalid Key";
    case 0x36: return "Exceeded Number of Attempts";
    case 0x37: return "Zaman Gecikmesi Bitmedi";
    case 0x78: return "Response Pending";
    default:   return QString("Bilinmeyen NRC: 0x%1").arg(nrc, 2, 16, QChar('0'));
    }
}

QString KWP2000Handler::dtcCodeToString(uint16_t code)
{
    // NAG1 internal DTC numarasını OBD-II P koduna çevir
    // WJ 2.7 CRD TCM DTC formatı: genellikle doğrudan P07XX veya P2XXX
    // Chrysler SCI protokolünde internal code → OBD kodu dönüşümü var

    // Basit dönüşüm: internal code'u P koduna map'le
    // Internal 2-65: aktif hatalar, 98-161: aralıklı hatalar (96 çıkar)
    return QString("P%1").arg(code, 4, 10, QChar('0'));
}

QList<KWP2000Handler::DTCInfo> KWP2000Handler::parseDTCResponse(const QByteArray &data)
{
    QList<DTCInfo> dtcList;

    if (data.isEmpty()) return dtcList;

    // KWP2000 DTC yanıt formatı:
    // Pozitif yanıt byte (0x58 veya 0x57) + DTC count + [DTC high, DTC low, status]...

    int startIdx = 0;

    // İlk byte pozitif yanıt mı kontrol et
    if (data.size() > 0) {
        uint8_t firstByte = static_cast<uint8_t>(data[0]);
        if (firstByte == 0x58 || firstByte == 0x57) {
            startIdx = 1;
            // İkinci byte bazen DTC sayısı olabilir
            if (data.size() > 1) {
                uint8_t dtcCount = static_cast<uint8_t>(data[1]);
                emit logMessage(QString("Total %1 fault codes found").arg(dtcCount));
                startIdx = 2;
            }
        }
    }

    // Her DTC: 2 byte code + 1 byte status = 3 byte
    for (int i = startIdx; i + 2 < data.size(); i += 3) {
        uint8_t highByte = static_cast<uint8_t>(data[i]);
        uint8_t lowByte  = static_cast<uint8_t>(data[i + 1]);
        uint8_t status   = static_cast<uint8_t>(data[i + 2]);

        uint16_t dtcCode = (highByte << 8) | lowByte;

        // OBD-II DTC koduna dönüştür
        // Byte1 bit 7:6 → P/C/B/U, bit 5:4 → 0-3, bit 3:0 → 2. hane
        // Byte2 → 3. ve 4. hane
        char typeChar;
        uint8_t firstDigit;

        switch ((highByte >> 6) & 0x03) {
        case 0: typeChar = 'P'; break; // Powertrain
        case 1: typeChar = 'C'; break; // Chassis
        case 2: typeChar = 'B'; break; // Body
        case 3: typeChar = 'U'; break; // Network
        default: typeChar = 'P'; break;
        }

        firstDigit = (highByte >> 4) & 0x03;
        uint8_t secondDigit = highByte & 0x0F;
        uint8_t thirdDigit = (lowByte >> 4) & 0x0F;
        uint8_t fourthDigit = lowByte & 0x0F;

        DTCInfo dtc;
        dtc.code = dtcCode;
        dtc.status = status;
        dtc.codeStr = QString("%1%2%3%4%5")
                      .arg(typeChar)
                      .arg(firstDigit)
                      .arg(secondDigit, 1, 16)
                      .arg(thirdDigit, 1, 16)
                      .arg(fourthDigit, 1, 16)
                      .toUpper();

        dtc.isActive = (status & 0x01) != 0;
        dtc.isStored = (status & 0x08) != 0;
        dtc.occurrences = (status >> 4) & 0x0F;

        // Lookup in description table
        // P2602 → code = 0x2602 → bakalım tabloda var mı
        uint16_t lookupCode = QString("%1%2%3%4")
                              .arg(firstDigit)
                              .arg(secondDigit, 1, 16)
                              .arg(thirdDigit, 1, 16)
                              .arg(fourthDigit, 1, 16)
                              .toUInt(nullptr, 16);

        if (s_dtcDescriptions.contains(lookupCode)) {
            dtc.description = s_dtcDescriptions[lookupCode];
        } else {
            dtc.description = "No description found";
        }

        emit logMessage(QString("DTC: %1 - %2 [%3]")
                        .arg(dtc.codeStr)
                        .arg(dtc.description)
                        .arg(dtc.isActive ? "ACTIVE" : "STORED"));

        dtcList.append(dtc);
    }

    return dtcList;
}

// --- NAG1 722.6 DTC Tablosu (WJ 2.7 CRD) ---

void KWP2000Handler::initDTCTable()
{
    if (!s_dtcDescriptions.isEmpty()) return;

    // Powertrain DTC'ler - NAG1 Transmission
    s_dtcDescriptions[0x0700] = "Transmission Control System Malfunction";
    s_dtcDescriptions[0x0701] = "Transmission Control System Range/Performance";
    s_dtcDescriptions[0x0702] = "Transmission Control System Electrical";
    s_dtcDescriptions[0x0703] = "Torque Converter/Brake Switch B Circuit";
    s_dtcDescriptions[0x0705] = "Transmission Range Sensor A Circuit Malfunction";
    s_dtcDescriptions[0x0706] = "Transmission Range Sensor A Circuit Range/Performance";
    s_dtcDescriptions[0x0710] = "Transmission Fluid Temperature Sensor Circuit";
    s_dtcDescriptions[0x0711] = "Transmission Fluid Temperature Sensor Range/Performance";
    s_dtcDescriptions[0x0712] = "Transmission Fluid Temperature Sensor Circuit Low";
    s_dtcDescriptions[0x0713] = "Transmission Fluid Temperature Sensor Circuit High";
    s_dtcDescriptions[0x0714] = "Transmission Fluid Temperature Sensor Intermittent";
    s_dtcDescriptions[0x0715] = "Turbine/Input Shaft Speed Sensor A Circuit";
    s_dtcDescriptions[0x0716] = "Turbine/Input Shaft Speed Sensor A Range/Performance";
    s_dtcDescriptions[0x0720] = "Output Shaft Speed Sensor Circuit";
    s_dtcDescriptions[0x0721] = "Output Shaft Speed Sensor Range/Performance";
    s_dtcDescriptions[0x0725] = "Engine Speed Input Circuit Malfunction";
    s_dtcDescriptions[0x0726] = "Engine Speed Input Circuit Range/Performance";
    s_dtcDescriptions[0x0730] = "Incorrect Gear Ratio";
    s_dtcDescriptions[0x0731] = "Gear 1 Incorrect Ratio";
    s_dtcDescriptions[0x0732] = "Gear 2 Incorrect Ratio";
    s_dtcDescriptions[0x0733] = "Gear 3 Incorrect Ratio";
    s_dtcDescriptions[0x0734] = "Gear 4 Incorrect Ratio";
    s_dtcDescriptions[0x0735] = "Gear 5 Incorrect Ratio";
    s_dtcDescriptions[0x0740] = "Torque Converter Clutch Circuit Malfunction";
    s_dtcDescriptions[0x0741] = "Torque Converter Clutch Circuit Performance/Stuck Off";
    s_dtcDescriptions[0x0742] = "Torque Converter Clutch Circuit Stuck On";
    s_dtcDescriptions[0x0743] = "Torque Converter Clutch Circuit Electrical";
    s_dtcDescriptions[0x0744] = "Torque Converter Clutch Circuit Intermittent";
    s_dtcDescriptions[0x0748] = "Pressure Control Solenoid A Electrical";
    s_dtcDescriptions[0x0750] = "Shift Solenoid A Malfunction";
    s_dtcDescriptions[0x0751] = "Shift Solenoid A Performance/Stuck Off";
    s_dtcDescriptions[0x0752] = "Shift Solenoid A Stuck On";
    s_dtcDescriptions[0x0753] = "Shift Solenoid A Electrical";
    s_dtcDescriptions[0x0755] = "Shift Solenoid B Malfunction";
    s_dtcDescriptions[0x0756] = "Shift Solenoid B Performance/Stuck Off";
    s_dtcDescriptions[0x0757] = "Shift Solenoid B Stuck On";
    s_dtcDescriptions[0x0758] = "Shift Solenoid B Electrical";

    // P2XXX - Transmission-specific
    s_dtcDescriptions[0x2600] = "Solenoid Supply Voltage High";
    s_dtcDescriptions[0x2601] = "Solenoid Supply Voltage Low";
    s_dtcDescriptions[0x2602] = "Solenoid Valve: Power Supply Value/Signal Out Of Range";
    s_dtcDescriptions[0x2604] = "Solenoid Supply Circuit Intermittent";

    // NAG1 İç Kodlar → OBD eşleştirmesi
    s_dtcDescriptions[0x2000] = "TCM Internal Malfunction";
    s_dtcDescriptions[0x2001] = "N15/3 (ETC Control Module) Malfunction";
    s_dtcDescriptions[0x2065] = "ETC Communication Malfunction";
    s_dtcDescriptions[0x2212] = "Gear Lever Position Implausible";

    // CAN Bus / İletişim
    s_dtcDescriptions[0x1242] = "CAN Bus - TCM'den Mesaj Yok";
    s_dtcDescriptions[0x1698] = "PCI Bus - TCM'den Mesaj Yok";
    s_dtcDescriptions[0x1694] = "PCI Bus - PCM'den Mesaj Yok";

    // Limp Mode ilişkili
    s_dtcDescriptions[0x0501] = "Vehicle Speed Sensor 1 Performance";
    s_dtcDescriptions[0x0562] = "System Voltage Low";
    s_dtcDescriptions[0x0563] = "System Voltage High";
}
