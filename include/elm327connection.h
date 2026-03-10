#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QQueue>
#include <QIODevice>
#include <functional>

// Bluetooth: iOS ve Android icin
#if __has_include(<QBluetoothSocket>)
#include <QBluetoothSocket>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyDescriptor>
#define HAS_BLUETOOTH 1
#else
#define HAS_BLUETOOTH 0
#endif

/**
 * ELM327 Connection Handler - WiFi (TCP) + Bluetooth (SPP/RFCOMM)
 *
 * Baglanti tipleri:
 *   WiFi:      TCP 192.168.0.10:35000 (klon ELM327'ler)
 *   Bluetooth: RFCOMM/SPP (orijinal ELM327, OBDLink, vLinker vs.)
 *
 * Jeep WJ 2.7 CRD protokolleri:
 *   J1850 VPW (ATSP2) - TCM/ABS/Airbag
 *   K-Line ISO 14230-4 (ATSP5) - Motor ECU (sadece orijinal ELM327)
 */
class ELM327Connection : public QObject
{
    Q_OBJECT

public:
    enum class Transport { WiFi, Bluetooth };
    Q_ENUM(Transport)

    enum class Protocol {
        Auto       = 0,
        J1850_PWM  = 1,
        J1850_VPW  = 2,
        ISO_9141   = 3,
        KWP_5BAUD  = 4,
        KWP_FAST   = 5,
        CAN_11_500 = 6,
        CAN_29_500 = 7,
        CAN_11_250 = 8,
        CAN_29_250 = 9
    };
    Q_ENUM(Protocol)

    enum class ConnectionState {
        Disconnected,
        Scanning,       // BT cihaz tarama
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
        int timeoutMs = 0;  // 0 = use m_defaultTimeoutMs
    };

    explicit ELM327Connection(QObject *parent = nullptr);
    ~ELM327Connection();

    // WiFi baglanti
    void connectToDevice(const QString &host, quint16 port = 35000);

    // Bluetooth baglanti
    void connectBluetooth(const QString &address = QString());
    void scanBluetooth();
    void stopScan();

    void disconnect();
    bool isConnected() const;
    ConnectionState state() const { return m_state; }
    Transport transport() const { return m_transport; }

    void setProtocol(Protocol proto);
    Protocol currentProtocol() const { return m_protocol; }

    void sendCommand(const QString &cmd,
                     std::function<void(const QString&)> callback = nullptr,
                     int timeoutMs = 0);

    void sendOBDCommand(const QByteArray &hexCmd,
                        std::function<void(const QByteArray&)> callback = nullptr,
                        int timeoutMs = 0);

    void setDefaultTimeout(int ms) { m_defaultTimeoutMs = ms; }
    int defaultTimeout() const { return m_defaultTimeoutMs; }

    QString elmVersion() const { return m_elmVersion; }
    QString elmVoltage() const { return m_elmVoltage; }

signals:
    void connected();
    void disconnected();
    void stateChanged(ConnectionState state);
    void errorOccurred(const QString &error);
    void rawDataReceived(const QByteArray &data);
    void logMessage(const QString &msg);

#if HAS_BLUETOOTH
    void bluetoothDeviceFound(const QString &name, const QString &address);
    void bluetoothScanFinished();
#endif

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onDataReady();
    void onCommandTimeout();
    void processNextCommand();

private:
    void setState(ConnectionState state);
    void initializeELM();
    void parseResponse(const QString &response);
    QByteArray parseHexResponse(const QString &response);

    void writeToDevice(const QByteArray &data);
    void connectSignals();

    // Ortak IO cihazi (TCP veya BT)
    QIODevice *m_io = nullptr;         // aktif IO (m_tcpSocket veya m_btSocket)
    QTcpSocket *m_tcpSocket = nullptr;

#if HAS_BLUETOOTH
    QBluetoothSocket *m_btSocket = nullptr;
    QBluetoothDeviceDiscoveryAgent *m_btAgent = nullptr;
    QString m_btAddress;

    // BLE GATT (iOS + Android BLE devices)
    QLowEnergyController *m_bleController = nullptr;
    QLowEnergyService *m_bleService = nullptr;
    QLowEnergyCharacteristic m_bleWriteChar;
    QLowEnergyCharacteristic m_bleNotifyChar;
    bool m_useBLE = false;  // true = BLE GATT, false = RFCOMM
    QBluetoothDeviceInfo m_bleDeviceInfo;
    void connectBLE(const QBluetoothDeviceInfo &info);
    void setupBLEService(QLowEnergyService *service);
#endif

    Transport m_transport = Transport::WiFi;

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
    int m_defaultTimeoutMs = 1500;
};
