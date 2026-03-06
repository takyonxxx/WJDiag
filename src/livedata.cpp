#include "livedata.h"
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>

LiveDataManager::LiveDataManager(TCMDiagnostics *tcm, QObject *parent)
    : QObject(parent), m_tcm(tcm)
{
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout,
            this, &LiveDataManager::onPollTimer);
}

void LiveDataManager::startPolling(int intervalMs)
{
    if (m_polling) return;

    m_polling = true;
    m_elapsed.start();
    m_pollTimer->start(intervalMs);
}

void LiveDataManager::stopPolling()
{
    m_polling = false;
    m_pollTimer->stop();
    stopLogging();
}

void LiveDataManager::setSelectedParams(const QList<uint8_t> &localIDs)
{
    m_selectedParams = localIDs;
}

void LiveDataManager::startLogging(const QString &filePath)
{
    if (m_logging) stopLogging();

    m_logFile = new QFile(filePath, this);
    if (!m_logFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
        delete m_logFile;
        m_logFile = nullptr;
        return;
    }

    m_logStream = new QTextStream(m_logFile);

    // CSV baslik: TCM parametreleri + ECU parametreleri
    *m_logStream << "Timestamp(ms),DateTime";
    for (const auto &param : m_tcm->liveDataParams()) {
        if (m_selectedParams.isEmpty() || m_selectedParams.contains(param.localID)) {
            *m_logStream << "," << param.name << "(" << param.unit << ")";
        }
    }
    // ECU bloklari aktifse ECU sutunlari ekle
    if (m_mode == ECU_ONLY || m_mode == DUAL) {
        *m_logStream << ",ECU_RPM,ECU_Coolant(C),ECU_IAT(C),ECU_TPS(%)"
                     << ",ECU_Boost(mbar),ECU_BoostSet(mbar)"
                     << ",ECU_InjQty(mg),ECU_BattV(V)"
                     << ",ECU_MAF_Act,ECU_MAF_Spec"
                     << ",ECU_RailAct(bar),ECU_RailSpec(bar)";
    }
    *m_logStream << "\n";
    m_logStream->flush();

    m_logging = true;
}

void LiveDataManager::stopLogging()
{
    if (!m_logging) return;

    m_logging = false;
    if (m_logStream) {
        m_logStream->flush();
        delete m_logStream;
        m_logStream = nullptr;
    }
    if (m_logFile) {
        m_logFile->close();
        delete m_logFile;
        m_logFile = nullptr;
    }
}

// === Ana polling dispatcher ===

void LiveDataManager::onPollTimer()
{
    if (!m_polling || m_readPending) return;
    m_readPending = true;

    switch (m_mode) {
    case TCM_ONLY:
        pollTCM([this]() { m_readPending = false; });
        break;
    case ECU_ONLY:
        pollECU([this]() { m_readPending = false; });
        break;
    case DUAL:
        // Once TCM oku, sonra ECU oku
        pollTCM([this]() {
            pollECU([this]() {
                m_readPending = false;
            });
        });
        break;
    }
}

// === TCM Polling (J1850 VPW) ===

void LiveDataManager::pollTCM(std::function<void()> then)
{
    if (m_selectedParams.isEmpty()) {
        // Tum TCM PID'leri oku (readAllLiveData)
        m_tcm->readAllLiveData([this, then](const TCMDiagnostics::TCMStatus &status) {
            emit fullStatusUpdated(status);
            logCurrentValues();
            if (then) then();
        });
    } else {
        // Sadece secili parametreleri oku
        struct ReadCtx {
            int index = 0;
            QList<uint8_t> params;
            QMap<uint8_t, double> values;
        };
        auto ctx = std::make_shared<ReadCtx>();
        ctx->params = m_selectedParams;

        auto readNext = std::make_shared<std::function<void()>>();
        *readNext = [this, ctx, readNext, then]() {
            if (ctx->index >= ctx->params.size()) {
                m_lastValues = ctx->values;
                emit dataUpdated(ctx->values);
                logCurrentValues();
                if (then) then();
                return;
            }
            uint8_t localID = ctx->params[ctx->index];
            m_tcm->readSingleParam(localID, [ctx, readNext](double val) {
                ctx->values[ctx->params[ctx->index]] = val;
                ctx->index++;
                QTimer::singleShot(10, *readNext);
            });
        };
        (*readNext)();
    }
}

// === ECU Polling (K-Line) ===

void LiveDataManager::pollECU(std::function<void()> then)
{
    m_tcm->readECULiveData([this, then](const TCMDiagnostics::ECUStatus &ecu) {
        m_lastECU = ecu;
        emit ecuDataUpdated(ecu);

        // ECU verilerini virtual ID'lerle dataUpdated signal'ina ekle
        QMap<uint8_t, double> ecuValues;
        ecuValues[0xF0] = ecu.rpm;
        ecuValues[0xF1] = ecu.coolantTemp;
        ecuValues[0xF2] = ecu.iat;
        ecuValues[0xF3] = ecu.tps;
        ecuValues[0xF4] = ecu.boostPressure;
        ecuValues[0xF5] = ecu.mafActual;
        ecuValues[0xF6] = ecu.railActual;
        ecuValues[0xF7] = ecu.injectionQty;
        ecuValues[0xF8] = ecu.batteryVoltage;
        // Merge with existing TCM values
        for (auto it = ecuValues.begin(); it != ecuValues.end(); ++it)
            m_lastValues[it.key()] = it.value();
        emit dataUpdated(m_lastValues);

        // DUAL modda: KLineTCM'e geri don
        if (m_mode == DUAL) {
            m_tcm->switchToModule(TCMDiagnostics::Module::KLineTCM, [then](bool) {
                if (then) then();
            });
        } else {
            if (then) then();
        }
    });
}

// === Loglama ===

void LiveDataManager::logCurrentValues()
{
    if (!m_logging || !m_logStream) return;

    qint64 ts = m_elapsed.elapsed();
    *m_logStream << ts << ","
                 << QDateTime::currentDateTime().toString(Qt::ISODate);

    // TCM parametreleri
    for (const auto &param : m_tcm->liveDataParams()) {
        if (m_selectedParams.isEmpty() || m_selectedParams.contains(param.localID)) {
            *m_logStream << "," << m_lastValues.value(param.localID, 0);
        }
    }

    // ECU verileri
    if (m_mode == ECU_ONLY || m_mode == DUAL) {
        *m_logStream << "," << m_lastECU.rpm
                     << "," << m_lastECU.coolantTemp
                     << "," << m_lastECU.iat
                     << "," << m_lastECU.tps
                     << "," << m_lastECU.boostPressure
                     << "," << m_lastECU.boostSetpoint
                     << "," << m_lastECU.injectionQty
                     << "," << m_lastECU.batteryVoltage
                     << "," << m_lastECU.mafActual
                     << "," << m_lastECU.mafSpec
                     << "," << m_lastECU.railActual
                     << "," << m_lastECU.railSpec;
    }

    *m_logStream << "\n";
    m_logStream->flush();
}
