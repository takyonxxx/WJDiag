#include "elm327connection.h"
#include <QDebug>
#include <QRegularExpression>

ELM327Connection::ELM327Connection(QObject *parent)
    : QObject(parent)
{
    m_tcpSocket = new QTcpSocket(this);
    m_timeoutTimer = new QTimer(this);
    m_processTimer = new QTimer(this);

    m_timeoutTimer->setSingleShot(true);
    m_processTimer->setInterval(50);

    connect(m_timeoutTimer, &QTimer::timeout,
            this, &ELM327Connection::onCommandTimeout);
    connect(m_processTimer, &QTimer::timeout,
            this, &ELM327Connection::processNextCommand);

#if HAS_BLUETOOTH
    m_btSocket = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol, this);
    m_bleController = nullptr;
    m_bleService = nullptr;
    m_useBLE = false;
    m_btAgent = new QBluetoothDeviceDiscoveryAgent(this);
    m_btAgent->setLowEnergyDiscoveryTimeout(10000);

    connect(m_btAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, [this](const QBluetoothDeviceInfo &info) {
        QString name = info.name().trimmed();
        // iOS: address() is empty, use deviceUuid() instead
        QString addr = info.address().toString();
        if (addr.isEmpty() || addr == "00:00:00:00:00:00")
            addr = info.deviceUuid().toString(QUuid::WithoutBraces);
        bool isBLE = (info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration);
        QString type = "BLE";



        if (name.isEmpty())
            return;

        // Relaxed filter: OBD, ELM, vLink, IOS-V, OBDII, V-LINK, Veepeak, Carista, LELink
        bool match = name.contains("OBD", Qt::CaseInsensitive) ||
                     name.contains("ELM", Qt::CaseInsensitive) ||
                     name.contains("vLink", Qt::CaseInsensitive) ||
                     name.contains("V-LINK", Qt::CaseInsensitive) ||
                     name.contains("IOS-V", Qt::CaseInsensitive) ||
                     name.contains("Veepeak", Qt::CaseInsensitive) ||
                     name.contains("Carista", Qt::CaseInsensitive) ||
                     name.contains("LELink", Qt::CaseInsensitive) ||
                     name.contains("iCar", Qt::CaseInsensitive) ||
                     name.contains("Viecar", Qt::CaseInsensitive) ||
                     name.contains("Konnwei", Qt::CaseInsensitive);


        if (match) {
            m_bleDeviceInfo = info;  // Save for BLE GATT connection
            emit logMessage(QString("BT device found: %1 [%2] (%3)").arg(name, addr, type));
            emit bluetoothDeviceFound(name, addr);
            m_btAgent->stop();
            if (m_state == ConnectionState::Scanning)
                setState(ConnectionState::Disconnected);
            emit bluetoothScanFinished();
        }
    });
    connect(m_btAgent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, [this]() {
        emit bluetoothScanFinished();
        if (m_state == ConnectionState::Scanning)
            setState(ConnectionState::Disconnected);
    });
    connect(m_btAgent, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            this, [this](QBluetoothDeviceDiscoveryAgent::Error err) {
        Q_UNUSED(err)
        emit logMessage("BT scan error: " + m_btAgent->errorString());
        if (m_state == ConnectionState::Scanning)
            setState(ConnectionState::Disconnected);
    });
#endif
}

ELM327Connection::~ELM327Connection()
{
    disconnect();
}

// === WiFi (TCP) Baglanti ===

void ELM327Connection::connectToDevice(const QString &host, quint16 port)
{
    if (m_state != ConnectionState::Disconnected)
        disconnect();

    m_transport = Transport::WiFi;
    m_io = m_tcpSocket;
    connectSignals();

    setState(ConnectionState::Connecting);
    emit logMessage(QString("WiFi ELM327: %1:%2").arg(host).arg(port));
    m_tcpSocket->connectToHost(host, port);
}

// === Bluetooth (RFCOMM/SPP) Baglanti ===

void ELM327Connection::scanBluetooth()
{
#if HAS_BLUETOOTH

    if (m_btAgent->isActive())
        m_btAgent->stop();
    setState(ConnectionState::Scanning);
    m_btAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);

#else
    emit logMessage("ERROR: Bluetooth not available on this platform");
#endif
}

void ELM327Connection::stopScan()
{
#if HAS_BLUETOOTH
    if (m_btAgent->isActive())
        m_btAgent->stop();
    if (m_state == ConnectionState::Scanning)
        setState(ConnectionState::Disconnected);
#endif
}

void ELM327Connection::connectBluetooth(const QString &address)
{
#if HAS_BLUETOOTH
    if (m_state != ConnectionState::Disconnected && m_state != ConnectionState::Scanning)
        disconnect();

    stopScan();
    m_transport = Transport::Bluetooth;
    m_btAddress = address;
    setState(ConnectionState::Connecting);

    // Always use BLE GATT - works on both iOS and Android
    if (m_bleDeviceInfo.isValid()) {
        m_useBLE = true;
        connectBLE(m_bleDeviceInfo);
    } else {
        // Fallback: try classic RFCOMM (Android only)
        m_useBLE = false;
        m_io = m_btSocket;
        connectSignals();
        static const QBluetoothUuid sppUuid(QStringLiteral("00001101-0000-1000-8000-00805F9B34FB"));
        QBluetoothAddress btAddr(address);
        if (!btAddr.isNull()) {
            m_btSocket->connectToService(btAddr, sppUuid);
        } else {
            emit logMessage("ERROR: No valid BT address or device info");
            setState(ConnectionState::Error);
        }
    }
#else
    Q_UNUSED(address)
    emit logMessage("ERROR: Bluetooth not available on this platform");
#endif
}

// === Ortak: Disconnect ===

void ELM327Connection::disconnect()
{
    m_timeoutTimer->stop();
    m_processTimer->stop();
    m_commandQueue.clear();
    m_commandPending = false;
    m_responseBuffer.clear();

    if (m_transport == Transport::WiFi) {
        if (m_tcpSocket->state() != QAbstractSocket::UnconnectedState)
            m_tcpSocket->disconnectFromHost();
    }
#if HAS_BLUETOOTH
    else if (m_transport == Transport::Bluetooth) {
        if (m_useBLE) {
            if (m_bleService) { delete m_bleService; m_bleService = nullptr; }
            if (m_bleController) { m_bleController->disconnectFromDevice(); delete m_bleController; m_bleController = nullptr; }
        } else {
            if (m_btSocket->state() != QBluetoothSocket::SocketState::UnconnectedState)
                m_btSocket->close();
        }
    }
#endif
    setState(ConnectionState::Disconnected);
}

bool ELM327Connection::isConnected() const
{
    return m_state == ConnectionState::Ready || m_state == ConnectionState::Busy;
}

// === Signal Baglama (IO cihazina gore) ===

void ELM327Connection::connectSignals()
{
    // Onceki sinyalleri temizle
    if (m_tcpSocket) {
        QObject::disconnect(m_tcpSocket, nullptr, this, nullptr);
    }
#if HAS_BLUETOOTH
    if (m_btSocket) {
        QObject::disconnect(m_btSocket, nullptr, this, nullptr);
    }
#endif

    // Timeout ve processTimer sinyalleri zaten constructor'da bagli

    if (m_transport == Transport::WiFi) {
        connect(m_tcpSocket, &QTcpSocket::connected,
                this, &ELM327Connection::onSocketConnected);
        connect(m_tcpSocket, &QTcpSocket::disconnected,
                this, &ELM327Connection::onSocketDisconnected);
        connect(m_tcpSocket, &QTcpSocket::errorOccurred,
                this, [this](QAbstractSocket::SocketError) {
            setState(ConnectionState::Error);
            emit errorOccurred("WiFi hata: " + m_tcpSocket->errorString());
        });
        connect(m_tcpSocket, &QTcpSocket::readyRead,
                this, &ELM327Connection::onDataReady);
    }
#if HAS_BLUETOOTH
    else if (m_transport == Transport::Bluetooth) {
        connect(m_btSocket, &QBluetoothSocket::connected,
                this, &ELM327Connection::onSocketConnected);
        connect(m_btSocket, &QBluetoothSocket::disconnected,
                this, &ELM327Connection::onSocketDisconnected);
        connect(m_btSocket, &QBluetoothSocket::errorOccurred,
                this, [this](QBluetoothSocket::SocketError) {
            setState(ConnectionState::Error);
            emit errorOccurred("BT hata: " + m_btSocket->errorString());
        });
        connect(m_btSocket, &QBluetoothSocket::readyRead,
                this, &ELM327Connection::onDataReady);
    }
#endif
}

// === Ortak IO Yazma ===

void ELM327Connection::writeToDevice(const QByteArray &data)
{
    // BLE GATT write - m_io is null for BLE, handle separately
    if (m_transport == Transport::Bluetooth && m_useBLE && m_bleService && m_bleWriteChar.isValid()) {
        QLowEnergyService::WriteMode mode = QLowEnergyService::WriteWithoutResponse;
        if (m_bleWriteChar.properties() & QLowEnergyCharacteristic::Write)
            mode = QLowEnergyService::WriteWithResponse;
        if (m_bleWriteChar.properties() & QLowEnergyCharacteristic::WriteNoResponse)
            mode = QLowEnergyService::WriteWithoutResponse;
        m_bleService->writeCharacteristic(m_bleWriteChar, data, mode);
        return;
    }

    if (m_io && m_io->isOpen()) {
        m_io->write(data);
            m_tcpSocket->flush();
    }
}

// === Protokol ===

void ELM327Connection::setProtocol(Protocol proto)
{
    m_protocol = proto;
    if (isConnected()) {
        sendCommand(QString("ATSP%1").arg(static_cast<int>(proto)),
                    [this, proto](const QString &resp) {
            if (resp.contains("OK"))
                emit logMessage(QString("Protokol: %1").arg(static_cast<int>(proto)));
            else
                emit errorOccurred("Protocol error: " + resp);
        });
    }
}

// === Komut Gonderme ===

void ELM327Connection::sendCommand(const QString &cmd,
                                    std::function<void(const QString&)> callback,
                                    int timeoutMs)
{
    ATCommand atCmd;
    atCmd.command = cmd;
    atCmd.callback = callback;
    atCmd.timeoutMs = (timeoutMs > 0) ? timeoutMs : m_defaultTimeoutMs;

    m_commandQueue.enqueue(atCmd);

    if (!m_commandPending)
        processNextCommand();
}

void ELM327Connection::sendOBDCommand(const QByteArray &hexCmd,
                                       std::function<void(const QByteArray&)> callback,
                                       int timeoutMs)
{
    QString cmdStr = QString::fromLatin1(hexCmd).trimmed();
    sendCommand(cmdStr, [this, callback](const QString &resp) {
        if (callback) {
            QByteArray parsed = parseHexResponse(resp);
            callback(parsed);
        }
    }, (timeoutMs > 0) ? timeoutMs : m_defaultTimeoutMs);
}

// === Slots ===

void ELM327Connection::onSocketConnected()
{
    QString type = (m_transport == Transport::WiFi) ? "WiFi TCP" : "Bluetooth SPP";
    emit logMessage(QString("%1 connection established, initializing ELM327...").arg(type));
    setState(ConnectionState::Initializing);
    initializeELM();
}

void ELM327Connection::onSocketDisconnected()
{
    emit logMessage("Disconnected");
    setState(ConnectionState::Disconnected);
    emit disconnected();
}

void ELM327Connection::onDataReady()
{
    QByteArray data;
    if (m_io)
        data = m_io->readAll();

    m_responseBuffer += QString::fromLatin1(data);
    emit rawDataReceived(data);

    // ELM327 yaniti '>' prompt ile biter
    if (m_responseBuffer.contains('>')) {
        m_timeoutTimer->stop();

        QString response = m_responseBuffer.trimmed();
        response.remove('>');
        response = response.trimmed();
        m_responseBuffer.clear();

        // Strip echo: ATE1 is on, first line is the sent command echo
        // Response format: "COMMAND\r\nACTUAL_RESPONSE"
        // Remove first line if it matches the sent command
        if (!m_currentCommand.command.isEmpty()) {
            QStringList lines = response.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
            if (lines.size() > 1 && lines.first().trimmed().compare(
                    m_currentCommand.command.trimmed(), Qt::CaseInsensitive) == 0) {
                lines.removeFirst();
            }
            response = lines.join("\n").trimmed();
        }

        // Clean non-printable / non-ASCII garbage (clone ELM327 ATZ junk bytes)
        QString cleaned;
        cleaned.reserve(response.size());
        for (const QChar &ch : response) {
            ushort u = ch.unicode();
            if (u == '\n' || (u >= 0x20 && u <= 0x7E))
                cleaned.append(ch);
        }
        // Remove resulting empty lines
        cleaned = cleaned.split('\n', Qt::SkipEmptyParts).join("\n").trimmed();
        if (!cleaned.isEmpty())
            response = cleaned;

        emit logMessage(QString::fromUtf8("\xe2\x86\x90 %1").arg(response));

        if (m_commandPending && m_currentCommand.callback)
            m_currentCommand.callback(response);

        m_commandPending = false;
        QTimer::singleShot(340, this, &ELM327Connection::processNextCommand);
    }
}

void ELM327Connection::onCommandTimeout()
{
    emit logMessage("Command timeout!");

    if (m_commandPending && m_currentCommand.callback)
        m_currentCommand.callback("TIMEOUT");

    m_commandPending = false;
    m_responseBuffer.clear();

    // Timeout sonrasi buffer temizle
    if (m_io && m_io->bytesAvailable() > 0)
        m_io->readAll();

    processNextCommand();
}

void ELM327Connection::processNextCommand()
{
    if (m_commandPending || m_commandQueue.isEmpty()) return;

    m_currentCommand = m_commandQueue.dequeue();
    m_commandPending = true;
    m_responseBuffer.clear();

    QString cmdWithCR = m_currentCommand.command + "\r";
    emit logMessage(QString::fromUtf8("\xe2\x86\x92 %1").arg(m_currentCommand.command));

    writeToDevice(cmdWithCR.toLatin1());
    m_timeoutTimer->start(m_currentCommand.timeoutMs);
}

// === Init ===

void ELM327Connection::setState(ConnectionState state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged(state);
    }
}

void ELM327Connection::initializeELM()
{
    // === APK uyumlu init sirasi ===

    // 1) Reset
    sendCommand("ATZ", [this](const QString &resp) {
        m_elmVersion = resp;
        emit logMessage("ELM327 Version: " + resp);
    }, 5000);

    // 2) Echo on (APK: ATE1)
    sendCommand("ATE1", nullptr);

    // 3) Headers on (APK: ATH1)
    sendCommand("ATH1", nullptr);

    // 4) Disable IFR for J1850 VPW (APK: ATIFR0)
    sendCommand("ATIFR0", [this](const QString &resp) {
        if (resp.contains("?"))
            emit logMessage("ATIFR0 not supported (not critical)");
    });

    // 5) Default: J1850 VPW (APK: ATSP2)
    sendCommand("ATSP2", [this](const QString &resp) {
        if (resp.contains("OK")) {
            emit logMessage("Protocol: SAE J1850 VPW (ATSP2)");
            m_protocol = Protocol::J1850_VPW;
        }
        setState(ConnectionState::Ready);
        emit connected();
        QString type = (m_transport == Transport::WiFi) ? "WiFi" : "Bluetooth";
        emit logMessage(QString("ELM327 ready [%1]").arg(type));
    });
}

// === Parse ===

QByteArray ELM327Connection::parseHexResponse(const QString &response)
{
    QByteArray result;

    if (response.contains("NO DATA") ||
        response.contains("ERROR") ||
        response.contains("UNABLE") ||
        response.contains("TIMEOUT") ||
        response.contains("?")) {
        return result;
    }

    static QRegularExpression hexRegex("[0-9A-Fa-f]{2}");
    QRegularExpressionMatchIterator it = hexRegex.globalMatch(response);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        bool ok;
        uint8_t byte = match.captured().toUInt(&ok, 16);
        if (ok)
            result.append(static_cast<char>(byte));
    }

    return result;
}

// === BLE GATT Connection (iOS + Android BLE) ===

#if HAS_BLUETOOTH
void ELM327Connection::connectBLE(const QBluetoothDeviceInfo &info)
{

    if (m_bleController) {
        delete m_bleController;
        m_bleController = nullptr;
    }

    m_bleController = QLowEnergyController::createCentral(info, this);

    connect(m_bleController, &QLowEnergyController::connected, this, [this]() {
        m_bleController->discoverServices();
    });

    connect(m_bleController, &QLowEnergyController::disconnected, this, [this]() {
        emit logMessage("BLE disconnected");
        setState(ConnectionState::Disconnected);
        emit disconnected();
    });

    connect(m_bleController, &QLowEnergyController::errorOccurred, this,
            [this](QLowEnergyController::Error err) {
        emit logMessage(QString("BLE error: %1").arg(static_cast<int>(err)));
        setState(ConnectionState::Error);
    });

    connect(m_bleController, &QLowEnergyController::serviceDiscovered, this,
            [this](const QBluetoothUuid &uuid) {
        Q_UNUSED(uuid);
    });

    connect(m_bleController, &QLowEnergyController::discoveryFinished, this, [this]() {
        // ELM327 BLE: common service UUIDs
        // FFF0 (most clones), FFE0 (some), 18F0 (OBDLink), E7810A71... (custom)
        QList<QBluetoothUuid> tryServices = {
            QBluetoothUuid(QStringLiteral("0000fff0-0000-1000-8000-00805f9b34fb")),
            QBluetoothUuid(QStringLiteral("0000ffe0-0000-1000-8000-00805f9b34fb")),
            QBluetoothUuid(QStringLiteral("000018f0-0000-1000-8000-00805f9b34fb")),
        };

        QLowEnergyService *svc = nullptr;
        for (const auto &suuid : tryServices) {
            auto services = m_bleController->services();
            if (services.contains(suuid)) {
                svc = m_bleController->createServiceObject(suuid, this);
                if (svc) {
                    break;
                }
            }
        }

        if (!svc) {
            // Try first available service
            auto services = m_bleController->services();
            for (const auto &suuid : services) {
                svc = m_bleController->createServiceObject(suuid, this);
                if (svc) {
                    break;
                }
            }
        }

        if (!svc) {
            emit logMessage("BLE: No suitable service found!");
            setState(ConnectionState::Error);
            return;
        }

        setupBLEService(svc);
    });

    m_bleController->connectToDevice();
}

void ELM327Connection::setupBLEService(QLowEnergyService *service)
{
    m_bleService = service;

    connect(m_bleService, &QLowEnergyService::stateChanged, this,
            [this](QLowEnergyService::ServiceState state) {
        if (state == QLowEnergyService::RemoteServiceDiscovered) {
            // Find write + notify characteristics
            // ELM327 BLE: FFF1=write, FFF2=notify (most common)
            // or FFE1=write+notify (some)
            auto chars = m_bleService->characteristics();
            for (const auto &ch : chars) {
                // char found

                if (ch.properties() & QLowEnergyCharacteristic::Write ||
                    ch.properties() & QLowEnergyCharacteristic::WriteNoResponse) {
                    m_bleWriteChar = ch;
                }
                if (ch.properties() & QLowEnergyCharacteristic::Notify) {
                    m_bleNotifyChar = ch;
                    // Enable notifications via CCCD descriptor
                    auto desc = ch.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
                    if (desc.isValid()) {

                        m_bleService->writeDescriptor(desc, QByteArray::fromHex("0100"));
                    } else {
                        emit logMessage("BLE WARNING: No CCCD descriptor for notify!");
                    }
                }
            }

            if (m_bleWriteChar.isValid() && m_bleNotifyChar.isValid()) {
                setState(ConnectionState::Initializing);
                emit logMessage("Bluetooth BLE connection established, initializing ELM327...");
                initializeELM();
            } else {
                emit logMessage("BLE: Missing write or notify characteristic!");
                setState(ConnectionState::Error);
            }
        }
    });

    connect(m_bleService, &QLowEnergyService::characteristicChanged, this,
            [this](const QLowEnergyCharacteristic &ch, const QByteArray &data) {
        Q_UNUSED(ch)
        // BLE data received
        m_responseBuffer += QString::fromLatin1(data);
        emit rawDataReceived(data);
        // Check for ELM327 prompt '>'
        if (m_responseBuffer.contains('>')) {
            // Trigger same processing as onDataReady via timer
            QTimer::singleShot(0, this, &ELM327Connection::onDataReady);
        }
    });

    connect(m_bleService, &QLowEnergyService::descriptorWritten, this,
            [this](const QLowEnergyDescriptor &desc, const QByteArray &value) {
        Q_UNUSED(desc)
        Q_UNUSED(value)
    });

    m_bleService->discoverDetails();
}
#endif
