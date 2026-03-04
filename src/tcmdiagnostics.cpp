#include "tcmdiagnostics.h"
#include <QDebug>
#include <QTimer>

// ============================================================
// WJ 2.7 CRD Multi-Protocol Diagnostics
// WJ CRD verified: K-Line 0x15=Motor, 0x20=KLineTCM | J1850 0x28=TCM
// ============================================================

WJDiagnostics::WJDiagnostics(ELM327Connection *elm, QObject *parent)
    : QObject(parent), m_elm(elm)
{
    m_kwp = new KWP2000Handler(elm, this);
    connect(m_kwp, &KWP2000Handler::logMessage,
            this, &WJDiagnostics::logMessage);
    initLiveDataParams();
}

// --- Static Module Registry ---

QList<WJDiagnostics::ModuleInfo> WJDiagnostics::allModules()
{
    return {
        // K-Line modules (ISO 14230-4 KWP fast init)
        // Init sirasi: ATZ -> ATWM -> ATSH -> ATSP5 -> ATFI -> 81 -> 27
        {Module::MotorECU, "Engine ECU (Bosch EDC15C2 OM612)", "Engine",
         BusType::KLine, "ATSH8115F1", "ATWM8115F13E", "ATSP5"},
        {Module::KLineTCM, "NAG1 722.6 Transmission (K-Line)", "KL-TCM",
         BusType::KLine, "ATSH8120F1", "ATWM8120F13E", "ATSP5"},

        // J1850 VPW modules - dogrulanmis header'lar
        // Her modul icin ilk header fonksiyonel (session/reset), okuma icin 22 header
        {Module::TCM, "NAG1 722.6 Transmission (EGS52)", "TCM",
         BusType::J1850, "ATSH242810", "", "ATSP2"},
        {Module::EVIC, "Overhead Console / Pusula", "EVIC",
         BusType::J1850, "ATSH242A22", "", "ATSP2"},
        {Module::ABS, "ABS / ESP Braking", "ABS",
         BusType::J1850, "ATSH244022", "", "ATSP2"},
        {Module::Airbag, "Airbag (ORC/AOSIM)", "Airbag",
         BusType::J1850, "ATSH246022", "", "ATSP2"},
        {Module::SKIM, "SKIM Immobilizer", "SKIM",
         BusType::J1850, "ATSH246222", "", "ATSP2"},
        {Module::ATC, "Klima Kontrol (ATC/HVAC)", "Klima",
         BusType::J1850, "ATSH246822", "", "ATSP2"},
        {Module::BCM, "Govde Kontrol (BCM)", "BCM",
         BusType::J1850, "ATSH248022", "", "ATSP2"},
        {Module::Radio, "Radyo / Ses Sistemi", "Radyo",
         BusType::J1850, "ATSH248722", "", "ATSP2"},
        {Module::Cluster, "Gosterge Paneli", "Gosterge",
         BusType::J1850, "ATSH249022", "", "ATSP2"},
        {Module::MemSeat, "Hafizali Koltuk / Ayna", "Koltuk",
         BusType::J1850, "ATSH249822", "", "ATSP2"},
        {Module::Liftgate, "Power Liftgate", "Liftgate",
         BusType::J1850, "ATSH24A022", "", "ATSP2"},
        {Module::HandsFree, "HandsFree / Uconnect", "HFM",
         BusType::J1850, "ATSH24A122", "", "ATSP2"},
        {Module::ParkAssist, "Park Sensoru", "Park",
         BusType::J1850, "ATSH24C022", "", "ATSP2"},
    };
}

WJDiagnostics::ModuleInfo WJDiagnostics::moduleInfo(Module mod)
{
    for (const auto &m : allModules())
        if (m.id == mod) return m;
    return allModules().first();
}

QString WJDiagnostics::moduleName(Module mod)
{
    return moduleInfo(mod).shortName;
}

// --- Protocol & Module Switching ---

void WJDiagnostics::switchToModule(Module mod, std::function<void(bool)> done)
{
    const Module targetMod = mod;
    auto info = moduleInfo(mod);
    BusType newBus = info.bus;

    emit logMessage(QString("Module: %1 [%2]")
        .arg(info.name, info.bus == BusType::KLine ? "K-Line" : "J1850"));

    if (newBus != m_activeBus) {
        if (newBus == BusType::KLine) {
            // === Jeep WJ 2.7 CRD K-Line init sirasi ===
            // ATZ -> ATWM -> ATSH -> ATSP5 -> ATFI -> 81 -> 27
            // ATWM ve ATSH, ATSP5'ten ONCE gonderilir
            emit logMessage("K-Line switch: full ATZ reset...");
            m_elm->sendCommand("ATZ", [this, info, done, targetMod](const QString &atzResp) {
                if (atzResp.contains("TIMEOUT")) {
                    emit logMessage("ATZ timeout - ELM327 not responding!");
                    if (done) done(false);
                    return;
                }
                emit logMessage("ATZ OK, K-Line init starting...");

                // ATE1 + ATH1
                m_elm->sendCommand("ATE1", [this, info, done, targetMod](const QString&) {
                m_elm->sendCommand("ATH1", [this, info, done, targetMod](const QString&) {

                // ATWM - wakeup message (ATSP5'ten ONCE)
                auto afterWakeup = [this, info, done, targetMod]() {
                    // ATSH - header set (ATSP5'ten ONCE)
                    m_elm->sendCommand(info.atshHeader, [this, info, done, targetMod](const QString&) {

                    // ATSP5 - K-Line protokol sec
                    m_elm->sendCommand(info.atspProtocol, [this, info, done, targetMod](const QString &sp5) {
                        if (sp5.contains("TIMEOUT") || sp5.contains("ERROR")) {
                            emit logMessage("ATSP5 failed - reverting to J1850");
                            m_elm->sendCommand("ATSP2", [this, done, targetMod](const QString&) {
                                m_activeBus = BusType::J1850;
                                if (done) done(false);
                            });
                            return;
                        }

                    // ATFI - Fast Init (bus init)
                    m_elm->sendCommand("ATFI", [this, info, done, targetMod](const QString &fi) {
                        // Check ERROR first - "BUS INIT: ERROR" contains "BUS INIT" too!
                        if (fi.contains("ERROR") || fi.contains("TIMEOUT") || fi.contains("?")) {
                            emit logMessage("ATFI failed: " + fi);
                            // Save previous module before recovery
                            Module prevMod = m_activeModule;
                            // Recovery: ATZ -> ATSP2 -> restore previous ATSH
                            m_elm->sendCommand("ATZ", [this, done, targetMod, prevMod](const QString&) {
                                QTimer::singleShot(500, this, [this, done, targetMod, prevMod]() {
                                    m_elm->sendCommand("ATE1", [this, done, targetMod, prevMod](const QString&) {
                                    m_elm->sendCommand("ATH1", [this, done, targetMod, prevMod](const QString&) {
                                    m_elm->sendCommand("ATSP2", [this, done, targetMod, prevMod](const QString&) {
                                        m_activeBus = BusType::J1850;
                                        // Restore previous J1850 module header if it was active
                                        auto prevInfo = moduleInfo(prevMod);
                                        if (prevInfo.bus == BusType::J1850 && !prevInfo.atshHeader.isEmpty()) {
                                            m_elm->sendCommand(prevInfo.atshHeader, [this, done, prevMod, prevInfo](const QString&) {
                                                m_activeModule = prevMod;
                                                emit logMessage(QString("ATFI failed - restored %1 | %2")
                                                    .arg(prevInfo.shortName, prevInfo.atshHeader));
                                                if (done) done(false);
                                            });
                                        } else {
                                            emit logMessage("ATFI failed - reverted to J1850");
                                            if (done) done(false);
                                        }
                                    });
                                    });
                                    });
                                });
                            });
                        } else if (fi.contains("BUS INIT") || fi.contains("OK")) {
                            emit logMessage("ATFI OK - K-Line bus init successful!");
                            m_activeModule = targetMod;
                            m_activeBus = BusType::KLine;

                            // 81 - StartCommunication (KWP SID)
                            m_elm->sendCommand("81", [this, info, done, targetMod](const QString &sc) {
                                emit logMessage("StartComm(81): " + sc);

                                // SecurityAccess (27 01 -> seed, 27 02 CD46 -> key)
                                m_elm->sendCommand("27 01", [this, done, targetMod](const QString &seed) {
                                    emit logMessage("Security seed: " + seed);
                                    if (seed.contains("67 01")) {
                                        m_elm->sendCommand("27 02 CD 46", [this, done, targetMod](const QString &key) {
                                            emit logMessage("Security key: " + key);
                                            emit logMessage("K-Line session active (ECU ready)");
                                            if (done) done(true);
                                        });
                                    } else {
                                        emit logMessage("Security skipped, continuing");
                                        if (done) done(true);
                                    }
                                });
                            });
                        } else {
                            // Unknown response - try to continue
                            emit logMessage("ATFI response: " + fi + " (continuing)");
                            m_activeModule = targetMod;
                            m_activeBus = BusType::KLine;
                            if (done) done(true);
                        }
                    }, 5000); // ATFI timeout 5s
                    });
                    });
                };

                if (!info.atwmWakeup.isEmpty()) {
                    m_elm->sendCommand(info.atwmWakeup, [afterWakeup](const QString&) {
                        afterWakeup();
                    });
                } else {
                    afterWakeup();
                }

                });
                });
            }, 7500); // ATZ timeout
        } else {
            // J1850'ye gecis - ATZ ile temiz baslat + ATIFR0
            emit logMessage("J1850 switch: ATZ reset...");
            m_elm->sendCommand("ATZ", [this, info, done, targetMod](const QString&) {
                m_elm->sendCommand("ATE1", [this, info, done, targetMod](const QString&) {
                m_elm->sendCommand("ATH1", [this, info, done, targetMod](const QString&) {
                m_elm->sendCommand("ATIFR0", [this, info, done, targetMod](const QString&) {
                m_elm->sendCommand("ATSP2", [this, info, done, targetMod](const QString&) {
                    m_activeBus = BusType::J1850;
                    emit logMessage("J1850 VPW active");
                    // Header set
                    m_elm->sendCommand(info.atshHeader, [this, info, done, targetMod](const QString&) {
                        m_activeModule = targetMod;
                        emit logMessage(QString("Active: %1 | %2").arg(info.shortName, info.atshHeader));
                        if (done) done(true);
                    });
                });
                });
                });
                });
            }, 7500);
        }
    } else {
        // Ayni bus - sadece header degistir
        m_elm->sendCommand(info.atshHeader, [this, info, done, targetMod](const QString&) {
            m_activeModule = targetMod;
            emit logMessage(QString("Active: %1 | %2").arg(info.shortName, info.atshHeader));
            if (done) done(true);
        });
    }
}

// --- Session ---

void WJDiagnostics::startSession(Module mod, std::function<void(bool)> cb)
{
    switchToModule(mod, [this, mod, cb](bool ok) {
        if (!ok) { if (cb) cb(false); return; }
        if (moduleInfo(mod).bus == BusType::KLine) {
            m_kwp->startDiagnosticSession(KWP2000Handler::DefaultSession,
                [this, mod, cb](bool s) {
                    emit moduleReady(mod, s);
                    if (cb) cb(s);
                });
        } else {
            emit moduleReady(mod, true);
            if (cb) cb(true);
        }
    });
}

void WJDiagnostics::stopSession()
{
    if (m_activeBus == BusType::KLine)
        m_kwp->startDiagnosticSession(KWP2000Handler::DefaultSession, nullptr);
}

// --- DTC Read/Clear ---

void WJDiagnostics::readDTCs(Module mod, std::function<void(const QList<DTCEntry>&)> cb)
{
    switchToModule(mod, [this, mod, cb](bool ok) {
        if (!ok) { if (cb) cb({}); return; }

        if (moduleInfo(mod).bus == BusType::KLine) {
            m_kwp->readAllDTCs([this, mod, cb](const QList<KWP2000Handler::DTCInfo> &kwp) {
                QList<DTCEntry> r;
                for (const auto &d : kwp) {
                    DTCEntry e;
                    e.code = d.codeStr; e.status = d.status;
                    e.isActive = d.isActive; e.occurrences = d.occurrences;
                    e.description = dtcDescription(d.codeStr, mod);
                    e.source = mod;
                    r.append(e);
                }
                emit dtcListReady(mod, r);
                if (cb) cb(r);
            });
        } else {
            // J1850 VPW DTC read
            m_elm->sendCommand("18 02 FF 00", [this, mod, cb](const QString &resp) {
                auto dtcs = decodeJ1850DTCs(resp, mod);
                emit dtcListReady(mod, dtcs);
                if (cb) cb(dtcs);
            });
        }
    });
}

void WJDiagnostics::clearDTCs(Module mod, std::function<void(bool)> cb)
{
    switchToModule(mod, [this, mod, cb](bool ok) {
        if (!ok) { if (cb) cb(false); return; }

        if (moduleInfo(mod).bus == BusType::KLine) {
            m_kwp->clearAllDTCs(cb);
        } else {
            m_elm->sendCommand("14 00 00", [this, mod, cb](const QString &resp) {
                bool ok = !resp.contains("7F") && !resp.contains("ERROR");
                emit logMessage(QString("%1 DTC sil: %2")
                    .arg(moduleName(mod), ok ? "OK" : "FAIL"));
                if (cb) cb(ok);
            });
        }
    });
}

void WJDiagnostics::readModuleInfo(Module mod, std::function<void(const QMap<QString,QString>&)> cb)
{
    switchToModule(mod, [this, mod, cb](bool ok) {
        if (!ok) { if (cb) cb({}); return; }

        auto r = std::make_shared<QMap<QString,QString>>();
        auto info = moduleInfo(mod);
        (*r)["Module"] = info.name;
        (*r)["Bus"] = (info.bus == BusType::KLine) ? "K-Line" : "J1850 VPW";

        if (info.bus == BusType::KLine) {
            auto n = std::make_shared<int>(3);
            auto chk = [r, n, cb]() { if (--(*n) <= 0 && cb) cb(*r); };
            m_kwp->readECUIdentification(0x91, [r, chk](const QByteArray &d) {
                if (d.size() > 2) (*r)["ECU_ID"] = QString::fromLatin1(d.mid(2));
                chk();
            });
            m_kwp->readECUIdentification(0x90, [r, chk](const QByteArray &d) {
                if (d.size() > 2) (*r)["VIN"] = QString::fromLatin1(d.mid(2));
                chk();
            });
            m_kwp->readECUIdentification(0x86, [r, chk](const QByteArray &d) {
                if (d.size() > 2) (*r)["Variant"] = QString::fromLatin1(d.mid(2));
                chk();
            });
        } else {
            if (cb) cb(*r);
        }
    });
}

// --- ECU Live Data (K-Line 0x15) ---

void WJDiagnostics::readECULiveData(std::function<void(const ECUStatus&)> cb)
{
    auto ecu = std::make_shared<ECUStatus>();
    auto step = std::make_shared<int>(0);
    auto doNext = std::make_shared<std::function<void()>>();

    *doNext = [this, ecu, step, doNext, cb]() {
        uint8_t ids[] = {0x12, 0x28, 0x20, 0x22, 0x62, 0xB0, 0xB1, 0xB2};
        if (*step >= 8) {
            // Read battery voltage via ATRV after all ECU blocks
            m_elm->sendCommand("ATRV", [this, ecu, cb](const QString &rv) {
                // Parse "12.6V" or "13.8V"
                QString v = rv.trimmed().remove('V').remove('v');
                bool ok = false;
                double volts = v.toDouble(&ok);
                if (ok && volts > 0) ecu->batteryVoltage = volts;
                m_lastECU = *ecu;
                emit ecuStatusUpdated(*ecu);
                if (cb) cb(*ecu);
            });
            return;
        }
        m_kwp->readLocalData(ids[*step], [this, ecu, step, doNext](const QByteArray &data) {
            if (!data.isEmpty()) {
                uint8_t ids2[] = {0x12, 0x28, 0x20, 0x22, 0x62};
                parseECUBlock(ids2[*step], data, *ecu);
            }
            (*step)++;
            QTimer::singleShot(340, *doNext);
        });
    };

    switchToModule(Module::MotorECU, [doNext](bool ok) {
        if (ok) (*doNext)();
    });
}

// --- TCM Live Data (J1850 VPW 0x28) ---

void WJDiagnostics::readTCMLiveData(std::function<void(const TCMStatus&)> cb)
{
    // J1850 VPW: ATSH242822 for ReadDataByPID
    // PIDs need real vehicle verification
    auto tcm = std::make_shared<TCMStatus>();

    switchToModule(Module::TCM, [this, tcm, cb](bool ok) {
        if (!ok) { if (cb) cb(*tcm); return; }
        // Switch to data-read header
        m_elm->sendCommand("ATSH242822", [this, tcm, cb](const QString&) {
            // Read basic PIDs sequentially
            auto step = std::make_shared<int>(0);
            auto doNext = std::make_shared<std::function<void()>>();

            // Chrysler J1850 PID pairs: cmd -> parse
            struct PIDRead { QString cmd; QString name; };
            auto pids = std::make_shared<QList<PIDRead>>(QList<PIDRead>{
                {"2201", "Actual Gear"},
                {"2202", "Selected Gear"},
                {"2210", "Turbine RPM"},
                {"2211", "Output RPM"},
                {"2214", "Trans Temp"},
                {"2220", "Vehicle Speed"},
            });

            *doNext = [this, tcm, step, doNext, pids, cb]() {
                if (*step >= pids->size()) {
                    m_lastTCM = *tcm;
                    emit tcmStatusUpdated(*tcm);
                    if (cb) cb(*tcm);
                    return;
                }
                auto &p = pids->at(*step);
                m_elm->sendCommand(p.cmd, [this, tcm, step, doNext, p](const QString &resp) {
                    QString c = resp; c.remove(' ').remove('\r').remove('\n');
                    if (!c.contains("NODATA") && !c.contains("7F") && c.size() >= 6) {
                        bool ok;
                        if (p.cmd == "2201") {
                            int v = c.mid(4,2).toInt(&ok,16); if(ok) tcm->actualGear = v;
                        } else if (p.cmd == "2202") {
                            int v = c.mid(4,2).toInt(&ok,16); if(ok) tcm->selectedGear = v;
                        } else if (p.cmd == "2210") {
                            int v = c.mid(4,4).toInt(&ok,16); if(ok) tcm->turbineRPM = v;
                        } else if (p.cmd == "2211") {
                            int v = c.mid(4,4).toInt(&ok,16); if(ok) tcm->outputRPM = v;
                        } else if (p.cmd == "2214") {
                            int v = c.mid(4,2).toInt(&ok,16); if(ok) tcm->transTemp = v - 40;
                        } else if (p.cmd == "2220") {
                            int v = c.mid(4,4).toInt(&ok,16); if(ok) tcm->vehicleSpeed = v;
                        }
                    }
                    (*step)++;
                    QTimer::singleShot(340, *doNext);
                });
            };
            (*doNext)();
        });
    });
}

// --- ABS Live Data (J1850 VPW 0x40) ---

void WJDiagnostics::readABSLiveData(std::function<void(const ABSStatus&)> cb)
{
    auto abs = std::make_shared<ABSStatus>();

    switchToModule(Module::ABS, [this, abs, cb](bool ok) {
        if (!ok) { if (cb) cb(*abs); return; }
        // Switch to data-read header for ABS
        m_elm->sendCommand("ATSH244022", [this, abs, cb](const QString&) {
            auto step = std::make_shared<int>(0);
            auto doNext = std::make_shared<std::function<void()>>();

            // WJ CRD live data params: LF/RF/LR/RR Wheel Speed, Vehicle Speed
            struct PIDRead { QString cmd; QString name; };
            auto pids = std::make_shared<QList<PIDRead>>(QList<PIDRead>{
                {"2201", "LF Wheel Speed"},
                {"2202", "RF Wheel Speed"},
                {"2203", "LR Wheel Speed"},
                {"2204", "RR Wheel Speed"},
                {"2210", "Vehicle Speed"},
            });

            *doNext = [this, abs, step, doNext, pids, cb]() {
                if (*step >= pids->size()) {
                    m_lastABS = *abs;
                    emit absStatusUpdated(*abs);
                    if (cb) cb(*abs);
                    return;
                }
                auto &p = pids->at(*step);
                m_elm->sendCommand(p.cmd, [this, abs, step, doNext, p](const QString &resp) {
                    QString c = resp; c.remove(' ').remove('\r').remove('\n');
                    if (!c.contains("NODATA") && !c.contains("7F") && c.size() >= 6) {
                        bool ok;
                        int v = c.mid(4, 4).toInt(&ok, 16);
                        if (!ok) v = c.mid(4, 2).toInt(&ok, 16);
                        if (ok) {
                            if (p.cmd == "2201") abs->wheelLF = v;
                            else if (p.cmd == "2202") abs->wheelRF = v;
                            else if (p.cmd == "2203") abs->wheelLR = v;
                            else if (p.cmd == "2204") abs->wheelRR = v;
                            else if (p.cmd == "2210") abs->vehicleSpeed = v;
                        }
                    }
                    (*step)++;
                    QTimer::singleShot(340, *doNext);
                });
            };
            (*doNext)();
        });
    });
}

// --- Raw Bus Dump ---

void WJDiagnostics::rawBusDump(Module mod, const QList<uint8_t> &ids,
                                 std::function<void(uint8_t, const QByteArray&)> perID,
                                 std::function<void()> done)
{
    auto idx = std::make_shared<int>(0);
    auto idList = std::make_shared<QList<uint8_t>>(ids);
    auto readNext = std::make_shared<std::function<void()>>();
    auto busType = moduleInfo(mod).bus;

    *readNext = [this, idx, idList, readNext, perID, done, busType]() {
        if (*idx >= idList->size()) { if (done) done(); return; }
        uint8_t lid = idList->at(*idx);

        if (busType == BusType::KLine) {
            m_kwp->readLocalData(lid, [idx, readNext, perID, lid](const QByteArray &data) {
                if (perID) perID(lid, data);
                (*idx)++;
                QTimer::singleShot(340, *readNext);
            });
        } else {
            QString cmd = QString("22%1").arg(lid, 2, 16, QChar('0'));
            m_elm->sendCommand(cmd, [idx, readNext, perID, lid](const QString &resp) {
                QByteArray data;
                // Skip invalid responses
                if (!resp.contains("NO DATA") && !resp.contains("ERROR")
                    && !resp.contains("?") && !resp.contains("UNABLE")) {
                    QString c = resp; c.remove(' ').remove('\r').remove('\n');
                    for (int i = 0; i + 1 < c.size(); i += 2) {
                        bool ok; uint8_t b = c.mid(i, 2).toUInt(&ok, 16);
                        if (ok) data.append(static_cast<char>(b));
                    }
                }
                if (perID) perID(lid, data);
                (*idx)++;
                QTimer::singleShot(340, *readNext);
            });
        }
    };

    switchToModule(mod, [this, readNext, done, mod](bool ok) {
        if (ok) {
            (*readNext)();
        } else {
            emit logMessage(QString("rawBusDump: failed to switch to %1")
                .arg(moduleInfo(mod).shortName));
            if (done) done();
        }
    });
}

void WJDiagnostics::rawSendCommand(const QString &cmd, std::function<void(const QString&)> cb)
{
    m_elm->sendCommand(cmd, [cb](const QString &r) { if (cb) cb(r); });
}

// --- ECU Block Parser ---

void WJDiagnostics::parseECUBlock(uint8_t localID, const QByteArray &d, ECUStatus &ecu)
{
    int n = d.size();
    auto u8 = [&](int i) -> uint8_t { return (i < n) ? static_cast<uint8_t>(d[i]) : 0; };
    auto u16 = [&](int i) -> uint16_t { return (uint16_t(u8(i)) << 8) | u8(i+1); };
    auto s16 = [&](int i) -> int16_t { return static_cast<int16_t>(u16(i)); };

    switch (localID) {
    case 0x12:
        if (n >= 34) {
            ecu.coolantTemp = u16(2) / 10.0 - 273.1;
            ecu.iat = u16(4) / 10.0 - 273.1;
            ecu.tps = u16(14) / 100.0;              // Java: byte 14, /100
            ecu.mapActual = u16(18);                 // mbar
            ecu.railActual = u16(20) / 10.0;         // Java: byte 20, /10 -> bar
            ecu.aap = u16(30);                       // mbar (barometric)
            emit logMessage(QString("ECU 2112: cool=%1 iat=%2 tps=%3% map=%4 rail=%5bar aap=%6")
                .arg(ecu.coolantTemp,0,'f',1).arg(ecu.iat,0,'f',1)
                .arg(ecu.tps,0,'f',1).arg(ecu.mapActual)
                .arg(ecu.railActual,0,'f',1).arg(ecu.aap));
        }
        break;
    case 0x20:
        if (n >= 18) {
            ecu.mafActual = u16(14);
            ecu.mafSpec = u16(16);
            emit logMessage(QString("ECU 2120: maf=%1/%2").arg(ecu.mafActual).arg(ecu.mafSpec));
        }
        break;
    case 0x22:
        if (n >= 34) {
            ecu.railSpec = u16(18) / 10.0;
            ecu.mapSpec = u16(16);
            emit logMessage(QString("ECU 2122: rail=%1bar mapSpec=%2")
                .arg(ecu.railSpec,0,'f',1).arg(ecu.mapSpec));
        }
        break;
    case 0x28:
        if (n >= 28) {
            ecu.rpm = u16(2);
            ecu.injectionQty = u16(4) / 100.0;
            for (int i = 0; i < 5; i++)
                ecu.injCorr[i] = s16(18 + i*2) / 100.0;
            emit logMessage(QString("ECU 2128: rpm=%1 iq=%2 inj=[%3,%4,%5,%6,%7]")
                .arg(ecu.rpm).arg(ecu.injectionQty,0,'f',1)
                .arg(ecu.injCorr[0],0,'f',2).arg(ecu.injCorr[1],0,'f',2)
                .arg(ecu.injCorr[2],0,'f',2).arg(ecu.injCorr[3],0,'f',2)
                .arg(ecu.injCorr[4],0,'f',2));
        }
        break;
    case 0x62:
        if (n >= 8) {
            ecu.egrDuty = u8(2);
            ecu.wastegate = u8(3);
            ecu.glowPlug1 = u8(4) != 0;
            ecu.glowPlug2 = u8(5) != 0;
            ecu.mafActual = u16(6);
            if (n >= 9)
                ecu.alternatorDuty = u8(8);
            emit logMessage(QString("ECU 2162: egr=%1% wg=%2% maf=%3 alt=%4%")
                .arg(ecu.egrDuty).arg(ecu.wastegate)
                .arg(ecu.mafActual).arg(ecu.alternatorDuty));
        }
        break;
    case 0xB0:
        // Block B0: Injector corrections & adaptation (EDC15C2)
        // Byte layout based on Bosch EDC15C2 documentation
        if (n >= 12) {
            ecu.injCorr[0] = s16(2) / 100.0;  // Injector 1 correction (mg/stroke)
            ecu.injCorr[1] = s16(4) / 100.0;  // Injector 2 correction
            ecu.injCorr[2] = s16(6) / 100.0;  // Injector 3 correction
            ecu.injCorr[3] = s16(8) / 100.0;  // Injector 4 correction
            ecu.injCorr[4] = s16(10) / 100.0; // Injector 5 correction
            if (n >= 14)
                ecu.injLearn = u8(12);         // Injector learn status
            if (n >= 16)
                ecu.oilPressure = u8(14) * 0.5; // Oil pressure (bar)
            emit logMessage(QString("ECU 21B0: inj1=%1 inj2=%2 inj3=%3 inj4=%4 inj5=%5 learn=%6 oil=%7bar")
                .arg(ecu.injCorr[0],0,'f',2).arg(ecu.injCorr[1],0,'f',2)
                .arg(ecu.injCorr[2],0,'f',2).arg(ecu.injCorr[3],0,'f',2)
                .arg(ecu.injCorr[4],0,'f',2).arg(ecu.injLearn)
                .arg(ecu.oilPressure,0,'f',1));
        }
        break;
    case 0xB1:
        // Block B1: Boost & idle adaptation
        if (n >= 6) {
            ecu.boostAdapt = s16(2) / 10.0;   // Boost adaptation (mbar)
            ecu.idleAdapt = s16(4) / 10.0;     // Idle speed adaptation (RPM)
            emit logMessage(QString("ECU 21B1: boostAdapt=%1mbar idleAdapt=%2rpm")
                .arg(ecu.boostAdapt,0,'f',1).arg(ecu.idleAdapt,0,'f',1));
        }
        break;
    case 0xB2:
        // Block B2: Fuel quantity adaptation
        if (n >= 4) {
            ecu.fuelAdapt = s16(2) / 100.0;   // Fuel adaptation (mg/stroke)
            emit logMessage(QString("ECU 21B2: fuelAdapt=%1mg")
                .arg(ecu.fuelAdapt,0,'f',2));
        }
        break;
    }
}

// --- J1850 DTC Decoder ---

QList<WJDiagnostics::DTCEntry> WJDiagnostics::decodeJ1850DTCs(const QString &resp, Module src)
{
    QList<DTCEntry> result;
    QString c = resp; c.remove(' ').remove('\r').remove('\n');
    if (c.contains("NODATA") || c.contains("ERROR") || c.size() < 6)
        return result;

    // ATH1 modunda header byte'lari var: "281058..." veya "281058xx..."
    // "58" (positive response SID 0x18+0x40) byte'ini bul
    int pos58 = c.indexOf("58", 0, Qt::CaseInsensitive);
    if (pos58 < 0 || pos58 % 2 != 0) return result;

    c = c.mid(pos58); // "58" den itibaren al

    // 58 <count> <DTC_HI DTC_LO STATUS> ...
    // count byte'ini al
    if (c.size() < 4) return result;
    int count = c.mid(2, 2).toUInt(nullptr, 16);

    int start = 4; // "58 CC" den sonra
    for (int i = 0; i < count && start + 5 < c.size(); i++, start += 6) {
        bool ok;
        uint8_t hi = c.mid(start, 2).toUInt(&ok, 16);
        if (!ok) continue;
        uint8_t lo = c.mid(start + 2, 2).toUInt(&ok, 16);
        if (!ok) continue;
        uint8_t status = c.mid(start + 4, 2).toUInt(&ok, 16);
        if (!ok) continue;

        uint16_t raw = (hi << 8) | lo;
        if (raw == 0) continue;

        DTCEntry e;
        char pfx;
        switch ((raw >> 14) & 3) {
        case 0: pfx='P'; break; case 1: pfx='C'; break;
        case 2: pfx='B'; break; default: pfx='U';
        }
        e.code = QString("%1%2").arg(pfx).arg(raw & 0x3FFF, 4, 16, QChar('0')).toUpper();
        e.description = dtcDescription(e.code, src);
        e.status = status;
        e.isActive = (status & 0x01) != 0;
        e.occurrences = 1;
        e.source = src;
        result.append(e);
    }
    return result;
}

QList<WJDiagnostics::DTCEntry> WJDiagnostics::decodeKWPDTCs(const QByteArray &data, Module src)
{
    Q_UNUSED(data) Q_UNUSED(src) return {};
}

// --- DTC Descriptions (analizden) ---

QString WJDiagnostics::dtcDescription(const QString &code, Module src)
{
    static const QMap<QString, QString> ecuDtcs = {
        {"P0100","MAF Sensor Circuit"}, {"P0105","Barometric Pressure Sensor"},
        {"P0110","Air Intake Temp Sensor"}, {"P0115","Coolant Temp Sensor"},
        {"P0190","Fuel Rail Pressure Sensor"}, {"P0201","Cyl 1 Injector Circuit"},
        {"P0202","Cyl 2 Injector Circuit"}, {"P0203","Cyl 3 Injector Circuit"},
        {"P0204","Cyl 4 Injector Circuit"}, {"P0205","Cyl 5 Injector Circuit"},
        {"P0335","CKP Position Sensor"}, {"P0340","CMP Position Sensor"},
        {"P0380","Glow Plug Circuit"}, {"P0403","EGR Solenoid Circuit"},
        {"P0500","Vehicle Speed Sensor"}, {"P1130","Boost Pressure Sensor"},
        {"P2602","Fuel Pressure Solenoid"},
    };

    static const QMap<QString, QString> tcmDtcs = {
        {"P0700","Transmission Control System"}, {"P0705","Range Sensor Circuit"},
        {"P0710","Trans Fluid Temp Sensor"}, {"P0715","Input Speed Sensor"},
        {"P0720","Output Speed Sensor"}, {"P0730","Incorrect Gear Ratio"},
        {"P0731","Gear 1 Ratio Error"}, {"P0732","Gear 2 Ratio Error"},
        {"P0733","Gear 3 Ratio Error"}, {"P0734","Gear 4 Ratio Error"},
        {"P0735","Gear 5 Ratio Error"}, {"P0740","TCC Solenoid Circuit"},
        {"P0741","TCC Stuck On"}, {"P0748","Pressure Solenoid Electrical"},
        {"P0750","Shift Solenoid A"}, {"P0755","Shift Solenoid B"},
        {"P0760","Shift Solenoid C"}, {"P0765","Shift Solenoid D"},
        {"P0780","Shift Error"}, {"P0894","Transmission Slipping"},
    };

    // ABS DTCs (dogrulanmis - Chrysler C-codes + standard)
    static const QMap<QString, QString> absDtcs = {
        {"C0031","Left Front Sensor Circuit Failure"},
        {"C0032","Left Front Wheel Speed Signal Failure"},
        {"C0035","Right Front Sensor Circuit Failure"},
        {"C0036","Right Front Wheel Speed Signal Failure"},
        {"C0041","Left Rear Sensor Circuit Failure"},
        {"C0042","Left Rear Wheel Speed Signal Failure"},
        {"C0045","Right Rear Sensor Circuit Failure"},
        {"C0046","Right Rear Wheel Speed Signal Failure"},
        {"C0051","Valve Power Feed Failure"},
        {"C0060","Pump Motor Circuit Failure"},
        {"C0070","CAB Internal Failure"},
        {"C0080","ABS Lamp Circuit Short"},
        {"C0081","ABS Lamp Open"},
        {"C0085","Brake Lamp Circuit Short"},
        {"C0086","Brake Lamp Open"},
        {"C0110","Brake Fluid Level Switch"},
        {"C0111","G-Switch / Sensor Failure"},
        {"C1014","ABS Messages Not Received"},
        {"C1015","No BCM Park Brake Messages Received"},
    };

    // Airbag/ORC DTCs (dogrulanmis - B-codes)
    static const QMap<QString, QString> airbagDtcs = {
        {"B1000","Airbag Lamp Driver Failure"},
        {"B1001","Airbag Lamp Open"},
        {"B1010","Driver SQUIB 1 Circuit Open"},
        {"B1011","Driver SQUIB 1 Circuit Short"},
        {"B1012","Driver SQUIB 1 Short To Battery"},
        {"B1013","Driver SQUIB 1 Short To Ground"},
        {"B1014","Driver Squib 2 Circuit Open"},
        {"B1015","Driver SQUIB 2 Circuit Short"},
        {"B1016","Driver SQUIB 2 Short To Battery"},
        {"B1017","Driver SQUIB 2 Short To Ground"},
        {"B1020","Passenger SQUIB 1 Circuit Open"},
        {"B1021","Passenger SQUIB 1 Circuit Short"},
        {"B1022","Passenger SQUIB 1 Short To Battery"},
        {"B1023","Passenger SQUIB 1 Short To Ground"},
        {"B1024","Passenger SQUIB 2 Circuit Open"},
        {"B1025","Passenger SQUIB 2 Circuit Short"},
        {"B1026","Passenger SQUIB 2 Short To Battery"},
        {"B1027","Passenger SQUIB 2 Short To Ground"},
        {"B1030","Driver Curtain SQUIB Circuit Open"},
        {"B1031","Driver Curtain SQUIB Short To Battery"},
        {"B1032","Driver Curtain SQUIB Short To Ground"},
        {"B1033","Passenger Curtain SQUIB Circuit Open"},
        {"B1034","Passenger Curtain SQUIB Circuit Short"},
        {"B1035","Passenger Curtain SQUIB Short To Battery"},
        {"B1036","Passenger Curtain SQUIB Short To Ground"},
        {"B1040","Driver Side Impact Sensor Internal 1"},
        {"B1041","No Driver Side Impact Sensor Communication"},
        {"B1042","Passenger Side Impact Sensor Internal 1"},
        {"B1043","No Passenger Side Impact Sensor Communication"},
        {"B1050","Driver Seat Belt Switch Circiut Open"},
        {"B1051","Driver Seat Belt Switch Shorted To Battery"},
        {"B1052","Driver Seat Belt Switch Shorted To Ground"},
        {"B1053","Passenger Seat Belt Switch Circiut Open"},
        {"B1054","Passenger Seat Belt Switch Shorted To Battery"},
        {"B1055","Passenger Seat Belt Switch Shorted To Ground"},
    };

    // BCM DTCs
    static const QMap<QString, QString> bcmDtcs = {
        {"B1A00","Interior Lamp Circuit"},
        {"B1A10","Door Ajar Switch Circuit"},
        {"B2100","SKIM Communication Error"},
    };

    // Lookup by source module first, then generic
    if (src == Module::MotorECU || src == Module::KLineTCM) {
        if (ecuDtcs.contains(code)) return ecuDtcs[code];
    }
    if (src == Module::TCM) {
        if (tcmDtcs.contains(code)) return tcmDtcs[code];
    }
    if (src == Module::ABS) {
        if (absDtcs.contains(code)) return absDtcs[code];
    }
    if (src == Module::Airbag) {
        if (airbagDtcs.contains(code)) return airbagDtcs[code];
    }
    if (src == Module::BCM) {
        if (bcmDtcs.contains(code)) return bcmDtcs[code];
    }

    // Generic fallback - search all maps
    if (ecuDtcs.contains(code)) return ecuDtcs[code];
    if (tcmDtcs.contains(code)) return tcmDtcs[code];
    if (absDtcs.contains(code)) return absDtcs[code];
    if (airbagDtcs.contains(code)) return airbagDtcs[code];
    if (bcmDtcs.contains(code)) return bcmDtcs[code];
    return "";
}

// ============================================================
// Compat Methods (eski mainwindow/livedata API uyumu icin)
// ============================================================

// startSession(callback) - compat: referansna gore J1850 VPW TCM (0x28)
void WJDiagnostics::startSession(std::function<void(bool)> cb)
{
    emit logMessage("Starting TCM session (J1850 VPW 0x28)...");
    m_activeBus = BusType::J1850;
    m_activeModule = Module::TCM;

    m_elm->sendCommand("ATSP2", [this, cb](const QString&) {
        QTimer::singleShot(340, this, [this, cb]() {
            m_elm->sendCommand("ATSH242810", [this, cb](const QString&) {
                // J1850'de session baslat: SID 0x10 0x89 (DiagSession)
                m_elm->sendCommand("10 89", [this, cb](const QString &resp) {
                    bool ok = !resp.contains("ERROR") && !resp.contains("NO DATA") && !resp.contains("?");
                    if (ok) {
                        emit logMessage("TCM J1850 session active (ATSH242810)");
                        initLiveDataParams();
                    } else {
                        // Bazi moduller session gerektirmez, yine de devam et
                        emit logMessage("TCM J1850 session yaniti: " + resp + " (continuing)");
                        initLiveDataParams();
                        ok = true;
                    }
                    if (cb) cb(ok);
                });
            });
        });
    });
}

void WJDiagnostics::_finishLegacySession(std::function<void(bool)> cb)
{
    // Artik kullanilmiyor (J1850'ye gecildi)
    if (cb) cb(false);
}

// readDTCs(callback) - compat: aktif modul
void WJDiagnostics::readDTCs(std::function<void(const QList<KWP2000Handler::DTCInfo>&)> cb)
{
    Module mod = m_activeModule;
    readDTCs(mod, [this, cb](const QList<DTCEntry> &dtcs) {
        QList<KWP2000Handler::DTCInfo> kwpDtcs;
        for (const auto &d : dtcs) {
            KWP2000Handler::DTCInfo k;
            k.codeStr = d.code;
            k.description = d.description;
            k.status = d.status;
            k.isActive = d.isActive;
            k.occurrences = d.occurrences;
            k.code = 0;
            k.isStored = !d.isActive;
            kwpDtcs.append(k);
        }
        if (cb) cb(kwpDtcs);
    });
}

// clearDTCs(callback) - compat: aktif modul
void WJDiagnostics::clearDTCs(std::function<void(bool)> cb)
{
    clearDTCs(m_activeModule, cb);
}

// readAllLiveData - J1850 VPW uzerinden TCM PID'lerden oku (referans)
void WJDiagnostics::readAllLiveData(std::function<void(const TCMStatus&)> cb)
{
    auto tcm = std::make_shared<TCMStatus>();
    auto step = std::make_shared<int>(0);
    auto doNext = std::make_shared<std::function<void()>>();

    // analizden cikarilan J1850 TCM PID'ler - TUMU
    // Header ATSH242822 ile okunur, komut: "22 XX", yanit: "62 XX <data>"
    QList<uint8_t> pids = {
        0x01, // Actual Gear (1B)
        0x02, // Selected Gear (1B)
        0x03, // Max Gear (1B)
        0x04, // Shift Selector Position (1B)
        0x10, // Turbine RPM (2B)
        0x11, // Input RPM N2 (2B)
        0x12, // Input RPM N3 (2B)
        0x13, // Output RPM (2B)
        0x14, // Transmission Temp (1B, +40 offset)
        0x15, // TCC Pressure (1B, *0.1)
        0x16, // Solenoid Supply (1B, *0.1V)
        0x17, // TCC Clutch State (1B)
        0x18, // Actual TCC Slip (2B, signed)
        0x19, // Desired TCC Slip (2B, signed)
        0x1A, // Act 1245 Solenoid (1B, *0.39%)
        0x1B, // Set 1245 Solenoid (1B, *0.39%)
        0x1C, // Act 2-3 Solenoid (1B, *0.39%)
        0x1D, // Set 2-3 Solenoid (1B, *0.39%)
        0x1E, // Act 3-4 Solenoid (1B, *0.39%)
        0x1F, // Set 3-4 Solenoid (1B, *0.39%)
        0x20, // Vehicle Speed (2B)
        0x21, // Front Vehicle Speed (2B)
        0x22, // Rear Vehicle Speed (2B)
        0x23, // Shift PSI (2B)
        0x24, // Modulation PSI (2B)
        0x25, // Park Lockout Solenoid (1B)
        0x26, // Park/Neutral Switch (1B)
        0x27, // Brake Light Switch (1B)
        0x28, // Primary Brake Switch (1B)
        0x29, // Secondary Brake Switch (1B)
        0x2A, // Kickdown Switch (1B)
        0x2B, // Fuel QTY Torque (1B, *0.39%)
        0x2C, // Swirl Solenoid (1B)
        0x2D, // Wastegate Solenoid (1B, *0.39%)
        0x30, // Calculated Gear (1B)
    };

    // Oncelikle ATSH242822 ayarla
    m_elm->sendCommand("ATSH242822", [this, tcm, step, doNext, pids, cb](const QString&) {

    *doNext = [this, tcm, step, doNext, pids, cb]() {
        if (*step >= pids.size()) {
            // Read battery voltage via ATRV
            m_elm->sendCommand("ATRV", [this, tcm, cb](const QString &rv) {
                QString v = rv.trimmed().remove('V').remove('v');
                bool ok = false;
                double volts = v.toDouble(&ok);
                if (ok && volts > 0) tcm->batteryVoltage = volts;
                fillTCMCompat(*tcm);
                m_lastTCM = *tcm;
                emit tcmStatusUpdated(*tcm);
                if (cb) cb(*tcm);
            });
            return;
        }
        uint8_t pid = pids[*step];
        QString cmd = QString("22 %1").arg(pid, 2, 16, QChar('0')).toUpper();

        m_elm->sendCommand(cmd, [this, tcm, step, doNext, pid](const QString &resp) {
            // ELM327 J1850 yanit (ATE1 + ATH1 acik):
            // Satir 1 (echo): "22 01" (komut echo'su)
            // Satir 2 (yanit): "28 10 62 01 03" (header + data)
            // Satir 3: ">" (prompt - zaten kesilmis)
            //
            // Parse: echo satirini atla, yanit satirindan "62" bul
            QByteArray raw;
            QStringList lines = resp.split('\r', Qt::SkipEmptyParts);

            // Son gecerli hex satirini bul (echo'dan sonraki = gercek yanit)
            for (int li = lines.size() - 1; li >= 0; li--) {
                QString cleaned = lines[li].trimmed().remove(' ');
                if (cleaned.isEmpty()) continue;
                // Hata yanitlari
                if (cleaned.contains("NODATA") || cleaned.contains("ERROR") ||
                    cleaned.contains("UNABLE") || cleaned.contains("?") ||
                    cleaned.contains("BUSBUSY") || cleaned.contains("STOPPED")) continue;
                // Echo satirini atla (komutumuzla baslar: "22XX")
                QString echoCheck = QString("22%1").arg(pid, 2, 16, QChar('0')).toUpper();
                if (cleaned.startsWith(echoCheck) && cleaned.length() <= echoCheck.length() + 2) continue;

                // "62" byte'inin pozisyonunu bul
                int pos62 = cleaned.indexOf("62", 0, Qt::CaseInsensitive);
                if (pos62 < 0 || pos62 % 2 != 0) continue;

                // Dogrudan PID byte'ini da kontrol et: "62 XX" olmali
                QString pidCheck = QString("62%1").arg(pid, 2, 16, QChar('0')).toUpper();
                int posPid = cleaned.indexOf(pidCheck, 0, Qt::CaseInsensitive);
                if (posPid < 0 || posPid % 2 != 0) continue;

                // posPid'den itibaren parse et
                QString dataHex = cleaned.mid(posPid);
                for (int i = 0; i + 1 < dataHex.length(); i += 2)
                    raw.append(static_cast<char>(dataHex.mid(i, 2).toUInt(nullptr, 16)));
                break;
            }

            // raw[0]=0x62, raw[1]=PID, raw[2...]=data
            if (raw.size() >= 3 && static_cast<uint8_t>(raw[0]) == 0x62
                && static_cast<uint8_t>(raw[1]) == pid) {
                uint8_t valByte = static_cast<uint8_t>(raw[2]);
                uint16_t val16 = valByte;
                if (raw.size() >= 4)
                    val16 = (static_cast<uint8_t>(raw[2]) << 8) | static_cast<uint8_t>(raw[3]);
                int16_t sval16 = static_cast<int16_t>(val16);

                switch (pid) {
                case 0x01: tcm->actualGear = valByte; break;
                case 0x02: tcm->selectedGear = valByte; break;
                case 0x03: tcm->maxGear = valByte; break;
                case 0x04: /* selector position */ break;
                case 0x10: tcm->turbineRPM = val16; break;
                case 0x11: /* input RPM N2 */ break;
                case 0x12: /* input RPM N3 */ break;
                case 0x13: tcm->outputRPM = val16; break;
                case 0x14: tcm->transTemp = valByte - 40; break;
                case 0x15: tcm->tccPressure = valByte * 0.1; break;
                case 0x16: tcm->solenoidSupply = valByte * 0.1; break;
                case 0x17: /* TCC clutch state */ break;
                case 0x18: tcm->actualTCCslip = sval16; break;
                case 0x19: tcm->desTCCslip = sval16; break;
                case 0x1A: /* act 1245 sol */ break;
                case 0x1B: /* set 1245 sol */ break;
                case 0x1C: /* act 2-3 sol */ break;
                case 0x1D: /* set 2-3 sol */ break;
                case 0x1E: /* act 3-4 sol */ break;
                case 0x1F: /* set 3-4 sol */ break;
                case 0x20: tcm->vehicleSpeed = val16; break;
                case 0x21: /* front speed */ break;
                case 0x22: /* rear speed */ break;
                case 0x23: tcm->linePressure = val16 * 0.1; break;
                case 0x24: /* modulation PSI */ break;
                case 0x25: /* park lockout */ break;
                case 0x26: /* park/neutral */ break;
                case 0x27: /* brake light */ break;
                case 0x28: /* primary brake */ break;
                case 0x29: /* secondary brake */ break;
                case 0x2A: /* kickdown */ break;
                case 0x2B: /* fuel qty torque */ break;
                case 0x2C: /* swirl solenoid */ break;
                case 0x2D: /* wastegate sol */ break;
                case 0x30: /* calculated gear */ break;
                }
            }
            (*step)++;
            QTimer::singleShot(340, *doNext);
        });
    };
    (*doNext)();

    }); // ATSH242822 callback end
}

// readSingleParam - J1850 VPW tek PID oku
void WJDiagnostics::readSingleParam(uint8_t localID, std::function<void(double)> cb)
{
    QString cmd = QString("22 %1").arg(localID, 2, 16, QChar('0')).toUpper();
    m_elm->sendCommand(cmd, [this, localID, cb](const QString &resp) {
        double val = 0;
        QByteArray raw;

        // ATE1 + ATH1: echo satiri + header'li yanit
        QStringList lines = resp.split('\r', Qt::SkipEmptyParts);
        for (int li = lines.size() - 1; li >= 0; li--) {
            QString cleaned = lines[li].trimmed().remove(' ');
            if (cleaned.isEmpty() || cleaned.contains("NODATA") ||
                cleaned.contains("ERROR") || cleaned.contains("BUSBUSY")) continue;

            // Echo satirini atla
            QString echoCheck = QString("22%1").arg(localID, 2, 16, QChar('0')).toUpper();
            if (cleaned.startsWith(echoCheck) && cleaned.length() <= echoCheck.length() + 2) continue;

            // "62 XX" pattern bul
            QString pidCheck = QString("62%1").arg(localID, 2, 16, QChar('0')).toUpper();
            int posPid = cleaned.indexOf(pidCheck, 0, Qt::CaseInsensitive);
            if (posPid < 0 || posPid % 2 != 0) continue;

            QString dataHex = cleaned.mid(posPid);
            for (int i = 0; i + 1 < dataHex.length(); i += 2)
                raw.append(static_cast<char>(dataHex.mid(i, 2).toUInt(nullptr, 16)));
            break;
        }

        if (raw.size() >= 3 && static_cast<uint8_t>(raw[0]) == 0x62) {
            uint8_t valByte = static_cast<uint8_t>(raw[2]);
            uint16_t raw16 = valByte;
            if (raw.size() >= 4)
                raw16 = (static_cast<uint8_t>(raw[2]) << 8) | static_cast<uint8_t>(raw[3]);

            bool found = false;
            for (const auto &p : m_liveParams) {
                if (p.localID == localID) {
                    if (p.byteLen == 1)
                        val = valByte * p.factor + p.offset;
                    else
                        val = raw16 * p.factor + p.offset;
                    found = true;
                    break;
                }
            }
            if (!found) val = raw16;
        }
        if (cb) cb(val);
    });
}

// fillTCMCompat - TCM status'a compat field'lari doldur
void WJDiagnostics::fillTCMCompat(TCMStatus &tcm)
{
    tcm.solenoidVoltage = tcm.solenoidSupply;
    if (tcm.linePressure == 0) tcm.linePressure = tcm.tccPressure;
    if (tcm.actualGear >= 0 && tcm.actualGear <= 7)
        tcm.currentGear = static_cast<Gear>(tcm.actualGear);
    else
        tcm.currentGear = Gear::Unknown;
    // limpMode: readAllLiveData 0x23 ile set edilmisse dokunma
    // aksi halde maxGear/transTemp'ten hesapla
    if (tcm.maxGear > 0 && tcm.maxGear <= 2 && tcm.transTemp > 130)
        tcm.limpMode = true;
}

// readTCMInfo - J1850 VPW ile TCM modul bilgisi oku (ATSH2428A0)
void WJDiagnostics::readTCMInfo(std::function<void(const QMap<QString,QString>&)> cb)
{
    auto r = std::make_shared<QMap<QString,QString>>();
    (*r)["Module"] = "NAG1 722.6 TCM";
    (*r)["Bus"] = "J1850 VPW";

    // ATSH2428A0 kullaniyor (SID 0xA0 = ReadIdentification)
    m_elm->sendCommand("ATSH2428A0", [this, r, cb](const QString&) {
        // A0 01 = Part Number
        m_elm->sendCommand("A0 01", [this, r, cb](const QString &resp1) {
            if (!resp1.contains("ERROR") && !resp1.contains("NO DATA"))
                (*r)["PartNumber"] = resp1.trimmed();
            else
                (*r)["PartNumber"] = "N/A";

            // A0 02 = Software Version
            m_elm->sendCommand("A0 02", [this, r, cb](const QString &resp2) {
                if (!resp2.contains("ERROR") && !resp2.contains("NO DATA"))
                    (*r)["SoftwareVersion"] = resp2.trimmed();
                else
                    (*r)["SoftwareVersion"] = "N/A";

                // A0 03 = Hardware Version
                m_elm->sendCommand("A0 03", [this, r, cb](const QString &resp3) {
                    if (!resp3.contains("ERROR") && !resp3.contains("NO DATA"))
                        (*r)["HardwareVersion"] = resp3.trimmed();
                    else
                        (*r)["HardwareVersion"] = "N/A";

                    // Header'i geri cevir
                    m_elm->sendCommand("ATSH242822", [r, cb](const QString&) {
                        if (cb) cb(*r);
                    });
                });
            });
        });
    });
}

// initLiveDataParams - TCM J1850 VPW PID listesi (referans)
void WJDiagnostics::initLiveDataParams()
{
    // referansndan: J1850 VPW, ATSH242822, "22 XX" komutu
    m_liveParams = {
        {0x01, "Actual Gear",                 "",      0,   7,   1.0,    0, 1, false},
        {0x02, "Selected Gear",               "",      0,   7,   1.0,    0, 1, false},
        {0x03, "Max Gear",                    "",      0,   7,   1.0,    0, 1, false},
        {0x04, "Shift Selector Position",     "",      0,  15,   1.0,    0, 1, false},
        {0x10, "Turbine RPM",                 "rpm",   0, 8000, 1.0,    0, 2, false},
        {0x11, "Input RPM (N2)",              "rpm",   0, 8000, 1.0,    0, 2, false},
        {0x12, "Input RPM (N3)",              "rpm",   0, 8000, 1.0,    0, 2, false},
        {0x13, "Output RPM",                  "rpm",   0, 8000, 1.0,    0, 2, false},
        {0x14, "Transmission Temp",           "C",   -40, 200,  1.0,  -40, 1, false},
        {0x15, "TCC Pressure",                "PSI",   0, 255,  0.1,    0, 1, false},
        {0x16, "Solenoid Supply",             "V",     0,  20,  0.1,    0, 1, false},
        {0x17, "TCC Clutch State",            "",      0,   5,  1.0,    0, 1, false},
        {0x18, "Actual TCC Slip",             "rpm",-2000,2000, 1.0,    0, 2, true},
        {0x19, "Desired TCC Slip",            "rpm",-2000,2000, 1.0,    0, 2, true},
        {0x1A, "Act 1245 Solenoid",           "%",     0, 100,  0.39,   0, 1, false},
        {0x1B, "Set 1245 Solenoid",           "%",     0, 100,  0.39,   0, 1, false},
        {0x1C, "Act 2-3 Solenoid",            "%",     0, 100,  0.39,   0, 1, false},
        {0x1D, "Set 2-3 Solenoid",            "%",     0, 100,  0.39,   0, 1, false},
        {0x1E, "Act 3-4 Solenoid",            "%",     0, 100,  0.39,   0, 1, false},
        {0x1F, "Set 3-4 Solenoid",            "%",     0, 100,  0.39,   0, 1, false},
        {0x20, "Vehicle Speed",               "km/h",  0, 300,  1.0,    0, 2, false},
        {0x21, "Front Vehicle Speed",         "km/h",  0, 300,  1.0,    0, 2, false},
        {0x22, "Rear Vehicle Speed",          "km/h",  0, 300,  1.0,    0, 2, false},
        {0x23, "Shift PSI",                   "PSI",   0, 500,  0.1,    0, 2, false},
        {0x24, "Modulation PSI",              "PSI",   0, 500,  0.1,    0, 2, false},
        {0x25, "Park Lockout Solenoid",       "",      0,   1,  1.0,    0, 1, false},
        {0x26, "Park/Neutral Switch",         "",      0,   1,  1.0,    0, 1, false},
        {0x27, "Brake Light Switch",          "",      0,   1,  1.0,    0, 1, false},
        {0x28, "Primary Brake Switch",        "",      0,   1,  1.0,    0, 1, false},
        {0x29, "Secondary Brake Switch",      "",      0,   1,  1.0,    0, 1, false},
        {0x2A, "Kickdown Switch",             "",      0,   1,  1.0,    0, 1, false},
        {0x2B, "Fuel QTY Torque",             "%",     0, 100,  0.39,   0, 1, false},
        {0x2C, "Swirl Solenoid",              "",      0,   1,  1.0,    0, 1, false},
        {0x2D, "Wastegate Solenoid",          "%",     0, 100,  0.39,   0, 1, false},
        {0x30, "Calculated Gear",             "",      0,   7,  1.0,    0, 1, false},
    };
    emit logMessage(QString("Live data: %1 parameters loaded (J1850 VPW)").arg(m_liveParams.size()));
}

// ioDefinitions - I/O kontrol tanimlari (NAG1 selenoidler)
QList<WJDiagnostics::IODefinition> WJDiagnostics::ioDefinitions() const
{
    return {
        {0x10, "Shift Solenoid 1-2/4-5", "Shift 1-2 / 4-5 solenoid"},
        {0x11, "Shift Solenoid 2-3",     "Shift 2-3 solenoid"},
        {0x12, "Shift Solenoid 3-4",     "Shift 3-4 solenoid"},
        {0x13, "TCC PWM Solenoid",       "TCC lockup"},
        {0x14, "Modulating Pressure",    "Modulating pressure sol."},
        {0x15, "Shift Pressure",         "Shift pressure solenoid"},
        {0x16, "Park Lockout Solenoid",  "Park lockout sol."},
        {0x17, "Starter Interlock",      "Starter interlock"},
        {0x18, "Reverse Light",          "Reverse light relay"},
    };
}

// readIOStates - I/O durumlarini oku (SID 0x30 / 0x22)
void WJDiagnostics::readIOStates(std::function<void(const QList<IOState>&)> cb)
{
    auto defs = ioDefinitions();
    auto states = std::make_shared<QList<IOState>>();
    auto idx = std::make_shared<int>(0);
    auto readNext = std::make_shared<std::function<void()>>();

    *readNext = [this, defs, states, idx, readNext, cb]() {
        if (*idx >= defs.size()) {
            if (cb) cb(*states);
            return;
        }
        uint8_t lid = defs[*idx].localID;
        m_kwp->readLocalData(lid, [states, idx, readNext, lid](const QByteArray &data) {
            IOState st;
            st.localID = lid;
            st.rawValue = (data.size() > 2) ? static_cast<uint8_t>(data[2]) : 0;
            st.isActive = (st.rawValue != 0);
            states->append(st);
            (*idx)++;
            QTimer::singleShot(340, *readNext);
        });
    };

    switchToModule(Module::TCM, [readNext](bool ok) {
        if (ok) (*readNext)();
    });
}
