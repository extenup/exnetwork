#include "exserver.h"
#include <QJsonDocument>
#include <QDateTime>

void ExServer::incomingConnection(qintptr socketDescriptor)
{
    QThread *thr = new QThread();
    QTcpSocket *soc = new QTcpSocket();

    soc->setSocketDescriptor(socketDescriptor);
    soc->moveToThread(thr);

    SocketInfo socInf;
    socInf.lastActivity = QDateTime::currentSecsSinceEpoch();
    socInf.thread = thr;
    mConnectionsMutex.lock();
    mConnections[soc] = socInf;
    mConnectionsMutex.unlock();

    connect(thr, &QThread::finished, thr, &QThread::deleteLater);
    connect(thr, &QThread::finished, soc, &QTcpSocket::deleteLater);
    connect(soc, &QTcpSocket::readyRead, [this, soc]()
    {
        mConnectionsMutex.lock();
        mConnections[soc].buffer += soc->readAll();
        QList<QByteArray> msgs = mConnections[soc].buffer.split('\n');
        if (msgs.size() > 1)
        {
            mConnections[soc].buffer = msgs.last();
            msgs.pop_back();
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

void ExServer::onPingTimerTimeout()
{
    qint64 secs = QDateTime::currentSecsSinceEpoch();
    mConnectionsMutex.lock();
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

QString ExServer::getConnectionIdBySocket(QTcpSocket *socket)
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

void ExServer::sendMessage2(QTcpSocket *socket, QJsonObject message)
{
    sendMessage(socket, message);
}

void ExServer::sendMessage(const QString &id, QJsonObject &message)
{
    QVector<QTcpSocket *> socs;

    mConnectionsMutex.lock();
    for (QTcpSocket *soc : mConnections.keys())
    {
        if (mConnections[soc].id == id)
        {
            socs.push_back(soc);
        }
    }
    mConnectionsMutex.unlock();

    for (QTcpSocket *soc : socs)
    {
        bool b0 = soc->isOpen();
        bool b1 = soc->isValid();

        QMetaObject::invokeMethod(soc, [this]()
        {
            QString str = "hhh";
        });

        QMetaObject::invokeMethod(soc, [this, soc]()
        {
            QTcpSocket *s = soc;
        });

        QMetaObject::invokeMethod(soc, [this, soc, message]()
        {
            sendMessage2(soc, message);
        });
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
    QVector<QTcpSocket *> socs;

    mConnectionsMutex.lock();
    for (QTcpSocket *soc : mConnections.keys())
    {
        if (mConnections[soc].id == id)
        {
            socs.push_back(soc);
        }
    }
    mConnectionsMutex.unlock();

    for (QTcpSocket *soc : socs)
    {
        QMetaObject::invokeMethod(soc, [this, soc, text]()
        {
            sendErrorMessage(soc, text);
        });
    }
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
