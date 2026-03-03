#pragma once
#include <QObject>
#include <QMap>
#include <QVariant>
#include <QTimer>
#include "elm327connection.h"
#include "kwp2000handler.h"

// ============================================================
// WJ 2.7 CRD Multi-Protocol Multi-Module Diagnostics
// K-Line (ATSP5): 0x15=MotorECU, 0x20=EPC
// J1850 VPW (ATSP2): 0x28=TCM, 0x40=ABS, 0x60=Airbag ...
// ============================================================

class WJDiagnostics : public QObject
{
    Q_OBJECT
public:
    // --- Gear Enum (NAG1 722.6) ---
    enum class Gear : int {
        Park=0, Reverse=1, Neutral=2,
        Drive1=3, Drive2=4, Drive3=5, Drive4=6, Drive5=7,
        Limp=8, Unknown=-1
    };
    Q_ENUM(Gear)

    // --- Module Addresses ---
    enum class Module : uint8_t {
        MotorECU     = 0x15,
        EPC          = 0x20,
        TCM          = 0x28,
        TransferCase = 0x2A,
        ABS          = 0x40,
        Airbag       = 0x60,
        SKIM         = 0x62,
        ATC          = 0x68,
        BCM          = 0x80,
        Compass      = 0x87,
        Cluster      = 0x90,
        Radio        = 0x98,
        Overhead     = 0xA0,
    };
    Q_ENUM(Module)

    enum class BusType { KLine, J1850 };

    // --- Data Structures ---
    struct ModuleInfo {
        Module id; QString name; QString shortName;
        BusType bus; QString atshHeader; QString atwmWakeup; QString atspProtocol;
    };

    struct DTCEntry {
        QString code; QString description; uint8_t status;
        bool isActive; int occurrences; Module source;
    };

    struct LiveParam {
        uint8_t localID; QString name; QString unit;
        double minVal, maxVal, factor, offset;
        int byteLen; bool isSigned;
    };

    struct IODefinition {
        uint8_t localID;
        QString name;
        QString description;
    };

    struct IOState {
        uint8_t localID;
        bool isActive;
        uint8_t rawValue;
    };

    struct ECUStatus {
        double rpm=0, injectionQty=0;
        double coolantTemp=0, iat=0, tps=0;
        double mapActual=0, aap=0;
        double mafActual=0, mafSpec=0;
        double railActual=0, railSpec=0, mapSpec=0;
        double injCorr[5]={0,0,0,0,0};
        double boostPressure=0, boostSetpoint=0;
        double batteryVoltage=0;
        // Block 0x62 fields
        double egrDuty=0, wastegate=0;
        double alternatorDuty=0;
        bool glowPlug1=false, glowPlug2=false;
        double coolantSensorV=0, iatSensorV=0;
        double accelPedal1=0, accelPedal2=0;
        double vehicleSpeed=0;
    };

    struct TCMStatus {
        // Gear
        Gear currentGear = Gear::Unknown;
        int actualGear=0, selectedGear=0, maxGear=0;
        // RPM
        double turbineRPM=0, outputRPM=0;
        // Temperatures & pressures
        double transTemp=0, tccPressure=0;
        double actualTCCslip=0, desTCCslip=0;
        QString tccState;
        double solenoidSupply=0;
        double vehicleSpeed=0;
        // Compat fields (eski dashboard icin)
        double solenoidVoltage=0;
        double batteryVoltage=0;
        double coolantTemp=0;
        double turboBoost=0;
        double mafSensor=0;
        double mapSensor=0;
        double linePressure=0;
        bool limpMode=false;
    };

    struct ABSStatus {
        double wheelLF=0, wheelRF=0, wheelLR=0, wheelRR=0;
        double vehicleSpeed=0;
        bool absActive=false;
        bool tractionControl=false;
    };

    struct AirbagStatus {
        bool lampOn=false;
        int faultCount=0;
        QString driverSquib1, driverSquib2;
        QString passengerSquib1, passengerSquib2;
        QString driverCurtain, passengerCurtain;
        QString driverSideImpact, passengerSideImpact;
        QString seatBeltDriver, seatBeltPassenger;
    };

    // --- Constructor ---
    explicit WJDiagnostics(ELM327Connection *elm, QObject *parent=nullptr);
    KWP2000Handler* kwp() const { return m_kwp; }
    ELM327Connection* elm() const { return m_elm; }

    // --- Module Registry ---
    static QList<ModuleInfo> allModules();
    static ModuleInfo moduleInfo(Module mod);
    static QString moduleName(Module mod);

    // --- Protocol Switch ---
    void switchToModule(Module mod, std::function<void(bool)> done=nullptr);
    Module activeModule() const { return m_activeModule; }
    BusType activeBus() const { return m_activeBus; }

    // --- Session ---
    void startSession(Module mod, std::function<void(bool)> cb=nullptr);
    void startSession(std::function<void(bool)> cb); // compat: default=TCM
    void stopSession();

    // --- DTC ---
    void readDTCs(Module mod, std::function<void(const QList<DTCEntry>&)> cb);
    void readDTCs(std::function<void(const QList<KWP2000Handler::DTCInfo>&)> cb); // compat
    void clearDTCs(Module mod, std::function<void(bool)> cb);
    void clearDTCs(std::function<void(bool)> cb); // compat
    void readModuleInfo(Module mod, std::function<void(const QMap<QString,QString>&)> cb);

    // --- Live Data ---
    void readECULiveData(std::function<void(const ECUStatus&)> cb);
    void readTCMLiveData(std::function<void(const TCMStatus&)> cb);
    void readABSLiveData(std::function<void(const ABSStatus&)> cb);
    void readAllLiveData(std::function<void(const TCMStatus&)> cb); // compat
    void readSingleParam(uint8_t localID, std::function<void(double)> cb); // compat

    // --- I/O ---
    QList<IODefinition> ioDefinitions() const;
    void readIOStates(std::function<void(const QList<IOState>&)> cb);

    // --- TCM Info (compat) ---
    void readTCMInfo(std::function<void(const QMap<QString,QString>&)> cb);

    // --- Raw ---
    void rawBusDump(Module mod, const QList<uint8_t> &ids,
                    std::function<void(uint8_t, const QByteArray&)> perID,
                    std::function<void()> done);
    void rawSendCommand(const QString &cmd, std::function<void(const QString&)> cb);

    // --- Status ---
    ECUStatus lastECUStatus() const { return m_lastECU; }
    TCMStatus lastTCMStatus() const { return m_lastTCM; }
    ABSStatus lastABSStatus() const { return m_lastABS; }
    QList<LiveParam> liveDataParams() const { return m_liveParams; }

signals:
    void logMessage(const QString &msg);
    void moduleReady(Module mod, bool success);
    void dtcListReady(Module mod, const QList<DTCEntry> &dtcs);
    void ecuStatusUpdated(const ECUStatus &st);
    void tcmStatusUpdated(const TCMStatus &st);
    void absStatusUpdated(const ABSStatus &st);

private:
    void parseECUBlock(uint8_t lid, const QByteArray &d, ECUStatus &ecu);
    void fillTCMCompat(TCMStatus &tcm);
    void initLiveDataParams();
    void _finishLegacySession(std::function<void(bool)> cb);
    QList<DTCEntry> decodeJ1850DTCs(const QString &resp, Module src);
    QList<DTCEntry> decodeKWPDTCs(const QByteArray &data, Module src);
    QString dtcDescription(const QString &code, Module src);

    ELM327Connection *m_elm;
    KWP2000Handler *m_kwp;
    Module m_activeModule = Module::MotorECU;
    BusType m_activeBus = BusType::KLine;

    ECUStatus m_lastECU;
    TCMStatus m_lastTCM;
    ABSStatus m_lastABS;
    QList<LiveParam> m_liveParams;
};

using TCMDiagnostics = WJDiagnostics;
