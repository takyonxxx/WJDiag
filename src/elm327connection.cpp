#include "elm327connection.h"
#include <QDebug>
#include <QRegularExpression>

ELM327Connection::ELM327Connection(QObject *parent)
    : QObject(parent)
{
    m_socket = new QTcpSocket(this);
    m_timeoutTimer = new QTimer(this);
    m_processTimer = new QTimer(this);

    m_timeoutTimer->setSingleShot(true);
    m_processTimer->setInterval(50);

    connect(m_socket, &QTcpSocket::connected,
            this, &ELM327Connection::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &ELM327Connection::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred,
            this, &ELM327Connection::onSocketError);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &ELM327Connection::onDataReady);
    connect(m_timeoutTimer, &QTimer::timeout,
            this, &ELM327Connection::onCommandTimeout);
    connect(m_processTimer, &QTimer::timeout,
            this, &ELM327Connection::processNextCommand);
}

ELM327Connection::~ELM327Connection()
{
    disconnect();
}

void ELM327Connection::connectToDevice(const QString &host, quint16 port)
{
    if (m_state != ConnectionState::Disconnected) {
        disconnect();
    }

    setState(ConnectionState::Connecting);
    emit logMessage(QString("ELM327'ye bağlanılıyor: %1:%2").arg(host).arg(port));
    m_socket->connectToHost(host, port);
}

void ELM327Connection::disconnect()
{
    m_timeoutTimer->stop();
    m_processTimer->stop();
    m_commandQueue.clear();
    m_commandPending = false;
    m_responseBuffer.clear();

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
    setState(ConnectionState::Disconnected);
}

bool ELM327Connection::isConnected() const
{
    return m_state == ConnectionState::Ready || m_state == ConnectionState::Busy;
}

void ELM327Connection::setProtocol(Protocol proto)
{
    m_protocol = proto;
    if (isConnected()) {
        sendCommand(QString("ATSP%1").arg(static_cast<int>(proto)),
                    [this, proto](const QString &resp) {
            if (resp.contains("OK")) {
                emit logMessage(QString("Protokol ayarlandı: %1").arg(static_cast<int>(proto)));
            } else {
                emit errorOccurred("Protokol ayarlanamadı: " + resp);
            }
        });
    }
}

void ELM327Connection::sendCommand(const QString &cmd,
                                    std::function<void(const QString&)> callback,
                                    int timeoutMs)
{
    ATCommand atCmd;
    atCmd.command = cmd;
    atCmd.callback = callback;
    atCmd.timeoutMs = timeoutMs;

    m_commandQueue.enqueue(atCmd);

    if (!m_commandPending) {
        processNextCommand();
    }
}

void ELM327Connection::sendOBDCommand(const QByteArray &hexCmd,
                                       std::function<void(const QByteArray&)> callback,
                                       int timeoutMs)
{
    // ELM327'ye hex string olarak gönder
    QString cmdStr = QString::fromLatin1(hexCmd).trimmed();

    sendCommand(cmdStr, [this, callback](const QString &resp) {
        if (callback) {
            QByteArray parsed = parseHexResponse(resp);
            callback(parsed);
        }
    }, timeoutMs);
}

// --- Private Slots ---

void ELM327Connection::onSocketConnected()
{
    emit logMessage("TCP bağlantısı kuruldu, ELM327 başlatılıyor...");
    setState(ConnectionState::Initializing);
    initializeELM();
}

void ELM327Connection::onSocketDisconnected()
{
    emit logMessage("Bağlantı kesildi");
    setState(ConnectionState::Disconnected);
    emit disconnected();
}

void ELM327Connection::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    setState(ConnectionState::Error);
    emit errorOccurred("Soket hatası: " + m_socket->errorString());
}

void ELM327Connection::onDataReady()
{
    QByteArray data = m_socket->readAll();
    m_responseBuffer += QString::fromLatin1(data);

    emit rawDataReceived(data);

    // ELM327 yanıtı '>' prompt ile biter
    if (m_responseBuffer.contains('>')) {
        m_timeoutTimer->stop();

        QString response = m_responseBuffer.trimmed();
        response.remove('>');
        response = response.trimmed();
        m_responseBuffer.clear();

        emit logMessage(QString("← %1").arg(response));

        if (m_commandPending && m_currentCommand.callback) {
            m_currentCommand.callback(response);
        }

        m_commandPending = false;

        // Kuyrukta bir sonraki komutu işle
        QTimer::singleShot(30, this, &ELM327Connection::processNextCommand);
    }
}

void ELM327Connection::onCommandTimeout()
{
    emit logMessage("Komut zaman aşımı!");

    if (m_commandPending && m_currentCommand.callback) {
        m_currentCommand.callback("TIMEOUT");
    }

    m_commandPending = false;
    m_responseBuffer.clear();

    // Timeout sonrası buffer temizle
    if (m_socket->bytesAvailable() > 0) {
        m_socket->readAll();
    }

    processNextCommand();
}

void ELM327Connection::processNextCommand()
{
    if (m_commandPending || m_commandQueue.isEmpty()) return;

    m_currentCommand = m_commandQueue.dequeue();
    m_commandPending = true;
    m_responseBuffer.clear();

    QString cmdWithCR = m_currentCommand.command + "\r";
    emit logMessage(QString("→ %1").arg(m_currentCommand.command));

    m_socket->write(cmdWithCR.toLatin1());
    m_socket->flush();

    m_timeoutTimer->start(m_currentCommand.timeoutMs);
}

// --- Private Methods ---

void ELM327Connection::setState(ConnectionState state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged(state);
    }
}

void ELM327Connection::initializeELM()
{
    m_genuineELM = true;

    // 1) Reset
    sendCommand("ATZ", [this](const QString &resp) {
        m_elmVersion = resp;
        emit logMessage("ELM327 Version: " + resp);
        if (resp.contains("OBDII") || resp.contains("vLinker") ||
            resp.toLower().contains("clone")) {
            m_genuineELM = false;
            emit logMessage("WARNING: Non-standard ELM327 detected");
        }
    }, 5000);

    // 2) Echo off
    sendCommand("ATE0", nullptr);

    // 3) Linefeed off
    sendCommand("ATL0", nullptr);

    // 4) Spaces off
    sendCommand("ATS0", nullptr);

    // 5) Headers on
    sendCommand("ATH1", nullptr);

    // 6) Adaptive timing auto2
    sendCommand("ATAT2", nullptr);

    // 7) Timeout 400ms (0x64 * 4ms)
    sendCommand("ATST64", nullptr);

    // 8) Battery voltage
    sendCommand("ATRV", [this](const QString &resp) {
        m_elmVoltage = resp;
        emit logMessage("Battery voltage: " + resp);
    });

    // 9) Protocol: KWP fast init (ATSP5) - jeepswj.com uses this
    sendCommand("ATSP5", [this](const QString &resp) {
        if (resp.contains("OK")) {
            emit logMessage("Protocol: ISO 14230-4 KWP fast init (ATSP5)");
            m_protocol = Protocol::KWP_FAST;
        } else {
            emit logMessage("ATSP5 failed, will fallback to ATSP3");
        }
    });

    // 10) Test ATFI (fast init) - genuine ELM327 only
    sendCommand("ATFI", [this](const QString &resp) {
        if (resp.contains("?") || resp.contains("ERROR")) {
            m_genuineELM = false;
            emit logMessage("ATFI not supported - fake/clone ELM327");
            m_protocol = Protocol::ISO_9141;
        } else {
            emit logMessage("ATFI OK - genuine ELM327");
        }
    });

    // 11) Describe protocol + fallback if needed
    sendCommand("ATDP", [this](const QString &resp) {
        emit logMessage("Active protocol: " + resp);
        if (m_protocol == Protocol::ISO_9141) {
            sendCommand("ATSP3", [this](const QString &r) {
                if (r.contains("OK"))
                    emit logMessage("Fallback: ISO 9141-2 (ATSP3)");
            });
        }
    });

    // 12) Wakeup message: TesterPresent to TCM (keep-alive)
    // ATWM 81 10 F1 3E = TesterPresent(3E) to TCM(10) from F1
    sendCommand("ATWM8110F13E", [this](const QString &resp) {
        if (resp.contains("OK")) {
            emit logMessage("Wakeup message: TesterPresent to TCM");
        } else if (resp.contains("?")) {
            m_genuineELM = false;
            emit logMessage("ATWM not supported - fake/clone ELM327");
        }
    });

    // 13) TCM header: 81 10 F1
    sendCommand("ATSH8110F1", [this](const QString &resp) {
        if (resp.contains("OK"))
            emit logMessage("TCM header: 81 10 F1");
    });

    // 14) 5-baud init address for TCM
    sendCommand("ATIIA10", [this](const QString &resp) {
        Q_UNUSED(resp)
        emit logMessage("Init address: 0x10 (TCM)");
    });

    // 15) Ready
    sendCommand("ATI", [this](const QString &resp) {
        Q_UNUSED(resp)
        setState(ConnectionState::Ready);
        emit connected();
        if (m_genuineELM) {
            emit logMessage("ELM327 ready - genuine chip, full feature support");
        } else {
            emit logMessage("ELM327 ready - WARNING: clone/fake, limited features");
            emit fakeELMDetected("ATFI or ATWM not supported");
        }
    });
}

QByteArray ELM327Connection::parseHexResponse(const QString &response)
{
    QByteArray result;

    // "NO DATA", "ERROR", "UNABLE TO CONNECT" gibi hata yanıtlarını kontrol et
    if (response.contains("NO DATA") ||
        response.contains("ERROR") ||
        response.contains("UNABLE") ||
        response.contains("TIMEOUT") ||
        response.contains("?")) {
        return result;
    }

    // Yanıttan hex baytları çıkar
    // Header'ları atla (ilk 3 byte genellikle header: format, target, source)
    // Yanıt formatı: "83 F1 10 XX YY ZZ ..." şeklinde olabilir
    static QRegularExpression hexRegex("[0-9A-Fa-f]{2}");

    QRegularExpressionMatchIterator it = hexRegex.globalMatch(response);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        bool ok;
        uint8_t byte = match.captured().toUInt(&ok, 16);
        if (ok) {
            result.append(static_cast<char>(byte));
        }
    }

    return result;
}
