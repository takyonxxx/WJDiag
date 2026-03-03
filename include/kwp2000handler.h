#pragma once

#include <QObject>
#include <QByteArray>
#include <QMap>
#include <functional>
#include "elm327connection.h"

/**
 * KWP2000 (ISO 14230) Diagnostic Protocol Handler
 * 
 * Jeep WJ 2.7 CRD NAG1 (722.6) TCM iletişimi:
 *   - K-Line üzerinden KWP2000 protokolü
 *   - 5-baud initialization (ISO 9141-2 uyumlu)
 *   - TCM (EGS) adresi: 0x10
 *   - Tester adresi: 0xF1
 *
 * KWP2000 Servis ID'leri (SID):
 *   0x10 - StartDiagnosticSession
 *   0x11 - ECUReset
 *   0x14 - ClearDiagnosticInformation
 *   0x17 - ReadDTCByStatus  (Chrysler variant)
 *   0x18 - ReadDTCByStatus
 *   0x1A - ReadECUIdentification
 *   0x21 - ReadDataByLocalIdentifier
 *   0x27 - SecurityAccess
 *   0x30 - InputOutputControlByLocalIdentifier
 *   0x31 - StartRoutineByLocalIdentifier
 *   0x3E - TesterPresent
 */
class KWP2000Handler : public QObject
{
    Q_OBJECT

public:
    // KWP2000 Service IDs
    enum ServiceID : uint8_t {
        StartDiagSession      = 0x10,
        ECUReset              = 0x11,
        ClearDTC              = 0x14,
        ReadDTCByStatus       = 0x18,
        ReadECUID             = 0x1A,
        ReadDataByLocalID     = 0x21,
        ReadDataByCommonID    = 0x22,
        SecurityAccess        = 0x27,
        IOControlByLocalID    = 0x30,
        StartRoutineByLocalID = 0x31,
        TesterPresent         = 0x3E,
    };

    // Diagnostic Session Types
    enum SessionType : uint8_t {
        DefaultSession    = 0x81,
        ECUProgSession    = 0x85,
        ExtendedSession   = 0x89,
        DevelopmentSession = 0x86,
    };

    // Negative Response Codes
    enum NRC : uint8_t {
        GeneralReject              = 0x10,
        ServiceNotSupported        = 0x11,
        SubFunctionNotSupported    = 0x12,
        BusyRepeatRequest          = 0x21,
        ConditionsNotCorrect       = 0x22,
        RequestSequenceError       = 0x24,
        RequestOutOfRange          = 0x31,
        SecurityAccessDenied       = 0x33,
        InvalidKey                 = 0x35,
        ExceededNumberOfAttempts   = 0x36,
        RequiredTimeDelayNotExpired = 0x37,
    };

    // DTC bilgisi
    struct DTCInfo {
        uint16_t code;          // DTC numarası (ör: 2602)
        uint8_t  status;        // Durum baytı
        QString  codeStr;       // "P2602" formatında
        QString  description;   // Açıklama
        bool     isActive;      // Aktif mi?
        bool     isStored;      // Kayıtlı mı?
        int      occurrences;   // Oluşum sayısı
    };

    explicit KWP2000Handler(ELM327Connection *elm, QObject *parent = nullptr);

    // Oturum yönetimi
    void startDiagnosticSession(SessionType session,
                                std::function<void(bool)> callback = nullptr);
    void sendTesterPresent();

    // DTC İşlemleri
    void readAllDTCs(std::function<void(const QList<DTCInfo>&)> callback);
    void clearAllDTCs(std::function<void(bool)> callback);

    // ECU Bilgileri
    void readECUIdentification(uint8_t option,
                                std::function<void(const QByteArray&)> callback);

    // Live Data
    void readLocalData(uint8_t localID,
                       std::function<void(const QByteArray&)> callback);
    void readCommonData(uint16_t commonID,
                        std::function<void(const QByteArray&)> callback);

    // I/O Kontrol (Selenoid testi vs.)
    void controlIO(uint8_t localID, const QByteArray &controlParam,
                   std::function<void(bool, const QByteArray&)> callback);

    // Security Access (ileri seviye)
    void requestSecuritySeed(std::function<void(const QByteArray&)> callback);
    void sendSecurityKey(const QByteArray &key,
                         std::function<void(bool)> callback);

signals:
    void dtcListReceived(const QList<DTCInfo> &dtcList);
    void dtcCleared(bool success);
    void sessionStarted(bool success);
    void liveDataReceived(uint8_t localID, const QByteArray &data);
    void logMessage(const QString &msg);
    void negativeResponse(uint8_t serviceID, uint8_t nrc, const QString &desc);

private:
    void sendKWPRequest(uint8_t serviceID, const QByteArray &data,
                        std::function<void(const QByteArray&)> callback,
                        int timeoutMs = 5000);
    bool isPositiveResponse(const QByteArray &resp, uint8_t serviceID);
    bool isNegativeResponse(const QByteArray &resp, uint8_t *nrc = nullptr);
    QString nrcToString(uint8_t nrc);
    QString dtcCodeToString(uint16_t code);
    QString dtcDescription(uint16_t code);
    QList<DTCInfo> parseDTCResponse(const QByteArray &data);

    // TCM header byte'ları atlama (ISO format)
    QByteArray stripHeader(const QByteArray &rawData);

    ELM327Connection *m_elm;
    QTimer *m_testerPresentTimer;
    bool m_sessionActive = false;

    // WJ 2.7 CRD NAG1 DTC tablosu
    static QMap<uint16_t, QString> s_dtcDescriptions;
    static void initDTCTable();
};
