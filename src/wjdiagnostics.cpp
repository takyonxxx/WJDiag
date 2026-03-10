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

        // J1850 VPW modules - verified headers from APK
        {Module::TCM, "NAG1 722.6 Transmission (EGS52)", "TCM",
         BusType::J1850, "ATSH242822", "", "ATSP2", ""},
        {Module::EVIC, "Overhead Console (EVIC)", "EVIC",
         BusType::J1850, "ATSH242A22", "", "ATSP2", ""},
        {Module::ABS, "ABS / ESP Braking", "ABS",
         BusType::J1850, "ATSH244022", "", "ATSP2", "ATRA40"},
        {Module::Airbag, "Airbag (ORC/AOSIM)", "Airbag",
         BusType::J1850, "ATSH246022", "", "ATSP2", "ATRA60"},
        {Module::SKIM, "SKIM Immobilizer", "SKIM",
         BusType::J1850, "ATSH246222", "", "ATSP2", ""},
        {Module::ATC, "Climate Control (HVAC)", "HVAC",
         BusType::J1850, "ATSH246822", "", "ATSP2", ""},
        {Module::BCM, "Body Computer (BCM)", "BCM",
         BusType::J1850, "ATSH248022", "", "ATSP2", "ATRA80"},
        {Module::Radio, "Radio / Audio", "Radio",
         BusType::J1850, "ATSH248722", "", "ATSP2", ""},
        {Module::Cluster, "Instrument Cluster", "Cluster",
         BusType::J1850, "ATSH249022", "", "ATSP2", ""},
        {Module::MemSeat, "Memory Seat / Mirror", "Seat",
         BusType::J1850, "ATSH249822", "", "ATSP2", ""},
        {Module::Liftgate, "Power Liftgate", "Liftgate",
         BusType::J1850, "ATSH24A022", "", "ATSP2", ""},
        {Module::HandsFree, "HandsFree / Uconnect", "HFM",
         BusType::J1850, "ATSH24A122", "", "ATSP2", ""},
        {Module::ParkAssist, "Park Assist", "Park",
         BusType::J1850, "ATSH24C022", "", "ATSP2", ""},
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

                            // TCM needs DiagSession before data read (APK verified)
                            if (targetMod == Module::TCM) {
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
        // Same bus - check if K-Line needs full reinit
        if (newBus == BusType::KLine) {
            // K-Line: ALWAYS needs full ATZ+ATWM+ATFI+81 sequence
            // Session is lost when switching to another K-Line module and back
            emit logMessage("K-Line: full reinit required");
            m_activeBus = BusType::None; // Force different-bus path
            switchToModule(mod, done);   // Recurse with bus mismatch
            return;
        }
        // J1850 same bus - just change header
        m_elm->sendCommand(info.atshHeader, [this, info, done, targetMod](const QString&) {
            auto finalize = [this, info, done, targetMod]() {
                m_activeModule = targetMod;
                emit logMessage(QString("Active: %1 | %2").arg(info.shortName, info.atshHeader));

                // TCM needs DiagSession before data read (APK verified)
                if (targetMod == Module::TCM) {
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
            // J1850 DTC read (APK verified):
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
                if (!resp.contains("NO DATA") && !resp.contains("ERROR") && resp.contains("62")) {
                    // Parse DTC bytes from response
                    // Response format: header + 62/64/68 + DTC data bytes
                    emit logMessage(QString("%1 DTC raw: %2")
                        .arg(moduleInfo(mod).shortName, resp.trimmed()));
                    // TODO: decode DTC bytes into P/C/B/U codes
                    // For now log raw response for analysis
                }
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
            // J1850 DTC clear (APK verified):
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
            m_lastTCM = *tcm;
            emit tcmStatusUpdated(*tcm);
            if (cb) cb(*tcm);
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
            // APK format: each module has its own SID prefix
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
            // Read battery voltage
            m_elm->sendCommand("ATRV", [this, tcm, cb](const QString &rv) {
                QString v = rv.trimmed().remove('V').remove('v');
                bool ok2 = false;
                double volts = v.toDouble(&ok2);
                if (ok2 && volts > 0) tcm->batteryVoltage = volts;
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
    // raw[0]=0x61, raw[1]=0x30, raw[2..]=data bytes
    int n = raw.size();
    if (n < 4) return;

    auto u8 = [&](int i) -> uint8_t { return (i+2 < n) ? static_cast<uint8_t>(raw[i+2]) : 0; };
    auto u16 = [&](int i) -> uint16_t { return (uint16_t(u8(i)) << 8) | u8(i+1); };

    // Log raw data for byte mapping analysis
    QString hex;
    for (int i = 2; i < n; i++)
        hex += QString("%1 ").arg(static_cast<uint8_t>(raw[i]), 2, 16, QChar('0')).toUpper();
    emit logMessage("TCM 0x30: " + hex.trimmed());

    // Tentative byte mapping (will be refined with driving test data):
    // Based on Mercedes 722.6 EGS52 typical block 0x30 structure
    // and comparison with known idle values:
    // [0-1] = 001A (26) - could be gear/status
    // [2-3] = 001E (30) - could be temperature+40 offset (30-40=-10? or raw)
    // [4-5] = 0000 - could be vehicle speed (0 at idle)
    // [6-7] = 0008 (8) - 
    // [8]   = 04 - could be selector position (P=4?)
    // [9-10] = 00DD (221) - could be turbine RPM (~750/3.4?)
    // [11]  = 61 - could be trans temp raw (97-40=57C?)
    // [12-13] = FFF7 - signed? (-9)
    // [14-15] = FFF7 - signed? (-9)
    // [16-17] = 0000
    // [18]  = 96 (150) - could be solenoid voltage * some factor
    // [19]  = 18 (24)
    // [20-21] = 0008

    // VERIFIED BYTE MAPPING (real vehicle 2025-03-07, P/R/N/D tested):
    // [0-1]   Turbine/Input RPM (P/N: ~20, R/D idle: ~750, D+gas: ~1100+)
    // [2-3]   Engagement status (0x1E=30 for P/N, 0x3C=60 for R/D)
    // [4-5]   Vehicle Speed (0 when stationary)
    // VERIFIED BYTE MAPPING (real vehicle, idle + driving tested):
    // [0-1]   Turbine/Input RPM (P/N: ~10-20, R/D idle: ~750, D driving: 300-1900)
    // [2-3]   Engage status (0x1E=30 for P/N, 0x3C=60 for R/D, varies during shifts)
    // [4-5]   Output Shaft RPM (0 when stopped, 600-800+ when driving)
    // [7]     Gear range: P=8, R=7, N=6, D=5 (NOT actual gear number!)
    // [8]     Config (always 4)
    // [9-10]  Line pressure (signed, varies widely: P:221, D:17, driving:1000+)
    // [11]    Trans Temp raw (subtract 40 for Celsius)
    // [12-13] TCC Slip actual (signed)
    // [14-15] TCC Slip desired (signed)
    // [16-17] Additional data (changes during driving)
    // [18]    Solenoid mode bitmask (P/N:0x96, D idle:0x00, D driving:0x08, decel:0x48)
    // [19]    Status flags
    // [20-21] Flags

    // Turbine RPM
    tcm.turbineRPM = u16(0);

    // Output Shaft RPM (NOT vehicle speed directly)
    uint16_t outputRPM = u16(4);
    // Vehicle speed from output RPM: NAG1 722.6 final drive ~3.27, tire ~2.1m circ
    // speed_kmh = outputRPM * 60 * 2.1 / (3.27 * 1000) ≈ outputRPM * 0.0385
    tcm.vehicleSpeed = outputRPM * 0.0385;
    tcm.outputRPM = outputRPM;

    // Trans Temp
    tcm.transTemp = u8(11) - 40;

    // Solenoid supply: NOT available in block 0x30
    // byte[9-10] is line pressure, not voltage
    // byte[18] is mode bitmask, not voltage
    tcm.solenoidSupply = 0;  // unknown from this block

    // Gear from byte[7]: this is SELECTOR RANGE, not actual gear
    // P=8, R=7, N=6, D=5 (all drive gears show as 5)
    uint8_t gearByte = u8(7);
    switch (gearByte) {
    case 8: tcm.currentGear = Gear::Park;    tcm.actualGear = 0; break;
    case 7: tcm.currentGear = Gear::Reverse; tcm.actualGear = 1; break;
    case 6: tcm.currentGear = Gear::Neutral; tcm.actualGear = 2; break;
    case 5: // D range - all drive gears (1-5) show as byte[7]=5
        // Estimate actual gear from RPM ratio if both turbine and output are valid
        if (tcm.turbineRPM > 100 && outputRPM > 100) {
            double ratio = tcm.turbineRPM / (double)outputRPM;
            // NAG1 722.6 ratios: 1st=3.59, 2nd=2.19, 3rd=1.41, 4th=1.00, 5th=0.83
            if (ratio > 2.8)      { tcm.currentGear = Gear::Drive1; tcm.actualGear = 3; }
            else if (ratio > 1.7) { tcm.currentGear = Gear::Drive2; tcm.actualGear = 4; }
            else if (ratio > 1.15){ tcm.currentGear = Gear::Drive3; tcm.actualGear = 5; }
            else if (ratio > 0.9) { tcm.currentGear = Gear::Drive4; tcm.actualGear = 6; }
            else                  { tcm.currentGear = Gear::Drive5; tcm.actualGear = 7; }
        } else {
            tcm.currentGear = Gear::Drive1; tcm.actualGear = 3; // D idle, no motion
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
