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
    // 1) Reset
    sendCommand("ATZ", [this](const QString &resp) {
        m_elmVersion = resp;
        emit logMessage("ELM327 Versiyon: " + resp);
    }, 5000);

    // 2) Echo kapalı
    sendCommand("ATE0", nullptr);

    // 3) Linefeed kapalı
    sendCommand("ATL0", nullptr);

    // 4) Boşlukları kapat (daha kolay parse)
    sendCommand("ATS0", nullptr);

    // 5) Header'ları göster (diagnostik için önemli)
    sendCommand("ATH1", nullptr);

    // 6) Adaptive timing
    sendCommand("ATAT2", nullptr);

    // 7) Voltaj oku
    sendCommand("ATRV", [this](const QString &resp) {
        m_elmVoltage = resp;
        emit logMessage("Akü voltajı: " + resp);
    });

    // 8) WJ 2.7 CRD TCM için: K-Line ISO 9141-2 (Protocol 3)
    sendCommand(QString("ATSP%1").arg(static_cast<int>(m_protocol)),
                [this](const QString &resp) {
        if (resp.contains("OK")) {
            emit logMessage("Protokol ISO 9141-2 (K-Line) ayarlandı");
        }
    });

    // 9) TCM adresi ayarla - WJ TCM receive address
    // NAG1 TCM tipik olarak 0x02 veya Mercedes EGS adresi kullanır
    // WJ 2.7 CRD TCM K-Line hedef adresi
    sendCommand("ATSH8110F1", [this](const QString &resp) {
        // 81 = ISO format byte (1 byte header, no length)
        // 10 = TCM/EGS hedef adresi
        // F1 = Tester adresi (scan tool)
        if (resp.contains("OK")) {
            emit logMessage("TCM header ayarlandı: 81 10 F1");
        }
    });

    // 10) Slow init - K-Line 5-baud init for TCM
    sendCommand("ATIIA10", [this](const QString &resp) {
        Q_UNUSED(resp)
        emit logMessage("Init adresi TCM (0x10) olarak ayarlandı");
    });

    // 11) Hazır sinyali
    sendCommand("ATI", [this](const QString &resp) {
        Q_UNUSED(resp)
        setState(ConnectionState::Ready);
        emit connected();
        emit logMessage("ELM327 hazır - TCM iletişimi başlayabilir");
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
