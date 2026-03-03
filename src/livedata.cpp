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

    // CSV başlık satırı
    *m_logStream << "Timestamp(ms),DateTime";
    for (const auto &param : m_tcm->liveDataParams()) {
        if (m_selectedParams.contains(param.localID)) {
            *m_logStream << "," << param.name << "(" << param.unit << ")";
        }
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

void LiveDataManager::onPollTimer()
{
    if (!m_polling || m_readPending) return;

    if (m_selectedParams.isEmpty()) {
        // Seçili parametre yoksa tüm durumu oku
        m_readPending = true;
        m_tcm->readAllLiveData([this](const TCMDiagnostics::TCMStatus &status) {
            m_readPending = false;
            emit fullStatusUpdated(status);
        });
    } else {
        // Sadece seçili parametreleri sırayla oku
        m_readPending = true;

        struct ReadCtx {
            int index = 0;
            QList<uint8_t> params;
            QMap<uint8_t, double> values;
        };
        auto ctx = std::make_shared<ReadCtx>();
        ctx->params = m_selectedParams;

        auto readNext = std::make_shared<std::function<void()>>();
        *readNext = [this, ctx, readNext]() {
            if (ctx->index >= ctx->params.size()) {
                // Tüm parametreler okundu
                m_lastValues = ctx->values;
                m_readPending = false;

                emit dataUpdated(ctx->values);

                // Loglama
                if (m_logging && m_logStream) {
                    qint64 ts = m_elapsed.elapsed();
                    *m_logStream << ts << ","
                                << QDateTime::currentDateTime().toString(Qt::ISODate);
                    for (const auto &param : m_tcm->liveDataParams()) {
                        if (m_selectedParams.contains(param.localID)) {
                            *m_logStream << ","
                                         << ctx->values.value(param.localID, 0);
                        }
                    }
                    *m_logStream << "\n";
                    m_logStream->flush();
                }
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
