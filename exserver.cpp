#include "exserver.h"
#include <QJsonDocument>
#include <QDateTime>
#include <QFile>

void ExServer::incomingConnection(qintptr socketDescriptor)
{
    QTcpSocket *soc = new QTcpSocket();
    if (soc->setSocketDescriptor(socketDescriptor))
    {
        if (!mBanList.contains(soc->peerAddress().toString()))
        {
            SocketInfo socInf;
            socInf.lastActivity = QDateTime::currentSecsSinceEpoch();
            mConnections[soc] = socInf;

            connect(soc, &QTcpSocket::readyRead, [this, soc]()
            {
                QList<QByteArray> msgs;

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

                for (int i = 0; i < msgs.size(); i++)
                {
                    QJsonObject msg = QJsonDocument::fromJson(msgs[i]).object();
                    if (!msg.isEmpty())
                    {
                        processMessage(soc, msg);
                    }
                }
            });
        }
        else
        {
            deleteSocket(soc);
        }
    }
    else
    {
        deleteSocket(soc);
        qDebug() << "setSocketDescriptor ERROR" << Q_FUNC_INFO;
    }
}

void ExServer::addLog(const QString &text)
{
    QFile file("exserver.log");
    if (file.open(QFile::Append))
    {
        file.write(QString("%0 %1\n")
                   .arg(QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd hh:mm:ss"))
                   .arg(text).toUtf8());
        file.close();
    }
    else
    {
        qDebug() << "Cannot open" << file.fileName() << "file";
    }
}

void ExServer::processMessage(QTcpSocket *socket, QJsonObject &message)
{
    QString peerAddress = socket->peerAddress().toString();

    int msgCount = mRequestsPerMinute[peerAddress]++;

    if (msgCount < mMaxRequestsPerMinute)
    {
        if (message.contains("exnetwork_ping"))
        {
            ping(socket, message);
        }
        readMessage(socket, message);
    }
    else
    {
        if (!mBanList.contains(peerAddress))
        {
            mBanList.push_back(peerAddress);
            deleteSocket(socket);
            addLog(QString("Ban %0").arg(peerAddress).toUtf8());
        }
    }
}

void ExServer::ping(QTcpSocket *socket, QJsonObject &message)
{
    if (mConnections.contains(socket))
    {
        mConnections[socket].lastActivity = QDateTime::currentSecsSinceEpoch();
    }
    sendMessage(socket, message);
}

void ExServer::deleteSocket(QTcpSocket *socket)
{
    connect(socket, &QTcpSocket::destroyed, this, [this, socket]()
    {
        removeOnlineId(socket, mConnections[socket].id);
        mConnections.remove(socket);
    });
    socket->deleteLater();
}

void ExServer::removeOnlineId(QTcpSocket *socket, QString id)
{
    if (mOnlineIds.contains(id))
    {
        mOnlineIds[id].removeAll(socket);
        if (mOnlineIds[id].isEmpty())
        {
            mOnlineIds.remove(id);
        }
    }
}

void ExServer::onPingTimerTimeout()
{
    qint64 secs = QDateTime::currentSecsSinceEpoch();
    for (QTcpSocket *soc : mConnections.keys())
    {
        if (secs - mConnections[soc].lastActivity > mPingTimeoutSecs)
        {
            deleteSocket(soc);
        }
    }
}

void ExServer::setConnectionId(QTcpSocket *socket, const QString &id)
{
    if (mConnections.contains(socket))
    {
        removeOnlineId(socket, mConnections[socket].id);
        if (!mOnlineIds[id].contains(socket))
        {
            mOnlineIds[id].push_back(socket);
        }
        mConnections[socket].id = id;
    }
}

QString ExServer::getConnectionId(QTcpSocket *socket)
{
    QString id;
    if (mConnections.contains(socket))
    {
        id = mConnections[socket].id;
    }
    return id;
}

void ExServer::getConnectionIds(QStringList &connectionIds)
{
    for (QTcpSocket *soc : mConnections.keys())
    {
        if (!connectionIds.contains(mConnections[soc].id))
        {
            connectionIds.push_back(mConnections[soc].id);
        }
    }
}

bool ExServer::isOnline(QTcpSocket *socket)
{
    return mConnections.contains(socket);
}

bool ExServer::isOnline(const QString &id)
{
    return mOnlineIds.contains(id);
}

void ExServer::sendMessage(QTcpSocket *socket, QJsonObject &message)
{
    if (mConnections.contains(socket) && socket->state() == QTcpSocket::ConnectedState)
    {
        socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    }
}

void ExServer::sendMessage(const QString &id, QJsonObject &message)
{
    for (QTcpSocket *soc : mConnections.keys())
    {
        if (mConnections[soc].id == id)
        {
            sendMessage(soc, message);
        }
    }
}

void ExServer::sendErrorMessage(QTcpSocket *socket, const QString &text)
{
    QJsonObject msg;
    msg["exnetwork_error"] = text;
    sendMessage(socket, msg);
}

void ExServer::sendErrorMessage(const QString &id, const QString &text)
{
    for (QTcpSocket *soc : mConnections.keys())
    {
        if (mConnections[soc].id == id)
        {
            sendErrorMessage(soc, text);
        }
    }
}

void ExServer::started(bool ok)
{
    Q_UNUSED(ok)
}

ExServer::ExServer(quint16 port, QObject *parent) :
    QTcpServer(parent),
    mPort(port)
{
    QFile::remove(mLogPath);
    connect(&mPingTimer, &QTimer::timeout, this, &ExServer::onPingTimerTimeout);
    connect(&mClearRequestsPerMinuteTimer, &QTimer::timeout, this, [this]()
    {
        mRequestsPerMinute.clear();
    });
    mClearRequestsPerMinuteTimer.start(60000);
}

void ExServer::start()
{
    if (listen(QHostAddress::Any, mPort))
    {
        started(true);
        mPingTimer.start(mPingIntervalSecs * 1000);
    }
    else
    {
        started(false);
    }
}

int ExServer::connectionsCount()
{
    return mConnections.count();
}

void ExServer::setMaxRequestsPerMinute(int maxRequestsPerMinute)
{
    mMaxRequestsPerMinute = maxRequestsPerMinute;
}
