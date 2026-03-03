#pragma once
#include <QObject>
#include <QMap>
#include <QVariant>
#include "elm327connection.h"
#include "kwp2000handler.h"

class TCMDiagnostics : public QObject
{
    Q_OBJECT
public:
    struct LiveParam {
        uint8_t localID; QString name; QString unit;
        double minVal; double maxVal; double factor; double offset;
        int byteLen; bool isSigned;
        bool isECU = false;
    };
    struct IOState {
        uint8_t localID; QString name; bool isActive;
        double currentValue; QString description;
    };
    enum class Gear : uint8_t {
        Park=0,Reverse=1,Neutral=2,Drive1=3,Drive2=4,
        Drive3=5,Drive4=6,Drive5=7,Limp=0xFF
    };
    Q_ENUM(Gear)
    struct TCMStatus {
        Gear currentGear=Gear::Park; Gear targetGear=Gear::Park;
        double turbineRPM=0; double outputRPM=0; double engineRPM=0;
        double vehicleSpeed=0; double transTemp=0;
        double solenoidVoltage=0; double batteryVoltage=0;
        double throttlePosition=0; double torqueConverterSlip=0;
        bool tccEngaged=false; bool limpMode=false;
        double coolantTemp=0; double turboBoost=0;
        double mafSensor=0; double mapSensor=0; double linePressure=0;
    };

    explicit TCMDiagnostics(ELM327Connection *elm, QObject *parent=nullptr);
    KWP2000Handler* kwp() const { return m_kwp; }
    void startSession(std::function<void(bool)> callback=nullptr);
    void stopSession();
    void readTCMInfo(std::function<void(const QMap<QString,QString>&)> callback);
    void readDTCs(std::function<void(const QList<KWP2000Handler::DTCInfo>&)> callback);
    void clearDTCs(std::function<void(bool)> callback);
    // Motor ECU (Bosch EDC15C2) durumu - Java wgdiag test vektorlerinden
    struct ECUStatus {
        double rpm=0; double injectionQty=0;      // 21 28: RPM, IQ (mg)
        double coolantTemp=0; double iat=0;        // 21 12: coolant, intake air
        double tps=0; double mapActual=0;          // 21 12: throttle, MAP
        double aap=0;                              // 21 12: atmospheric pressure
        double mafActual=0; double mafSpec=0;      // 21 20: MAF actual/specified
        double railActual=0; double railSpec=0;    // 21 22: rail pressure
        double mapSpec=0;                          // 21 22: MAP specified
        double injCorr[5]={0,0,0,0,0};            // 21 28: injector corrections
    };
    void readAllLiveData(std::function<void(const TCMStatus&)> callback);
    void readECULiveData(std::function<void(const ECUStatus&)> callback);
    void readSingleParam(uint8_t localID, std::function<void(double)> callback);
    void readIOStates(std::function<void(const QList<IOState>&)> callback);
    void activateOutput(uint8_t localID, bool activate, std::function<void(bool)> callback);
    QList<LiveParam> liveDataParams() const { return m_liveParams; }
    QList<IOState> ioDefinitions() const;
    TCMStatus lastStatus() const { return m_lastStatus; }
    void switchToTCM(std::function<void()> done=nullptr);
    void switchToECU(std::function<void()> done=nullptr);
signals:
    void sessionReady(bool success);
    void statusUpdated(const TCMStatus &status);
    void ecuStatusUpdated(const ECUStatus &ecuStatus);
    void dtcListReady(const QList<KWP2000Handler::DTCInfo> &dtcs);
    void logMessage(const QString &msg);
private:
    void initLiveDataParams();
    double decodeParam(const LiveParam &param, const QByteArray &data);
    void parseECUBlock(uint8_t localID, const QByteArray &data, ECUStatus &ecu);
    Gear decodeGear(uint8_t raw);
    ELM327Connection *m_elm;
    KWP2000Handler *m_kwp;
    QList<LiveParam> m_liveParams;
    TCMStatus m_lastStatus;
    ECUStatus m_lastECUStatus;
    bool m_onECU = false;
};
