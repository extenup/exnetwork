#include "exserver.h"
#include <QJsonDocument>
#include <QDateTime>
#include <QThread>
#include <QFile>

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

void ExServer::incomingConnection(qintptr socketDescriptor)
{
    QTcpSocket *soc = new QTcpSocket();
    if (soc->setSocketDescriptor(socketDescriptor))
    {
        if (!mBanList.contains(soc->peerAddress().toString()))
        {
            if (mThreadCount < mMaxThreadCount)
            {
                QThread *thr = new QThread();
                ++mThreadCount;
                soc->moveToThread(thr);
                connect(soc, &QTcpSocket::destroyed, thr, &QThread::quit);
                connect(thr, &QThread::finished, thr, &QThread::deleteLater);
                connect(thr, &QThread::destroyed, [this]()
                {
                    --mThreadCount;
                });
                thr->start();
            }

            SocketInfo socInf;
            socInf.lastActivity = QDateTime::currentSecsSinceEpoch();
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
        }
        else
        {
            soc->deleteLater();
        }
    }
    else
    {
        soc->deleteLater();
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
    mRequestsPerMinute[peerAddress]++;

    if (mRequestsPerMinute[peerAddress] < mMaxRequestsPerMinute)
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
            socket->disconnectFromHost();
            addLog(QString("Ban %0").arg(peerAddress).toUtf8());
        }
    }
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

void ExServer::onPingTimerTimeout()
{
    mConnectionsMutex.lock();
    qint64 secs = QDateTime::currentSecsSinceEpoch();
    for (QTcpSocket *soc : mConnections.keys())
    {
        if (secs - mConnections[soc].lastActivity > mPingTimeoutSecs)
        {
            mConnections.remove(soc);
            soc->deleteLater();
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
        QByteArray buf = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
        socket->write(buf);
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

ExServer::ExServer(quint16 port, QObject *parent) :
    QTcpServer(parent),
    mPort(port)
{
    connect(&mPingTimer, &QTimer::timeout, this, &ExServer::onPingTimerTimeout);
    connect(&mClearRequestsPerMinuteTimer, &QTimer::timeout, this, [this]()
    {
        mRequestsPerMinute.clear();
    });
    mClearRequestsPerMinuteTimer.start(60000);
}

void ExServer::setMaxThreadCount(int maxThreadCount)
{
    mMaxThreadCount = maxThreadCount;
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

int ExServer::connectionsCount()
{
    return mConnections.count();
}

void ExServer::setMaxRequestsPerMinute(int maxRequestsPerMinute)
{
    mMaxRequestsPerMinute = maxRequestsPerMinute;
}
