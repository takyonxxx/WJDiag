#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QList>
#include <QFile>
#include <QTextStream>
#include "tcmdiagnostics.h"

/**
 * Live Data Polling Manager
 * 
 * Belirli aralıklarla seçilen parametreleri okur ve günceller.
 * Log kayıt desteği ile birlikte.
 */
class LiveDataManager : public QObject
{
    Q_OBJECT

public:
    struct DataPoint {
        qint64  timestamp;  // ms (başlangıçtan itibaren)
        uint8_t localID;
        double  value;
        QString paramName;
    };

    explicit LiveDataManager(TCMDiagnostics *tcm, QObject *parent = nullptr);

    void startPolling(int intervalMs = 200);
    void stopPolling();
    bool isPolling() const { return m_polling; }

    // Hangi parametrelerin okunacağını seç
    void setSelectedParams(const QList<uint8_t> &localIDs);
    QList<uint8_t> selectedParams() const { return m_selectedParams; }

    // Loglama
    void startLogging(const QString &filePath);
    void stopLogging();
    bool isLogging() const { return m_logging; }

    // Son değerler
    QMap<uint8_t, double> lastValues() const { return m_lastValues; }

signals:
    void dataUpdated(const QMap<uint8_t, double> &values);
    void newDataPoint(const DataPoint &point);
    void fullStatusUpdated(const TCMDiagnostics::TCMStatus &status);

private slots:
    void onPollTimer();

private:
    TCMDiagnostics *m_tcm;
    QTimer *m_pollTimer;
    QElapsedTimer m_elapsed;

    bool m_polling = false;
    bool m_logging = false;
    bool m_readPending = false;

    QList<uint8_t> m_selectedParams;
    QMap<uint8_t, double> m_lastValues;

    QFile *m_logFile = nullptr;
    QTextStream *m_logStream = nullptr;
};
