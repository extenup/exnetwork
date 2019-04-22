#include "exserver.h"
#include <QJsonDocument>
#include <QDateTime>

void ExServer::deleteSocket(QTcpSocket *socket)
{
    removeSocketFromConnections(socket);

    socket->disconnect();
    socket->deleteLater();
}

void ExServer::processMessage(QTcpSocket *socket, const QJsonObject &message)
{
    if (message.contains("exnetwork_ping"))
    {
        ping(socket, message);
    }
    else
    {
        readMessage(socket, message);
    }
}

void ExServer::ping(QTcpSocket *socket, const QJsonObject &message)
{
    mLastActivities[socket] = QDateTime::currentDateTime().toUTC().toSecsSinceEpoch();
    sendMessage(socket, message);
}

void ExServer::onSocketReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    QByteArray rawMessage = socket->readAll();
    if (rawMessage.mid(0, 3) == "GET")
    {
        readHttp(socket, rawMessage);
    }
    else
    {
        mBuffers[socket] += rawMessage;
        QList<QByteArray> list = mBuffers[socket].split('\n');
        if (list.size() > 1)
        {
            for (int i = 0; i < list.size() - 1; i++)
            {
                QJsonObject message = QJsonDocument::fromJson(list[i]).object();
                if (!message.isEmpty())
                {
                    processMessage(socket, message);
                }
                else
                {
                    qDebug() << QString("Wrong message format %0").arg(QString(list[i]));
                }
            }
            mBuffers[socket] = list.last();
        }
    }
}

void ExServer::onSocketDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    deleteSocket(socket);
}

void ExServer::onSocketError(QAbstractSocket::SocketError error)
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (error != QAbstractSocket::RemoteHostClosedError)
    {
        qDebug() << "Socket ERROR number: " << error;
        qDebug() << "Socket ERROR string: " << socket->errorString();
    }
    deleteSocket(socket);
}

void ExServer::onServerNewConnection()
{
    QTcpSocket *socket = mServer.nextPendingConnection();
    mLastActivities[socket] = QDateTime::currentDateTime().toUTC().toSecsSinceEpoch();

    connect(socket, SIGNAL(disconnected()),
            this, SLOT(onSocketDisconnected()));

    connect(socket, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(onSocketError(QAbstractSocket::SocketError)));

    connect(socket, SIGNAL(readyRead()),
            this, SLOT(onSocketReadyRead()));
}

void ExServer::onPingTimerTimeout()
{
    qint64 currentDateTime = QDateTime::currentDateTime().toUTC().toSecsSinceEpoch();
    for (QTcpSocket *key : mLastActivities.keys())
    {
        if (currentDateTime - mLastActivities[key] > mPingTimeoutSecs)
        {
            key->disconnectFromHost();
        }
    }
}

ExConnection *ExServer::addSocketToConnection(const QString &id, QTcpSocket *socket)
{
    removeSocketFromConnections(socket);

    ExConnection *connection = getConnection(id);
    if (connection == nullptr)
    {
        ExConnection c;
        c.id = id;
        c.sockets.push_back(socket);
        mConnections.push_back(c);
        connection = &mConnections.last();
    }
    else
    {
        if (!connection->sockets.contains(socket))
        {
            connection->sockets.push_back(socket);
        }
    }

    connectionAddedEvent(connection->id);
    return connection;
}

void ExServer::removeSocketFromConnections(QTcpSocket *socket)
{
    for (int i = 0; i < mConnections.size(); i++)
    {
        ExConnection &connection = mConnections[i];
        int isocket = connection.sockets.indexOf(socket);
        if (isocket != -1)
        {
            connection.sockets.remove(isocket);
            if (connection.sockets.size() == 0)
            {
                connectionRemovedEvent(connection.id);
                mConnections.remove(i);
            }
        }
    }
    mBuffers.remove(socket);
    mLastActivities.remove(socket);
}

ExConnection *ExServer::getConnection(const QString &id)
{
    QVector<ExConnection>::iterator it = std::find(mConnections.begin(), mConnections.end(), id);
    if (it != mConnections.end())
    {
        return &(*it);
    }
    else
    {
        return nullptr;
    }
}

ExConnection *ExServer::getConnection(const QTcpSocket *socket)
{
    QVector<ExConnection>::iterator connIt = std::find(mConnections.begin(), mConnections.end(), socket);
    if (connIt != mConnections.end())
    {
        QVector<QTcpSocket *>::iterator sockIt =
                std::find(connIt->sockets.begin(), connIt->sockets.end(), socket);
        if (sockIt != connIt->sockets.end())
        {
            return &(*connIt);
        }
    }
    return nullptr;
}

void ExServer::sendMessage(QTcpSocket *socket, const QJsonObject &message)
{
    if (socket->state() == QTcpSocket::ConnectedState)
    {
        socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    }
}

void ExServer::sendMessage(ExConnection *connection, const QJsonObject &message)
{
    for (QTcpSocket *socket : connection->sockets)
    {
        sendMessage(socket, message);
    }
}

void ExServer::sendErrorMessage(QTcpSocket *socket, const QString &text)
{
    QJsonObject message;
    message["exnetwork_error"] = text;
    sendMessage(socket, message);
}

void ExServer::sendErrorMessage(ExConnection *connection, const QString &text)
{
    for (QTcpSocket *socket : connection->sockets)
    {
        sendErrorMessage(socket, text);
    }
}

void ExServer::readMessage(QTcpSocket *socket, const QJsonObject &message)
{
    Q_UNUSED(socket);
    Q_UNUSED(message);
}

void ExServer::readHttp(QTcpSocket *socket, const QString &http)
{
    Q_UNUSED(socket);
    Q_UNUSED(http);
}

ExServer::ExServer(quint16 port, QObject *parent) : QObject(parent)
{
    if (mServer.listen(QHostAddress::Any, port))
    {
        connect(&mServer, SIGNAL(newConnection()), this,
                SLOT(onServerNewConnection()));
    }
    else
    {
        qDebug() << "Server cannot listen";
    }
    connect(&mPingTimer, SIGNAL(timeout()),
            this, SLOT(onPingTimerTimeout()));
    mPingTimer.start(mPingIntervalSecs * 1000);
}

