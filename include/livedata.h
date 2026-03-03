#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QList>
#include <QFile>
#include <QTextStream>
#include "tcmdiagnostics.h"

/**
 * Live Data Polling Manager - Dual Mode (TCM J1850 + Motor ECU K-Line)
 *
 * Calisma modlari:
 *   TCM_ONLY   - Sadece TCM J1850 PID'leri (varsayilan)
 *   ECU_ONLY   - Sadece Motor ECU K-Line bloklari
 *   DUAL       - TCM + ECU donusumlu
 *
 * DUAL modda her cycle:
 *   1. TCM PID'leri oku (J1850 ATSP2)
 *   2. K-Line'a gec (ATSP5), ECU bloklari oku (21 12, 21 28, ...)
 *   3. J1850'ye geri don (ATSP2)
 *   4. dataUpdated + ecuDataUpdated signal gonder
 */
class LiveDataManager : public QObject
{
    Q_OBJECT

public:
    enum Mode { TCM_ONLY, ECU_ONLY, DUAL };

    struct DataPoint {
        qint64  timestamp;
        uint8_t localID;
        double  value;
        QString paramName;
    };

    explicit LiveDataManager(TCMDiagnostics *tcm, QObject *parent = nullptr);

    void startPolling(int intervalMs = 200);
    void stopPolling();
    bool isPolling() const { return m_polling; }

    void setMode(Mode m) { m_mode = m; }
    Mode mode() const { return m_mode; }

    void setSelectedParams(const QList<uint8_t> &localIDs);
    QList<uint8_t> selectedParams() const { return m_selectedParams; }

    void setECUBlocks(const QList<uint8_t> &blockIDs) { m_ecuBlocks = blockIDs; }

    void startLogging(const QString &filePath);
    void stopLogging();
    bool isLogging() const { return m_logging; }

    QMap<uint8_t, double> lastValues() const { return m_lastValues; }
    TCMDiagnostics::ECUStatus lastECU() const { return m_lastECU; }

signals:
    void dataUpdated(const QMap<uint8_t, double> &values);
    void newDataPoint(const DataPoint &point);
    void fullStatusUpdated(const TCMDiagnostics::TCMStatus &status);
    void ecuDataUpdated(const TCMDiagnostics::ECUStatus &ecu);

private slots:
    void onPollTimer();

private:
    void pollTCM(std::function<void()> then = nullptr);
    void pollECU(std::function<void()> then = nullptr);
    void logCurrentValues();

    TCMDiagnostics *m_tcm;
    QTimer *m_pollTimer;
    QElapsedTimer m_elapsed;

    Mode m_mode = TCM_ONLY;
    bool m_polling = false;
    bool m_logging = false;
    bool m_readPending = false;

    QList<uint8_t> m_selectedParams;
    QList<uint8_t> m_ecuBlocks = {0x12, 0x28};
    QMap<uint8_t, double> m_lastValues;
    TCMDiagnostics::ECUStatus m_lastECU;

    QFile *m_logFile = nullptr;
    QTextStream *m_logStream = nullptr;
};
