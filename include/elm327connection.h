#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QQueue>
#include <functional>

/**
 * ELM327 WiFi Connection Handler
 * 
 * Jeep WJ 2.7 CRD iletişim protokolleri:
 *   - ECM/TCM: K-Line (ISO 9141-2) → ELM327 ATSP 3
 *   - Diğer modüller: J1850 VPW (PCI Bus) → ELM327 ATSP 2
 * 
 * WiFi ELM327 varsayılan: 192.168.0.10:35000 (bazıları :35000 yerine :23 kullanır)
 */
class ELM327Connection : public QObject
{
    Q_OBJECT

public:
    enum class Protocol {
        Auto       = 0,
        J1850_PWM  = 1,  // SAE J1850 PWM (41.6 kbaud) - Ford
        J1850_VPW  = 2,  // SAE J1850 VPW (10.4 kbaud) - GM/Chrysler PCI Bus
        ISO_9141   = 3,  // ISO 9141-2 (K-Line) - WJ ECM/TCM
        KWP_5BAUD  = 4,  // ISO 14230-4 KWP 5-baud init
        KWP_FAST   = 5,  // ISO 14230-4 KWP fast init
        CAN_11_500 = 6,
        CAN_29_500 = 7,
        CAN_11_250 = 8,
        CAN_29_250 = 9
    };
    Q_ENUM(Protocol)

    enum class ConnectionState {
        Disconnected,
        Connecting,
        Initializing,
        Ready,
        Busy,
        Error
    };
    Q_ENUM(ConnectionState)

    struct ATCommand {
        QString command;
        std::function<void(const QString&)> callback;
        int timeoutMs = 3000;
    };

    explicit ELM327Connection(QObject *parent = nullptr);
    ~ELM327Connection();

    // Bağlantı
    void connectToDevice(const QString &host, quint16 port = 35000);
    void disconnect();
    bool isConnected() const;
    ConnectionState state() const { return m_state; }

    // Protokol ayarı
    void setProtocol(Protocol proto);
    Protocol currentProtocol() const { return m_protocol; }

    // ELM327 komut gönderme
    void sendCommand(const QString &cmd,
                     std::function<void(const QString&)> callback = nullptr,
                     int timeoutMs = 3000);

    // Ham OBD komutu gönderme (hex string)
    void sendOBDCommand(const QByteArray &hexCmd,
                        std::function<void(const QByteArray&)> callback = nullptr,
                        int timeoutMs = 5000);

    // ELM327 bilgileri
    QString elmVersion() const { return m_elmVersion; }
    QString elmVoltage() const { return m_elmVoltage; }
    bool isGenuineELM() const { return m_genuineELM; }

signals:
    void connected();
    void disconnected();
    void stateChanged(ConnectionState state);
    void errorOccurred(const QString &error);
    void rawDataReceived(const QByteArray &data);
    void logMessage(const QString &msg);
    void fakeELMDetected(const QString &reason);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onDataReady();
    void onCommandTimeout();
    void processNextCommand();

private:
    void setState(ConnectionState state);
    void initializeELM();
    void parseResponse(const QString &response);
    QByteArray parseHexResponse(const QString &response);

    QTcpSocket *m_socket = nullptr;
    QTimer *m_timeoutTimer = nullptr;
    QTimer *m_processTimer = nullptr;

    ConnectionState m_state = ConnectionState::Disconnected;
    Protocol m_protocol = Protocol::ISO_9141;

    QQueue<ATCommand> m_commandQueue;
    ATCommand m_currentCommand;
    bool m_commandPending = false;
    QString m_responseBuffer;

    QString m_elmVersion;
    QString m_elmVoltage;
    bool m_genuineELM = true;
};
