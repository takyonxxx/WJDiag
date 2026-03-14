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
// J1850 VPW (ATSP2): 0x28=ABS, 0x40=BodyComp, 0x58=Airbag/ESP, 0x60=ElecCluster(NRC), 0x61=Cluster, 0x68=Overhead, 0x98=HVAC/MemSeat, 0xA0=DriverDoor, 0xA1=PassDoor, 0xA7=Rain, 0xC0=SKIM
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

    // --- Module Addresses (Jeep WJ 2.7 CRD) ---
    enum class Module : uint8_t {
        // K-Line (ISO 14230-4 KWP2000)
        MotorECU     = 0x15,   // Bosch EDC15C2 OM612
        KLineTCM     = 0x20,   // NAG1 722.6 K-Line erişim
        // J1850 VPW
        ABS          = 0x28,   // ABS / TCM J1850
        EVIC         = 0x2A,   // Overhead Console / EVIC
        BodyComputer = 0x40,   // Body Computer (hazard/horn/mirrors)
        Airbag       = 0x60,   // Airbag (ORC/AOSIM)
        SKIM         = 0xC0,   // SKIM/Immobilizer 
        ATC          = 0x98,   // HVAC/ATC (APK verified, same as MemSeat)
        Radio80      = 0x80,   // Radio (NO DATA on EU)
        Radio        = 0x81,   // CD Changer
        // MemSeat uses same address as ATC (0x98)
        DriverDoor   = 0xA0,   // Driver Door (left windows)
        PassengerDoor= 0xA1,   // Passenger Door (right windows)
        ESP_Module  = 0x58,   // ESP/Traction Control
        Cluster      = 0x61,   // Instrument Cluster
        Overhead     = 0x68,   // Overhead Console
        Navigation   = 0x6D,   // Navigation System
        SatAudio     = 0x87,   // Satellite Audio
        HandsFree    = 0x90,   // Hands Free / Uconnect
        RainSensor   = 0xA7,   // Rain Sensor
        ParkAssist   = 0x62,   // Park Assist 
    };
    Q_ENUM(Module)

    enum class BusType { None, KLine, J1850 };

    // --- Data Structures ---
    struct ModuleInfo {
        Module id; QString name; QString shortName;
        BusType bus; QString atshHeader; QString atwmWakeup; QString atspProtocol;
        QString atraFilter; // ATRA receive filter (e.g. "ATRA40" for ABS)
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
        double fuelFlowLH=0;   // L/h instantaneous fuel consumption
        double fuelFlowGS=0;   // g/s total fuel mass flow
        double coolantTemp=0, iat=0, tps=0;
        double mapActual=0, aap=0;
        double mafActual=0, mafSpec=0;
        double railActual=0, railSpec=0, mapSpec=0;
        double injCorr[5]={0,0,0,0,0};
        double boostPressure=0, boostSetpoint=0;
        double batteryVoltage=0;
        // Block 0x10 - Idle/limits
        double idleRpmTarget=0, maxRpm=0;
        // Block 0x26 - Sensor raw
        double accelPedalRaw=0;
        // Block 0x30 - RPM setpoints
        double idleRpmSet=0;
        // Block 0x62 fields
        double egrDuty=0, wastegate=0;
        double alternatorDuty=0;
        bool glowPlug1=false, glowPlug2=false;
        double coolantSensorV=0, iatSensorV=0;
        double accelPedal1=0, accelPedal2=0;
        double vehicleSpeed=0;
        // Block 0xB0 - Injector corrections / adaptation
        double injLearn=0;
        double oilPressure=0;
        // Block 0xB1 - Boost adaptation
        double boostAdapt=0;
        double idleAdapt=0;
        // Block 0xB2 - Fuel trims / cold start
        double fuelAdapt=0;
        // Block 0x30 - RPM setpoints (APK: Engine RPM, Low Idle Setpoint)
        double lowIdleSetpoint=0;
        // Block 0x23 - Boost (APK: Boost Pressure Sensor/Voltage/Setpoint)
        double boostVoltage=0;
        // Block 0x21 - Fuel demand (APK: Desired/Actual Fuel QTY)
        double fuelDemand=0, fuelDriver=0, fuelActual=0;
        double fuelStartSet=0, fuelLimit=0, fuelTorque=0, fuelIdleGov=0;
        // Block 0x16 - Battery/alternator
        double batteryTemp=0, batteryTempV=0, alternatorField=0;
        // Block 0x32 - Vehicle speed
        double vehicleSpeedSet=0, cruiseSwitchV=0;
        // Block 0x37 - EGR
        double mafEgrSetpoint=0;
        // Block 0x13 - Oil/AC pressure
        double oilPressureV=0, acPressure=0, acPressureV=0;
        // Block 0x36 - Pedal sensors
        double pedalPos1=0, pedalPos2=0, pedalV1=0, pedalV2=0;
        double fuelQtyPedal=0, fuelQtyCruise=0;
        // Block 0x26 - Fuel level/pressure
        double fuelLevel=0, fuelLevelV=0, fuelRegOutput=0, fuelPressureV=0, fuelPressureSet=0;
        // Block 0x22 - Baro/temps (reinterpreted)
        double baroPressure=0, baroPressureV=0, outsideAirTemp=0, mafVoltage=0;
        // Block 0x34 - Transfer case
        double transferCaseV=0, camCrankSync=0, injBankCap=0;
    };

    struct TCMStatus {
        // Gear
        Gear currentGear = Gear::Unknown;
        int actualGear=0, selectedGear=0, maxGear=0;
        // Block 0x30 - TCC slip
        double desTccSlip=0, actualTccSlip=0;
        // Block 0x31 - Battery/supply
        double tcmBattery=0, sensorSupply=0, solenoidSupply=0;
        // Block 0x33 - Wheel speeds
        double lfWheelSpd=0, rfWheelSpd=0, lrWheelSpd=0, rrWheelSpd=0;
        double rearVehicleSpd=0, frontVehicleSpd=0;
        // Block 0x34 - Pressures
        double tccPressure=0, shiftPsi=0, modulationPsi=0;
        double tcmTpsPercent=0, uphillGrad=0;
        // RPM
        double turbineRpm=0, outputRPM=0;
        // Extra
        double transTemp=0;
        QString tccState;
        double vehicleSpeed=0;
        // Block 0x30 extra fields
        uint16_t engageStatus=0;    // byte[2-3]: P/N=30, D=50-55
        uint8_t solenoidMode=0;     // byte[18]: bitmask
        uint8_t gearByte=0;         // byte[7]: P=8,R=7,N=6,D=5
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
    void setActiveBus(BusType bus) { m_activeBus = bus; }

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
    void parseTCMBlock(uint8_t blk, const QByteArray &d, TCMStatus &tcm);
    void parseTCMBlock30(const QByteArray &raw, TCMStatus &tcm);
    void fillTCMCompat(TCMStatus &tcm);
    void initLiveDataParams();
    void _finishLegacySession(std::function<void(bool)> cb);
    QList<DTCEntry> decodeJ1850DTCs(const QString &resp, Module src);
    QList<DTCEntry> decodeKWPDTCs(const QByteArray &data, Module src);
    void readJ1850DTCsByPIDScan(Module mod, std::function<void(const QList<DTCEntry>&)> cb);
    QString dtcDescription(const QString &code, Module src);

    ELM327Connection *m_elm;
    KWP2000Handler *m_kwp;
    Module m_activeModule = Module::MotorECU;
    BusType m_activeBus = BusType::J1850;  // init: ATSP2 ile J1850 basliyor
    bool m_ecuSecurityUnlocked = false;

    ECUStatus m_lastECU;
    TCMStatus m_lastTCM;
    ABSStatus m_lastABS;
    QList<LiveParam> m_liveParams;
};

using TCMDiagnostics = WJDiagnostics;
