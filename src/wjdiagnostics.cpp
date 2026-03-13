#include "wjdiagnostics.h"
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
        // Init: ATZ -> ATWM -> ATSH -> ATSP5 -> ATFI -> 81 -> 27
        {Module::MotorECU, "Engine ECU (Bosch EDC15C2 OM612)", "Engine",
         BusType::KLine, "ATSH8115F1", "ATWM8115F13E", "ATSP5", ""},
        {Module::KLineTCM, "NAG1 722.6 Transmission (K-Line)", "KL-TCM",
         BusType::KLine, "ATSH8120F1", "ATWM8120F13E", "ATSP5", ""},

        // J1850 VPW modules - headers + ATRA filters
        {Module::ABS, "NAG1 722.6 Transmission (EGS52)", "TCM",
         BusType::J1850, "ATSH242822", "", "ATSP2", "ATRA28"},
        {Module::EVIC, "Overhead Console (EVIC)", "EVIC",
         BusType::J1850, "ATSH242A22", "", "ATSP2", "ATRA2A"},
        {Module::BodyComputer, "Body Computer / ESP Braking", "ABS",
         BusType::J1850, "ATSH244022", "", "ATSP2", "ATRA40"},
        {Module::Airbag, "Airbag (ORC/AOSIM)", "Airbag",
         BusType::J1850, "ATSH246022", "", "ATSP2", "ATRA60"},
        {Module::SKIM, "SKIM Immobilizer", "SKIM",
         BusType::J1850, "ATSH246222", "", "ATSP2", "ATRA62"},
        {Module::ATC, "Climate Control (HVAC)", "HVAC",
         BusType::J1850, "ATSH246822", "", "ATSP2", "ATRA68"},
        {Module::BodyComputer, "Body Computer (BCM)", "BCM",
         BusType::J1850, "ATSH248022", "", "ATSP2", "ATRA80"},
        {Module::Radio, "Radio / Audio", "Radio",
         BusType::J1850, "ATSH248722", "", "ATSP2", "ATRA87"},
        {Module::Cluster, "Instrument Cluster", "Cluster",
         BusType::J1850, "ATSH249022", "", "ATSP2", "ATRA90"},
        {Module::MemSeat, "Memory Seat / Mirror", "Seat",
         BusType::J1850, "ATSH249822", "", "ATSP2", "ATRA98"},
        {Module::DriverDoor, "Door Module (0xA0)", "Door",
         BusType::J1850, "ATSH24A022", "", "ATSP2", "ATRAA0"},
        {Module::PassengerDoor, "Liftgate / HandsFree", "HFM",
         BusType::J1850, "ATSH24A122", "", "ATSP2", "ATRAA1"},
        {Module::ESP_Module, "ESP / Traction Control", "ESP",
         BusType::J1850, "ATSH245822", "", "ATSP2", "ATRA58"},
        {Module::Cluster, "Instrument Cluster", "Cluster",
         BusType::J1850, "ATSH246122", "", "ATSP2", "ATRA61"},
        {Module::RainSensor, "Siren / Security", "Siren",
         BusType::J1850, "ATSH24A722", "", "ATSP2", "ATRAA7"},
        {Module::ParkAssist, "VTSS / Park Assist", "VTSS",
         BusType::J1850, "ATSH24C022", "", "ATSP2", "ATRAC0"},
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

                    // ATFI - Fast Init (bus init) with 1 retry
                    auto atfiRetry = std::make_shared<int>(0);
                    auto doATFI = std::make_shared<std::function<void()>>();
                    *doATFI = [this, info, done, targetMod, atfiRetry, doATFI](/*capture*/) {
                    m_elm->sendCommand("ATFI", [this, info, done, targetMod, atfiRetry, doATFI](const QString &fi) {
                        // Check ERROR first - "BUS INIT: ERROR" contains "BUS INIT" too!
                        if (fi.contains("ERROR") || fi.contains("TIMEOUT") || fi.contains("?")) {
                            if (*atfiRetry < 1) {
                                // Retry once: ATZ + reinit + ATFI
                                (*atfiRetry)++;
                                emit logMessage("ATFI failed, retry #" + QString::number(*atfiRetry));
                                m_elm->sendCommand("ATZ", [this, info, doATFI](const QString&) {
                                    QTimer::singleShot(500, this, [this, info, doATFI]() {
                                        m_elm->sendCommand("ATE1", [this, info, doATFI](const QString&) {
                                        m_elm->sendCommand("ATH1", [this, info, doATFI](const QString&) {
                                        m_elm->sendCommand(info.atwmWakeup, [this, info, doATFI](const QString&) {
                                        m_elm->sendCommand(info.atshHeader, [this, info, doATFI](const QString&) {
                                        m_elm->sendCommand("ATSP5", [this, doATFI](const QString&) {
                                            (*doATFI)();
                                        });
                                        });
                                        });
                                        });
                                        });
                                    });
                                });
                                return;
                            }
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

                                // SecurityAccess: 27 01 -> seed, compute EGS52 key, 27 02 -> key
                                m_elm->sendCommand("27 01", [this, done, targetMod](const QString &seed) {
                                    emit logMessage("Security seed: " + seed);
                                    if (seed.contains("67 01")) {
                                        QStringList parts = seed.trimmed().split(' ');
                                        QString keyCmd = "27 02 CC 21"; // TCM default
                                        if (parts.size() >= 7) {
                                            bool ok0, ok1, ok2;
                                            uint8_t s0 = parts[5].toUInt(&ok0, 16);
                                            uint8_t s1 = parts[6].toUInt(&ok1, 16);
                                            uint8_t s2 = (parts.size() >= 8) ? parts[7].toUInt(&ok2, 16) : 0;

                                            bool isTCM = (targetMod == Module::KLineTCM);

                                            if (isTCM && ok0 && ok1) {
                                                // EGS52: swap, XOR 0x5AA5, multiply 0x5AA5 (2-byte key)
                                                uint16_t s16 = (s0 << 8) | s1;
                                                uint16_t r = ((s16 << 8) & 0xFFFF) | (s16 >> 8);
                                                r ^= 0x5AA5;
                                                r = static_cast<uint16_t>(static_cast<uint32_t>(r) * 0x5AA5 & 0xFFFF);
                                                keyCmd = QString("27 02 %1 %2")
                                                    .arg(r >> 8, 2, 16, QChar('0'))
                                                    .arg(r & 0xFF, 2, 16, QChar('0')).toUpper();
                                                emit logMessage(QString("TCM EGS52 key: seed=%1 -> key=%2")
                                                    .arg(s16, 4, 16, QChar('0')).arg(r, 4, 16, QChar('0')));
                                            } else if (!isTCM && ok0 && ok1) {
                                                // EDC15C2 OM612: Level 01, 2-byte key
                                                // ArvutaKoodi — lookup-table seed-key for Chrysler EDC15C2
                                                // Estonca: "Compute Code" — lookup table based, NOT ProcessKey5
                                                static const uint8_t T1[] = {0xC0,0xD0,0xE0,0xF0,0x00,0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80,0x90,0xA0,0xB0};
                                                static const uint8_t T2[] = {0x02,0x03,0x00,0x01,0x06,0x07,0x04,0x05,0x0A,0x0B,0x08,0x09,0x0E,0x0F,0x0C,0x0D};
                                                static const uint8_t T3[] = {0x90,0x80,0xF0,0xE0,0xD0,0xC0,0x30,0x20,0x10,0x00,0x70,0x60,0x50,0x40,0xB0,0xA0};
                                                static const uint8_t T4[] = {0x0D,0x0C,0x0F,0x0E,0x09,0x08,0x0B,0x0A,0x05,0x04,0x07,0x06,0x01,0x00,0x03,0x02};
                                                uint8_t v1 = (s1 + 0x0B) & 0xFF;
                                                uint8_t keyLo = T1[(v1 >> 4) & 0xF] | T2[v1 & 0xF];
                                                uint8_t cond = (s1 > 0x34) ? 1 : 0;
                                                uint8_t v2 = (s0 + cond + 1) & 0xFF;
                                                uint8_t keyHi = T3[(v2 >> 4) & 0xF] | T4[v2 & 0xF];
                                                keyCmd = QString("27 02 %1 %2")
                                                    .arg(keyHi, 2, 16, QChar('0'))
                                                    .arg(keyLo, 2, 16, QChar('0')).toUpper();
                                                emit logMessage(QString("ECU ArvutaKoodi: seed=%1%2 -> key=%3")
                                                    .arg(s0,2,16,QChar('0')).arg(s1,2,16,QChar('0'))
                                                    .arg(keyCmd.mid(6)));
                                            }
                                        }
                                        m_elm->sendCommand(keyCmd, [this, done, targetMod](const QString &key) {
                                            emit logMessage("Security key: " + key);
                                            bool unlocked = key.contains("67 02", Qt::CaseInsensitive);
                                            if (unlocked) {
                                                emit logMessage("Security UNLOCKED!");
                                                if (targetMod == Module::MotorECU)
                                                    m_ecuSecurityUnlocked = true;
                                            }
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
                    }; // end doATFI lambda
                    (*doATFI)(); // initial ATFI call
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
                        auto finalize = [this, info, done, targetMod]() {
                            m_activeModule = targetMod;
                            emit logMessage(QString("Active: %1 | %2").arg(info.shortName, info.atshHeader));

                            // J1850 DiagSession for modules that need it
                            if (targetMod == Module::ABS) {
                                m_elm->sendCommand("ATSH242810", [this, done](const QString&) {
                                    QTimer::singleShot(100, this, [this, done]() {
                                    m_elm->sendCommand("02 00 00", [this, done](const QString &resp) {
                                        if (resp.contains("50")) {
                                            emit logMessage("TCM DiagSession OK (50)");
                                        } else {
                                            emit logMessage("TCM DiagSession: " + resp);
                                        }
                                        QTimer::singleShot(100, this, [this, done]() {
                                        m_elm->sendCommand("ATSH242822", [this, done](const QString&) {
                                            if (done) done(true);
                                        });
                                        });
                                    });
                                    });
                                });
                            } else if (targetMod == Module::BodyComputer || targetMod == Module::DriverDoor ||
                                       targetMod == Module::Cluster || targetMod == Module::ParkAssist ||
                                       targetMod == Module::MemSeat) {
                                uint8_t modAddr = static_cast<uint8_t>(targetMod);
                                QString sessHdr = QString("ATSH24%1%2")
                                    .arg(modAddr, 2, 16, QChar('0')).arg("11").toUpper();
                                QString readHdr = info.atshHeader;
                                m_elm->sendCommand(sessHdr, [this, readHdr, done, targetMod](const QString&) {
                                    QTimer::singleShot(100, this, [this, readHdr, done, targetMod]() {
                                    m_elm->sendCommand("01 01 00", [this, readHdr, done, targetMod](const QString &resp) {
                                        bool ok = resp.contains("41") || resp.contains("50");
                                        emit logMessage(QString("%1 DiagSession: %2")
                                            .arg(moduleName(targetMod), ok ? "OK" : resp.trimmed()));
                                        QTimer::singleShot(100, this, [this, readHdr, done]() {
                                        m_elm->sendCommand(readHdr, [this, done](const QString&) {
                                            if (done) done(true);
                                        });
                                        });
                                    });
                                    });
                                });
                            } else {
                                if (done) done(true);
                            }
                        };
                        if (!info.atraFilter.isEmpty()) {
                            m_elm->sendCommand(info.atraFilter, [finalize](const QString&) {
                                finalize();
                            });
                        } else {
                            finalize();
                        }
                    });
                });
                });
                });
                });
            }, 7500);
        }
    } else {
        // Same bus
        if (newBus == BusType::KLine) {
            // K-Line: ALWAYS do full reinit when switching between K-Line modules
            // because each module needs different ATSH/ATWM/ATFI sequence
            // Even if m_activeModule matches, the bus may have timed out
            if (targetMod == m_activeModule) {
                // Same module - verify session is alive with a quick test
                m_elm->sendCommand("3E", [this, done, targetMod](const QString &resp) {
                    // TesterPresent (3E) - if we get 7E back, session is alive
                    if (resp.contains("7E") || resp.contains("7e")) {
                        // Session alive, skip reinit
                        if (done) done(true);
                    } else {
                        // Session dead or wrong module - full reinit
                        emit logMessage("K-Line session expired, reinitializing...");
                        m_activeBus = BusType::None;
                        switchToModule(targetMod, done);
                    }
                });
                return;
            }
            // Different K-Line module: full reinit required
            emit logMessage("K-Line: full reinit required");
            m_activeBus = BusType::None;
            switchToModule(mod, done);
            return;
        }
        // J1850 same bus - just change header
        m_elm->sendCommand(info.atshHeader, [this, info, done, targetMod](const QString&) {
            auto finalize = [this, info, done, targetMod]() {
                m_activeModule = targetMod;
                emit logMessage(QString("Active: %1 | %2").arg(info.shortName, info.atshHeader));

                // J1850 modules that need DiagSession before data read
                // TCM(0x28): ECUReset via mode 0x10
                // OHC(0x2A): init via mode 0x11
                // BCM(0x80): init via mode 0x11 (needed for relay control prep)
                // Door(0xA0): init via mode 0x11 (needed for IOControl)
                // Cluster(0x90): init via mode 0x11 (needed for gauge test)
                // VTSS(0xC0): init via mode 0x11 (needed for security)
                if (targetMod == Module::ABS) {
                    m_elm->sendCommand("ATSH242810", [this, done](const QString&) {
                        QTimer::singleShot(100, this, [this, done]() {
                        m_elm->sendCommand("02 00 00", [this, done](const QString &resp) {
                            if (resp.contains("50")) {
                                emit logMessage("TCM DiagSession OK (50)");
                            } else {
                                emit logMessage("TCM DiagSession: " + resp);
                            }
                            // Switch back to data read header
                            QTimer::singleShot(100, this, [this, done]() {
                            m_elm->sendCommand("ATSH242822", [this, done](const QString&) {
                                if (done) done(true);
                            });
                            });
                        });
                        });
                    });
                } else if (targetMod == Module::BodyComputer || targetMod == Module::DriverDoor ||
                           targetMod == Module::Cluster || targetMod == Module::ParkAssist ||
                           targetMod == Module::MemSeat) {
                    // Generic J1850 DiagSession: ATSH24xx11 -> 01 01 00 -> back to read header
                    uint8_t modAddr = static_cast<uint8_t>(targetMod);
                    QString sessHdr = QString("ATSH24%1%2")
                        .arg(modAddr, 2, 16, QChar('0')).arg("11").toUpper();
                    QString readHdr = info.atshHeader;  // back to mode 0x22
                    m_elm->sendCommand(sessHdr, [this, readHdr, done, targetMod](const QString&) {
                        QTimer::singleShot(100, this, [this, readHdr, done, targetMod]() {
                        m_elm->sendCommand("01 01 00", [this, readHdr, done, targetMod](const QString &resp) {
                            bool ok = resp.contains("41") || resp.contains("50");
                            emit logMessage(QString("%1 DiagSession: %2")
                                .arg(moduleName(targetMod), ok ? "OK" : resp.trimmed()));
                            QTimer::singleShot(100, this, [this, readHdr, done]() {
                            m_elm->sendCommand(readHdr, [this, done](const QString&) {
                                if (done) done(true);
                            });
                            });
                        });
                        });
                    });
                } else {
                    if (done) done(true);
                }
            };
            if (!info.atraFilter.isEmpty()) {
                m_elm->sendCommand(info.atraFilter, [finalize](const QString&) {
                    finalize();
                });
            } else {
                finalize();
            }
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
            // J1850 DTC read (verified):
            // ABS: ATSH244022 → "24 00 00" (DTC as special PID via ReadDataByID)
            // Airbag: ATSH246022 → "28 37 00" (DTC PID 0x37)
            uint8_t modAddr = static_cast<uint8_t>(mod);
            QString dtcCmd;
            if (modAddr == 0x40) {
                dtcCmd = "24 00 00";   // ABS DTC read
            } else if (modAddr == 0x60) {
                dtcCmd = "28 37 00";   // Airbag DTC read
            } else {
                emit logMessage(QString("%1: J1850 DTC read not supported")
                    .arg(moduleInfo(mod).shortName));
                if (cb) cb({});
                return;
            }

            m_elm->sendCommand(dtcCmd, [this, mod, modAddr, cb](const QString &resp) {
                QList<DTCEntry> dtcs;
                if (!resp.contains("NO DATA") && !resp.contains("ERROR") && !resp.contains("7F")) {
                    dtcs = decodeJ1850DTCs(resp, mod);
                    emit logMessage(QString("%1 DTC: %2 codes found")
                        .arg(moduleInfo(mod).shortName).arg(dtcs.size()));
                }
                emit dtcListReady(mod, dtcs);
                if (cb) cb(dtcs);
            });
        }
    });
}

void WJDiagnostics::clearDTCs(Module mod, std::function<void(bool)> cb)
{
    if (!m_elm || !m_elm->isConnected()) {
        emit logMessage("clearDTCs: not connected");
        if (cb) cb(false);
        return;
    }
    switchToModule(mod, [this, mod, cb](bool ok) {
        if (!ok) { if (cb) cb(false); return; }

        if (moduleInfo(mod).bus == BusType::KLine) {
            m_kwp->clearAllDTCs(cb);
        } else {
            // J1850 DTC clear (verified):
            // ABS: ATSH244011 → "01 01 00" (ECUReset = clears DTCs)
            // Airbag: ATSH246011 → "0D" (ECUReset with param)
            uint8_t modAddr = static_cast<uint8_t>(mod);
            QString resetHeader = QString("ATSH24%1%2")
                .arg(modAddr, 2, 16, QChar('0'))
                .arg("11").toUpper();
            QString resetCmd;
            if (modAddr == 0x40) {
                resetCmd = "01 01 00";  // ABS: hardReset
            } else if (modAddr == 0x60) {
                resetCmd = "0D";        // Airbag: reset param 0x0D
            } else {
                emit logMessage(QString("%1: J1850 DTC clear not supported")
                    .arg(moduleInfo(mod).shortName));
                if (cb) cb(false);
                return;
            }

            m_elm->sendCommand(resetHeader, [this, mod, resetCmd, cb](const QString&) {
                m_elm->sendCommand(resetCmd, [this, mod, cb](const QString &resp) {
                    // ECUReset positive response: 0x51
                    bool ok = resp.contains("51") && !resp.contains("7F");
                    emit logMessage(QString("%1 DTC clear (ECUReset): %2")
                        .arg(moduleInfo(mod).shortName, ok ? "OK" : resp.trimmed()));
                    if (cb) cb(ok);
                });
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

    // Read blocks: 0x12, 0x28, 0x20, 0x22 (always)
    // + 0x62, 0xB0, 0xB1, 0xB2 (if ECU security unlocked)
    static const uint8_t baseIds[] = {0x12, 0x28, 0x20, 0x22};
    static const uint8_t secIds[] = {0x62, 0xB0, 0xB1, 0xB2};
    auto ids = std::make_shared<QVector<uint8_t>>();
    for (auto id : baseIds) ids->append(id);
    if (m_ecuSecurityUnlocked) {
        for (auto id : secIds) ids->append(id);
    }

    *doNext = [this, ecu, step, doNext, cb, ids]() {
        if (*step >= ids->size()) {
            // Read battery voltage
            m_elm->sendCommand("ATRV", [this, ecu, cb](const QString &rv) {
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
        m_kwp->readLocalData(ids->at(*step), [this, ecu, step, doNext, ids](const QByteArray &data) {
            if (!data.isEmpty()) {
                parseECUBlock(ids->at(*step), data, *ecu);
            }
            (*step)++;
            (*doNext)(); // No delay between reads
        });
    };

    switchToModule(Module::MotorECU, [doNext](bool ok) {
        if (ok) (*doNext)();
    });
}

// --- TCM Live Data (J1850 VPW 0x28) ---

void WJDiagnostics::readTCMLiveData(std::function<void(const TCMStatus&)> cb)
{
    // K-Line TCM (0x20) - Read block 0x30 for live data
    auto tcm = std::make_shared<TCMStatus>();

    switchToModule(Module::KLineTCM, [this, tcm, cb](bool ok) {
        if (!ok) { if (cb) cb(*tcm); return; }

        m_elm->sendCommand("21 30", [this, tcm, cb](const QString &resp) {
            if (!resp.contains("NO DATA") && !resp.contains("7F") && !resp.contains("ERROR")) {
                QByteArray raw;
                QString cleaned = resp;
                cleaned.remove(' ').remove('\r').remove('\n');
                int pos = cleaned.indexOf("6130", 0, Qt::CaseInsensitive);
                if (pos >= 0) {
                    QString dataHex = cleaned.mid(pos);
                    if (dataHex.length() > 4) dataHex.chop(2);
                    for (int i = 0; i + 1 < dataHex.length(); i += 2) {
                        bool ok2;
                        uint8_t b = dataHex.mid(i, 2).toUInt(&ok2, 16);
                        if (ok2) raw.append(static_cast<char>(b));
                    }
                }
                if (raw.size() >= 4) parseTCMBlock30(raw, *tcm);
            }

            // Read battery voltage via ATRV
            m_elm->sendCommand("ATRV", [this, tcm, cb](const QString &rv) {
                QString v = rv.trimmed().remove('V').remove('v');
                bool ok = false;
                double volts = v.toDouble(&ok);
                if (ok && volts > 0) {
                    tcm->batteryVoltage = volts;
                    tcm->solenoidSupply = volts;
                }
                m_lastTCM = *tcm;
                emit tcmStatusUpdated(*tcm);
                if (cb) cb(*tcm);
            });
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
                {"20 01 00", "LF Wheel Speed"},
                {"20 02 00", "RF Wheel Speed"},
                {"20 03 00", "LR Wheel Speed"},
                {"20 04 00", "RR Wheel Speed"},
                {"20 10 00", "Vehicle Speed"},
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
                            if (p.cmd == "20 01 00") abs->wheelLF = v;
                            else if (p.cmd == "20 02 00") abs->wheelRF = v;
                            else if (p.cmd == "20 03 00") abs->wheelLR = v;
                            else if (p.cmd == "20 04 00") abs->wheelRR = v;
                            else if (p.cmd == "20 10 00") { abs->vehicleSpeed = v; }
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

    *readNext = [this, idx, idList, readNext, perID, done, busType, mod]() {
        if (*idx >= idList->size()) { if (done) done(); return; }
        uint8_t lid = idList->at(*idx);

        if (busType == BusType::KLine) {
            m_kwp->readLocalData(lid, [idx, readNext, perID, lid](const QByteArray &data) {
                if (perID) perID(lid, data);
                (*idx)++;
                QTimer::singleShot(340, *readNext);
            });
        } else {
            // Each module has its own SID prefix
            // TCM(0x28)->2E, ABS(0x40)->20, Airbag(0x60)->28
            uint8_t sidPrefix = 0x22; // default
            uint8_t modAddr = static_cast<uint8_t>(mod);
            if (modAddr == 0x28) sidPrefix = 0x2E;       // TCM
            else if (modAddr == 0x40) sidPrefix = 0x20;   // ABS
            else if (modAddr == 0x60) sidPrefix = 0x28;   // Airbag

            QString cmd = QString("%1 %2 00")
                .arg(sidPrefix, 2, 16, QChar('0'))
                .arg(lid, 2, 16, QChar('0')).toUpper();
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
    // Debug: log first bytes for verification
    QString hexDump;
    for (int i = 0; i < qMin(n, 10); i++)
        hexDump += QString("%1 ").arg(static_cast<uint8_t>(d[i]), 2, 16, QChar('0')).toUpper();
    emit logMessage(QString("ECU block 0x%1: n=%2 [%3]")
        .arg(localID, 2, 16, QChar('0')).toUpper().arg(n).arg(hexDump.trimmed()));

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
            ecu.boostPressure = ecu.mapActual;        // dashboard shows MAP as boost
            emit logMessage(QString("ECU 2112: cool=%1 iat=%2 tps=%3% map=%4 rail=%5bar aap=%6 [n=%7]")
                .arg(ecu.coolantTemp,0,'f',1).arg(ecu.iat,0,'f',1)
                .arg(ecu.tps,0,'f',1).arg(ecu.mapActual)
                .arg(ecu.railActual,0,'f',1).arg(ecu.aap).arg(n));
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
        if (n >= 6) {  // minimum: 61 28 + RPM(2) + InjQty(2)
            ecu.rpm = u16(2);
            ecu.injectionQty = u16(4) / 100.0;

            // Injector correction values at bytes 18-27 (if available)
            if (n >= 28) {
                for (int i = 0; i < 5; i++)
                    ecu.injCorr[i] = s16(18 + i*2) / 100.0;
            }

            // Fuel flow calculation: OM612 = 5 cylinders, 4-stroke
            constexpr double DIESEL_DENSITY = 832.0;  // g/L
            constexpr int CYLINDERS = 5;               // OM612
            ecu.fuelFlowGS = ecu.rpm * ecu.injectionQty * CYLINDERS / (2.0 * 1000.0 * 60.0);
            ecu.fuelFlowLH = ecu.fuelFlowGS * 3600.0 / DIESEL_DENSITY;

            emit logMessage(QString("ECU 2128: rpm=%1 iq=%2mg/str fuel=%3L/h %4g/s [n=%5]")
                .arg(ecu.rpm).arg(ecu.injectionQty,0,'f',1)
                .arg(ecu.fuelFlowLH,0,'f',2).arg(ecu.fuelFlowGS,0,'f',2).arg(n));
        }
        break;
    case 0x62:
        // Block 0x62: 4 data bytes after "61 62"
        // STATIC calibration constants — NEVER changes with RPM/load/temp
        // Real vehicle always: 8A 79 8D 84
        if (n >= 6) {
            emit logMessage(QString("ECU 2162: cal=[%1 %2 %3 %4] (static)")
                .arg(u8(2),2,16,QChar('0')).arg(u8(3),2,16,QChar('0'))
                .arg(u8(4),2,16,QChar('0')).arg(u8(5),2,16,QChar('0')));
        }
        break;
    case 0xB0:
        // Block B0: 2 data bytes after "61 B0"
        // Real vehicle always returns 37 0F — does NOT change with conditions
        // byte[0] = 0x37 (55) byte[1] = 0x0F (15)
        if (n >= 4) {
            emit logMessage(QString("ECU 21B0: byte0=%1(%2) byte1=%3(%4)")
                .arg(u8(2),2,16,QChar('0')).arg(u8(2))
                .arg(u8(3),2,16,QChar('0')).arg(u8(3)));
        }
        break;
    case 0xB1:
        // Block B1: 2 data bytes after "61 B1"
        // Real vehicle always returns D2 15 — does NOT change with conditions
        // Likely two separate 8-bit values, NOT a single s16
        // byte[0] = 0xD2 (210 unsigned, -46 signed)
        // byte[1] = 0x15 (21)
        if (n >= 4) {
            int8_t b0 = static_cast<int8_t>(u8(2));
            uint8_t b1 = u8(3);
            ecu.boostAdapt = b0;
            emit logMessage(QString("ECU 21B1: byte0=%1(%2) byte1=%3(%4)")
                .arg(u8(2),2,16,QChar('0')).arg(b0)
                .arg(b1,2,16,QChar('0')).arg(b1));
        }
        break;
    case 0xB2:
        // Block B2: 2 data bytes after "61 B2"
        // Real vehicle always returns E0 4B — does NOT change with conditions
        // Likely two separate 8-bit values, NOT a single s16
        // byte[0] = 0xE0 (224 unsigned, -32 signed) — possibly fuel qty offset
        // byte[1] = 0x4B (75) — possibly fuel qty limit/factor
        if (n >= 4) {
            int8_t b0 = static_cast<int8_t>(u8(2));
            uint8_t b1 = u8(3);
            ecu.fuelAdapt = b0;  // Use signed byte[0] as fuel adaptation
            emit logMessage(QString("ECU 21B2: byte0=%1(%2) byte1=%3(%4)")
                .arg(u8(2),2,16,QChar('0')).arg(b0)
                .arg(b1,2,16,QChar('0')).arg(b1));
        }
        break;
    case 0x10:
        // Block 0x10: Idle/limits (verified: 18 bytes)
        // [0-1]=005B [2-3]=0045 [4-5]=005A [6-7]=0BB8(3000) [10]=37(55)
        if (n >= 10) {
            ecu.idleRpmTarget = u16(2);
            ecu.maxRpm = u16(6);
            emit logMessage(QString("ECU 2110: idleTgt=%1 maxRpm=%2 byte10=%3")
                .arg(u16(0)).arg(ecu.maxRpm).arg(u8(10)));
        }
        break;
    case 0x14:
        // Block 0x14: (verified: 12 bytes) - content TBD
        if (n >= 4) {
            emit logMessage(QString("ECU 2114: %1 %2 %3 %4 %5 %6")
                .arg(u16(2)).arg(u16(4)).arg(u16(6))
                .arg(u16(8)).arg(u16(10)).arg(u8(12)));
        }
        break;
    case 0x16:
        // Block 0x16: Idle/fuel parameters (verified: 40 bytes)
        // [0-1]=012C(300) [2-3]=2134 [4-5]=012C(300)
        if (n >= 6) {
            emit logMessage(QString("ECU 2116: v0=%1 v2=%2 v4=%3 v16=%4 v18=%5")
                .arg(u16(2)).arg(u16(4)).arg(u16(6))
                .arg(n >= 20 ? u16(18) : 0).arg(n >= 22 ? u16(20) : 0));
        }
        break;
    case 0x24:
        // Block 0x24: RPM thresholds? (verified: 26 bytes)
        // [16-17]=0746(1862) [18-19]=07B9(1977) [20-21]=094E(2382)
        if (n >= 6) {
            emit logMessage(QString("ECU 2124: v0=%1 v2=%2 v16=%3 v18=%4 v20=%5")
                .arg(u16(2)).arg(u16(4))
                .arg(n >= 20 ? u16(18) : 0)
                .arg(n >= 22 ? u16(20) : 0)
                .arg(n >= 24 ? u16(22) : 0));
        }
        break;
    case 0x26:
        // Block 0x26: Sensor/injector raw (verified: 32 bytes)
        // [6-7]=5C6C [12-13]=2FA0 [14-21]=0029 repeated (injector trims?)
        // [28-29]=0BCD (coolant raw?)
        if (n >= 16) {
            ecu.accelPedalRaw = u16(8);    // byte[6-7] of data (raw ADC?)
            emit logMessage(QString("ECU 2126: raw6=%1 raw8=%2 raw12=%3 inj=%4,%5,%6,%7 cool=%8")
                .arg(u16(8)).arg(u16(10)).arg(u16(14))
                .arg(u16(16)).arg(u16(18)).arg(u16(20)).arg(u16(22))
                .arg(n >= 32 ? u16(30) : 0));
        }
        break;
    case 0x30:
        // Block 0x30: RPM setpoints (verified: 26 bytes)
        // [0-1]=02EE(750=idle RPM) [6-7]=0AD9(2777) [8-9]=0AD9
        // [10-11]=03EA(1002=MAP?) [18-19]=0B91(2961)
        if (n >= 4) {
            ecu.idleRpmSet = u16(2);
            emit logMessage(QString("ECU 2130: idleSet=%1 v6=%2 v8=%3 v10=%4 v18=%5")
                .arg(ecu.idleRpmSet).arg(u16(8)).arg(u16(10))
                .arg(u16(12)).arg(n >= 22 ? u16(20) : 0));
        }
        break;
    case 0x18:
        // Block 0x18: (verified: 30 bytes, all zero at idle)
        if (n >= 4) {
            bool allZero = true;
            for (int i = 2; i < n && allZero; i++)
                if (static_cast<uint8_t>(d[i]) != 0) allZero = false;
            if (!allZero)
                emit logMessage(QString("ECU 2118: non-zero data found"));
        }
        break;
    case 0x40:
        // Block 0x40: (verified: 52 bytes, all zero at idle)
        if (n >= 4) {
            bool allZero = true;
            for (int i = 2; i < n && allZero; i++)
                if (static_cast<uint8_t>(d[i]) != 0) allZero = false;
            if (!allZero)
                emit logMessage(QString("ECU 2140: non-zero data found"));
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

    // J1850 VPW DTC response format (ATH1 mode):
    // ABS:    "26 40 62 <count> <DTC_HI DTC_LO STATUS>..."
    // Airbag: "26 60 68 <count> <DTC_HI DTC_LO STATUS>..."
    // KWP:    "xx xx xx 58 <count> <DTC_HI DTC_LO STATUS>..."
    //
    // Find the positive-response data byte: 62, 64, 68, or 58
    // These come after the J1850 header bytes (26 xx)

    int dataStart = -1;
    // Search for response marker after header
    for (int i = 4; i + 1 < c.size(); i += 2) {
        bool ok;
        uint8_t b = c.mid(i, 2).toUInt(&ok, 16);
        if (!ok) continue;
        // Positive response bytes: 0x62, 0x64, 0x68, 0x58
        if (b == 0x62 || b == 0x64 || b == 0x68 || b == 0x58) {
            dataStart = i + 2; // skip the response byte itself
            break;
        }
    }
    if (dataStart < 0 || dataStart + 2 > c.size()) return result;

    // Next byte = DTC count
    bool okCnt;
    int count = c.mid(dataStart, 2).toUInt(&okCnt, 16);
    if (!okCnt || count == 0) return result;

    emit logMessage(QString("%1 J1850 DTC: %2 codes, raw: %3")
        .arg(moduleName(src)).arg(count).arg(resp.trimmed()));

    int pos = dataStart + 2;
    for (int i = 0; i < count && pos + 5 < c.size(); i++, pos += 6) {
        bool ok;
        uint8_t hi = c.mid(pos, 2).toUInt(&ok, 16);
        if (!ok) continue;
        uint8_t lo = c.mid(pos + 2, 2).toUInt(&ok, 16);
        if (!ok) continue;
        uint8_t status = c.mid(pos + 4, 2).toUInt(&ok, 16);
        if (!ok) continue;

        uint16_t raw = (hi << 8) | lo;
        if (raw == 0) continue;

        DTCEntry e;
        // DTC type from upper 2 bits
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
        {"P0520","Oil Pressure Sensor/Switch Circuit"},
        {"P0579","Cruise Control Multi-Function Input"},
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
    if (src == Module::ABS) {
        if (tcmDtcs.contains(code)) return tcmDtcs[code];
    }
    if (src == Module::ABS) {
        if (absDtcs.contains(code)) return absDtcs[code];
    }
    if (src == Module::Airbag) {
        if (airbagDtcs.contains(code)) return airbagDtcs[code];
    }
    if (src == Module::BodyComputer) {
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

// startSession(callback) - compat: K-Line TCM (0x20) for 2.7 CRD
void WJDiagnostics::startSession(std::function<void(bool)> cb)
{
    emit logMessage("Starting TCM session (K-Line 0x20)...");

    switchToModule(Module::KLineTCM, [this, cb](bool ok) {
        if (ok) {
            emit logMessage("K-Line TCM session active (ATSH8120F1)");
            initLiveDataParams();
        }
        if (cb) cb(ok);
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

// readAllLiveData - K-Line TCM (0x20) Block 0x30 = transmission live data
void WJDiagnostics::readAllLiveData(std::function<void(const TCMStatus&)> cb)
{
    auto tcm = std::make_shared<TCMStatus>();

    // Switch to K-Line TCM
    switchToModule(Module::KLineTCM, [this, tcm, cb](bool ok) {
        if (!ok) { if (cb) cb(*tcm); return; }

        // Read block 0x30 - contains 22 bytes of live transmission data
        m_elm->sendCommand("21 30", [this, tcm, cb](const QString &resp) {
            // Validate response is from TCM (0x20), not ECU (0x15)
            // KWP response: XX F1 20 61 30 ... (source=0x20=TCM)
            // If we see F1 15, bus is still on ECU - need reinit
            if (resp.contains("F1 15") && !resp.contains("F1 20")) {
                emit logMessage("WARNING: Response from ECU (0x15), not TCM (0x20) - bus mismatch!");
                // Force reinit on next call
                m_activeBus = BusType::None;
                m_activeModule = Module::MotorECU; // reflect actual state
                if (cb) cb(*tcm);
                return;
            }
            // Parse KWP response: find "61 30" positive response
            if (!resp.contains("NO DATA") && !resp.contains("7F") && !resp.contains("ERROR")) {
                QByteArray raw;
                QString cleaned = resp;
                cleaned.remove(' ').remove('\r').remove('\n');
                int pos = cleaned.indexOf("6130", 0, Qt::CaseInsensitive);
                if (pos >= 0) {
                    // Strip checksum: everything from "6130" to end minus last 2 chars (checksum byte)
                    QString dataHex = cleaned.mid(pos);
                    // Remove last byte (checksum)
                    if (dataHex.length() > 4) dataHex.chop(2);
                    for (int i = 0; i + 1 < dataHex.length(); i += 2) {
                        bool ok2;
                        uint8_t b = dataHex.mid(i, 2).toUInt(&ok2, 16);
                        if (ok2) raw.append(static_cast<char>(b));
                    }
                }
                // raw[0]=0x61, raw[1]=0x30, raw[2..23]=22 bytes data
                if (raw.size() >= 4) {
                    parseTCMBlock30(raw, *tcm);
                }
            }

            // Read battery voltage via ATRV (use as solenoid supply proxy)
            m_elm->sendCommand("ATRV", [this, tcm, cb](const QString &rv) {
                QString v = rv.trimmed().remove('V').remove('v');
                bool ok = false;
                double volts = v.toDouble(&ok);
                if (ok && volts > 0) {
                    tcm->batteryVoltage = volts;
                    tcm->solenoidSupply = volts;  // proxy: solenoid fed from battery
                }
                m_lastTCM = *tcm;
                emit tcmStatusUpdated(*tcm);
                if (cb) cb(*tcm);
            });
        });
    });
}

// parseTCMBlock30 - decode block 0x30 live data (22 bytes)
// Byte mapping TBD - initial test data at idle/P:
// 98 F1 20 61 30 | 00 1A 00 1E 00 00 00 08 04 00 DD 61 FF F7 FF F7 00 00 96 18 00 08
// Byte offsets after "61 30": [0]=00 [1]=1A [2]=00 [3]=1E ...
void WJDiagnostics::parseTCMBlock30(const QByteArray &raw, TCMStatus &tcm)
{
    // raw[0]=0x61, raw[1]=0x30, raw[2..]=data bytes (22 bytes)
    int n = raw.size();
    if (n < 4) return;

    auto u8 = [&](int i) -> uint8_t { return (i+2 < n) ? static_cast<uint8_t>(raw[i+2]) : 0; };
    auto u16 = [&](int i) -> uint16_t { return (uint16_t(u8(i)) << 8) | u8(i+1); };

    // Log raw data for byte mapping analysis
    QString hex;
    for (int i = 2; i < n; i++)
        hex += QString("%1 ").arg(static_cast<uint8_t>(raw[i]), 2, 16, QChar('0')).toUpper();
    emit logMessage("TCM 0x30: " + hex.trimmed());

    // BYTE MAP - verified with real vehicle (2025-03-10, P/D idle + driving):
    // [0-1]   Turbine/N2 sensor (P:49, D idle:222, accel:500, cruise:30-50)
    //         NOTE: during driving values drop very low (30-50) when TCC locked
    //         Possibly N2 clutch drum speed, not raw turbine RPM
    // [2-3]   Engage status (P/N:0x1E=30, D idle:0x37=55, D driving:0x32-0x35)
    // [4-5]   Output Shaft RPM (0 stopped, 600-800+ driving)
    // [6]     Unknown (0 at idle, 1/3 during driving - NOT confirmed as gear)
    // [7]     Selector range: P=8, R=7, N=6, D=5
    // [8]     Config (usually 4, briefly 0xFF/0x05 during shifts)
    // [9-10]  Line pressure (signed, P:221, D idle:1092, driving:600-900)
    // [11]    Trans Temp raw (raw - 40 = Celsius)
    // [12-13] TCC Slip actual (signed, idle:~100, cruise:~150-200)
    // [14-15] TCC Slip desired (signed, tracks actual)
    // [16-17] Unknown (increases during drive: 0x05BF->0x07D0+)
    // [18]    Solenoid mode (P:0x96, D idle:0x08, driving:0x08, shift:0x28/0x80)
    // [19]    Status flags (0x10 idle, 0x10 driving)
    // [20]    Flags (0x00 idle, 0x80 during D engagement, 0x82 briefly)
    // [21]    Flags (always 0x08)

    // Turbine/N2 sensor RPM
    tcm.turbineRPM = u16(0);

    // Engage status
    tcm.engageStatus = u16(2);

    // Output Shaft RPM
    uint16_t outputRPM = u16(4);
    // Vehicle speed from output RPM: NAG1 722.6 final drive ~3.27, tire ~2.1m circ
    // speed_kmh = outputRPM * 60 * 2.1 / (3.27 * 1000) = outputRPM * 0.0385
    tcm.vehicleSpeed = outputRPM * 0.0385;
    tcm.outputRPM = outputRPM;

    // Trans Temp
    tcm.transTemp = u8(11) - 40;

    // Line pressure (signed, byte 9-10)
    int16_t rawLP = static_cast<int16_t>(u16(9));
    tcm.linePressure = rawLP;

    // TCC Slip actual (signed, byte 12-13)
    int16_t rawSlipA = static_cast<int16_t>(u16(12));
    tcm.actualTCCslip = rawSlipA;

    // TCC Slip desired (signed, byte 14-15)
    int16_t rawSlipD = static_cast<int16_t>(u16(14));
    tcm.desTCCslip = rawSlipD;

    // Solenoid mode bitmask
    tcm.solenoidMode = u8(18);

    // Solenoid supply: NOT available in block 0x30
    tcm.solenoidSupply = 0;

    // Gear from byte[7]: SELECTOR RANGE, not actual gear
    // P=8, R=7, N=6, D=5 (all drive gears show as 5)
    tcm.gearByte = u8(7);
    switch (tcm.gearByte) {
    case 8: tcm.currentGear = Gear::Park;    tcm.actualGear = 0; break;
    case 7: tcm.currentGear = Gear::Reverse; tcm.actualGear = 1; break;
    case 6: tcm.currentGear = Gear::Neutral; tcm.actualGear = 2; break;
    case 5: // D range - all drive gears (1-5) show as byte[7]=5
        // Gear estimation from RPM ratio when available.
        // byte[0-1] = N2 sensor: unreliable during TCC lockup (drops to 30-50).
        // Use ratio only when N2 > 100 AND output > 100 (torque converter open).
        // When TCC locked (N2 < 100), show "D" without gear number.
        if (tcm.turbineRPM > 100 && outputRPM > 100) {
            double ratio = tcm.turbineRPM / (double)outputRPM;
            // NAG1 722.6 ratios: 1st=3.59, 2nd=2.19, 3rd=1.41, 4th=1.00, 5th=0.83
            if (ratio > 2.8)      { tcm.currentGear = Gear::Drive1; tcm.actualGear = 3; }
            else if (ratio > 1.7) { tcm.currentGear = Gear::Drive2; tcm.actualGear = 4; }
            else if (ratio > 1.15){ tcm.currentGear = Gear::Drive3; tcm.actualGear = 5; }
            else if (ratio > 0.9) { tcm.currentGear = Gear::Drive4; tcm.actualGear = 6; }
            else                  { tcm.currentGear = Gear::Drive5; tcm.actualGear = 7; }
        } else if (outputRPM > 50) {
            // Moving but TCC locked — cannot determine gear
            tcm.currentGear = Gear::Drive1; tcm.actualGear = 3; // "D"
        } else {
            tcm.currentGear = Gear::Drive1; tcm.actualGear = 3; // D idle
        }
        break;
    default: tcm.currentGear = Gear::Unknown; tcm.actualGear = -1; break;
    }
    tcm.selectedGear = tcm.actualGear;
}

// readSingleParam - K-Line KWP2000 ReadLocalData tek blok oku
void WJDiagnostics::readSingleParam(uint8_t localID, std::function<void(double)> cb)
{
    QString cmd = QString("21 %1").arg(localID, 2, 16, QChar('0')).toUpper();
    m_elm->sendCommand(cmd, [this, localID, cb](const QString &resp) {
        double val = 0;

        // KWP response: "83 F1 20 61 xx data... checksum"
        // Parse: find "61 xx" positive response
        if (!resp.contains("NO DATA") && !resp.contains("7F") && !resp.contains("ERROR")) {
            QString cleaned = resp;
            cleaned.remove(' ').remove('\r').remove('\n');
            QString blkHex = QString("61%1").arg(localID, 2, 16, QChar('0')).toUpper();
            int pos = cleaned.indexOf(blkHex, 0, Qt::CaseInsensitive);
            if (pos >= 0) {
                QByteArray raw;
                QString dataHex = cleaned.mid(pos);
                for (int i = 0; i + 1 < dataHex.length(); i += 2)
                    raw.append(static_cast<char>(dataHex.mid(i, 2).toUInt(nullptr, 16)));

                // raw[0]=0x61, raw[1]=blk, raw[2...]=data
                if (raw.size() >= 3) {
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
            }
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

// readTCMInfo - K-Line KWP2000 ReadEcuIdentification (SID 0x1A)
void WJDiagnostics::readTCMInfo(std::function<void(const QMap<QString,QString>&)> cb)
{
    auto r = std::make_shared<QMap<QString,QString>>();
    (*r)["Module"] = "NAG1 722.6 TCM";
    (*r)["Bus"] = "K-Line (0x20)";

    switchToModule(Module::KLineTCM, [this, r, cb](bool ok) {
        if (!ok) { if (cb) cb(*r); return; }

        // KWP2000 ReadEcuIdentification: 1A 86 (manufacturer), 1A 90 (VIN), 1A 91 (HW)
        m_elm->sendCommand("1A 86", [this, r, cb](const QString &resp1) {
            (*r)["Manufacturer"] = resp1.contains("NO DATA") ? "N/A" : resp1.trimmed();

            m_elm->sendCommand("1A 90", [this, r, cb](const QString &resp2) {
                (*r)["PartNumber"] = resp2.contains("NO DATA") ? "N/A" : resp2.trimmed();

                m_elm->sendCommand("1A 91", [this, r, cb](const QString &resp3) {
                    (*r)["HardwareVersion"] = resp3.contains("NO DATA") ? "N/A" : resp3.trimmed();
                    if (cb) cb(*r);
                });
            });
        });
    });
}

// initLiveDataParams - TCM J1850 VPW PID listesi (referans)
void WJDiagnostics::initLiveDataParams()
{
    // TCM K-Line (0x20) ReadLocalData parameters
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
        // ECU (0x15) parameters - localID 0xE0+ range (virtual IDs for ECU block data)
        {0xF0, "Engine RPM",                  "rpm",   0, 6000, 1.0,    0, 2, false},
        {0xF1, "Coolant Temp",                "C",   -40, 150,  1.0,    0, 2, false},
        {0xF2, "Intake Air Temp",             "C",   -40, 100,  1.0,    0, 2, false},
        {0xF3, "Throttle Position",           "%",     0, 100,  1.0,    0, 2, false},
        {0xF4, "Boost Pressure",              "mbar",  0, 3000, 1.0,    0, 2, false},
        {0xF5, "MAF Actual",                  "mg/s",  0, 2000, 1.0,    0, 2, false},
        {0xF6, "Rail Pressure",               "bar",   0, 2000, 1.0,    0, 2, false},
        {0xF7, "Injection Qty",               "mg",    0, 100,  1.0,    0, 2, false},
        {0xF8, "Battery Voltage",             "V",     0,  20,  1.0,    0, 2, false},
    };
    emit logMessage(QString("Live data: %1 parameters loaded").arg(m_liveParams.size()));
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

    switchToModule(Module::KLineTCM, [readNext](bool ok) {
        if (ok) (*readNext)();
    });
}
