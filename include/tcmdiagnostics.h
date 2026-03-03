#pragma once

#include <QObject>
#include <QMap>
#include <QVariant>
#include "elm327connection.h"
#include "kwp2000handler.h"

/**
 * NAG1 722.6 TCM Diagnostics
 * 
 * Jeep WJ 2.7 CRD şanzıman diagnostik üst-katman arayüzü.
 * Live data local ID'leri ve solenoid I/O tanımları.
 *
 * NAG1 TCM Live Data Parametreleri (KWP2000 ReadDataByLocalIdentifier):
 *   Toplam 22 live data değeri + 23 I/O değeri desteklenir.
 */
class TCMDiagnostics : public QObject
{
    Q_OBJECT

public:
    // NAG1 Live Data Parametreleri
    struct LiveParam {
        uint8_t  localID;
        QString  name;
        QString  unit;
        double   minVal;
        double   maxVal;
        double   factor;   // ham değer * factor = gerçek değer
        double   offset;   // (ham * factor) + offset
        int      byteLen;  // kaç byte
        bool     isSigned;
    };

    // NAG1 Selenoid / I/O Durumu
    struct IOState {
        uint8_t  localID;
        QString  name;
        bool     isActive;
        double   currentValue;   // PWM duty veya on/off
        QString  description;
    };

    // Vites bilgisi
    enum class Gear : uint8_t {
        Park     = 0,
        Reverse  = 1,
        Neutral  = 2,
        Drive1   = 3,
        Drive2   = 4,
        Drive3   = 5,
        Drive4   = 6,
        Drive5   = 7,
        Limp     = 0xFF
    };
    Q_ENUM(Gear)

    // Genel TCM durumu
    struct TCMStatus {
        Gear     currentGear      = Gear::Park;
        Gear     targetGear       = Gear::Park;
        double   turbineRPM       = 0;
        double   outputRPM        = 0;
        double   engineRPM        = 0;
        double   transTemp         = 0;   // °C
        double   solenoidVoltage   = 0;   // V  ← P2602 ile ilgili!
        double   batteryVoltage    = 0;   // V
        double   throttlePosition  = 0;   // %
        double   torqueConverterSlip = 0; // RPM
        bool     tccEngaged        = false;
        bool     limpMode          = false;
        QString  shifterPosition;
    };

    explicit TCMDiagnostics(ELM327Connection *elm, QObject *parent = nullptr);

    KWP2000Handler* kwp() const { return m_kwp; }

    // Bağlantı ve oturum
    void startSession(std::function<void(bool)> callback = nullptr);
    void stopSession();

    // ECU bilgileri
    void readTCMInfo(std::function<void(const QMap<QString,QString>&)> callback);

    // DTC
    void readDTCs(std::function<void(const QList<KWP2000Handler::DTCInfo>&)> callback);
    void clearDTCs(std::function<void(bool)> callback);

    // Live Data
    void readAllLiveData(std::function<void(const TCMStatus&)> callback);
    void readSingleParam(uint8_t localID,
                         std::function<void(double)> callback);

    // I/O Kontrol - Selenoid testi
    void readIOStates(std::function<void(const QList<IOState>&)> callback);
    void activateOutput(uint8_t localID, bool activate,
                        std::function<void(bool)> callback);

    // Parametre listesi
    QList<LiveParam> liveDataParams() const { return m_liveParams; }
    QList<IOState>   ioDefinitions() const;

    // TCM durum cache
    TCMStatus lastStatus() const { return m_lastStatus; }

signals:
    void sessionReady(bool success);
    void statusUpdated(const TCMStatus &status);
    void dtcListReady(const QList<KWP2000Handler::DTCInfo> &dtcs);
    void logMessage(const QString &msg);

private:
    void initLiveDataParams();
    double decodeParam(const LiveParam &param, const QByteArray &data);
    Gear decodeGear(uint8_t raw);

    ELM327Connection *m_elm;
    KWP2000Handler   *m_kwp;

    QList<LiveParam>  m_liveParams;
    TCMStatus         m_lastStatus;
};
