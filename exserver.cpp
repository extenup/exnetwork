#include "exserver.h"
#include <QJsonDocument>
#include <QDateTime>

void ExServer::incomingConnection(qintptr socketDescriptor)
{
    QTcpSocket *soc = new QTcpSocket();
    if (soc->setSocketDescriptor(socketDescriptor))
    {
        QThread *thr = new QThread();
        soc->moveToThread(thr);

        connect(thr, &QThread::finished, soc, &QTcpSocket::deleteLater);
        connect(soc, &QTcpSocket::destroyed, thr, &QThread::deleteLater);

        SocketInfo socInf;
        socInf.lastActivity = QDateTime::currentSecsSinceEpoch();
        socInf.thread = thr;
        mConnectionsMutex.lock();
        mConnections[soc] = socInf;
        mConnectionsMutex.unlock();

        connect(soc, &QTcpSocket::readyRead, [this, soc]()
        {
            QList<QByteArray> msgs;

            mConnectionsMutex.lock();
            if (mConnections.contains(soc))
            {
                mConnections[soc].buffer += soc->readAll();
                msgs = mConnections[soc].buffer.split('\n');
                if (msgs.size() > 1)
                {
                    mConnections[soc].buffer = msgs.last();
                    msgs.pop_back();
                }
            }
            mConnectionsMutex.unlock();

            for (int i = 0; i < msgs.size(); i++)
            {
                QJsonObject msg = QJsonDocument::fromJson(msgs[i]).object();
                if (!msg.isEmpty())
                {
                    processMessage(soc, msg);
                }
            }
        });

        thr->start();
    }
    else
    {
        soc->deleteLater();
        qDebug() << "setSocketDescriptor ERROR" << Q_FUNC_INFO;
    }
}

void ExServer::processMessage(QTcpSocket *socket, QJsonObject &message)
{
    if (message.contains("exnetwork_ping"))
    {
        ping(socket, message);
    }
    readMessage(socket, message);
}

void ExServer::ping(QTcpSocket *socket, QJsonObject &message)
{
    mConnectionsMutex.lock();
    if (mConnections.contains(socket))
    {
        mConnections[socket].lastActivity = QDateTime::currentSecsSinceEpoch();
    }
    mConnectionsMutex.unlock();
    sendMessage(socket, message);
}

void ExServer::sendMessage2(QTcpSocket *socket, QJsonObject message)
{
    if (socket->state() == QTcpSocket::ConnectedState)
    {
        socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    }
}

void ExServer::sendErrorMessage2(QTcpSocket *socket, QString text)
{
    QJsonObject msg;
    msg["exnetwork_error"] = text;
    sendMessage2(socket, msg);
}

void ExServer::onPingTimerTimeout()
{
    mConnectionsMutex.lock();
    qint64 secs = QDateTime::currentSecsSinceEpoch();
    for (QTcpSocket *soc : mConnections.keys())
    {
        if (secs - mConnections[soc].lastActivity > mPingTimeoutSecs)
        {
            mConnections[soc].thread->quit();
            mConnections.remove(soc);
        }
    }
    mConnectionsMutex.unlock();
}

void ExServer::setConnectionId(QTcpSocket *socket, const QString &id)
{
    mConnectionsMutex.lock();
    if (mConnections.contains(socket))
    {
        mConnections[socket].id = id;
    }
    mConnectionsMutex.unlock();
}

QString ExServer::getConnectionId(QTcpSocket *socket)
{
    QString id;
    mConnectionsMutex.lock();
    if (mConnections.contains(socket))
    {
        id = mConnections[socket].id;
    }
    mConnectionsMutex.unlock();
    return id;
}

void ExServer::getConnectionIds(QStringList &connectionIds)
{
    mConnectionsMutex.lock();
    for (QTcpSocket *soc : mConnections.keys())
    {
        if (!connectionIds.contains(mConnections[soc].id))
        {
            connectionIds.push_back(mConnections[soc].id);
        }
    }
    mConnectionsMutex.unlock();
}

bool ExServer::isOnline(const QString &id)
{
    bool io = false;
    mConnectionsMutex.lock();
    for (QTcpSocket *soc : mConnections.keys())
    {
        if (mConnections[soc].id == id)
        {
            io = true;
            break;
        }
    }
    mConnectionsMutex.unlock();
    return io;
}

void ExServer::sendMessage(QTcpSocket *socket, QJsonObject &message)
{
    mConnectionsMutex.lock();
    if (mConnections.contains(socket) && socket->state() == QTcpSocket::ConnectedState)
    {
        socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    }
    mConnectionsMutex.unlock();
}

void ExServer::sendMessage(const QString &id, QJsonObject &message)
{
    mConnectionsMutex.lock();
    for (QTcpSocket *soc : mConnections.keys())
    {
        if (mConnections[soc].id == id)
        {
            QMetaObject::invokeMethod(soc, [this, soc, message]()
            {
                sendMessage2(soc, message);
            });
        }
    }
    mConnectionsMutex.unlock();
}

void ExServer::sendErrorMessage(QTcpSocket *socket, const QString &text)
{
    QJsonObject msg;
    msg["exnetwork_error"] = text;
    sendMessage(socket, msg);
}

void ExServer::sendErrorMessage(const QString &id, const QString &text)
{
    mConnectionsMutex.lock();
    for (QTcpSocket *soc : mConnections.keys())
    {
        if (mConnections[soc].id == id)
        {
            QMetaObject::invokeMethod(soc, [this, soc, text]()
            {
                sendErrorMessage2(soc, text);
            });
        }
    }
    mConnectionsMutex.unlock();
}

int ExServer::connectionsCount()
{
    mConnectionsMutex.lock();
    int cc = mConnections.size();
    mConnectionsMutex.unlock();
    return cc;
}

ExServer::ExServer(quint16 port, QObject *parent) :
    QTcpServer(parent),
    mPort(port)
{
    connect(&mPingTimer, &QTimer::timeout, this, &ExServer::onPingTimerTimeout);
}

void ExServer::start()
{
    if (listen(QHostAddress::Any, mPort))
    {
        qDebug() << "The server is started";
    }
    else
    {
        qDebug() << "The server cannot be started";
    }
    mPingTimer.start(mPingIntervalSecs * 1000);
}
