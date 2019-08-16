#include "exclient.h"
#include <QJsonDocument>
#include <QDateTime>

void ExClient::connectToHost()
{
    mPingTimer.stop();

    if (mSocket != nullptr)
    {
        mSocket->disconnect();
        mSocket->deleteLater();
    }

    mSocket = new QTcpSocket(this);

    connect(mSocket, SIGNAL(connected()),
            this, SLOT(onSocketConnected()));
    connect(mSocket, SIGNAL(disconnected()),
            this, SLOT(onSocketDisconnected()));
    connect(mSocket, SIGNAL(readyRead()),
            this, SLOT(onSocketReadyRead()));
    connect(mSocket, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(onSocketError(QAbstractSocket::SocketError)));

    mSocket->connectToHost(mServerAddress, mServerPort);
}

void ExClient::processMessage(const QJsonObject &message)
{
    if (message.contains("exnetwork_ping"))
    {
        updateLastActivity();
    }
    else
    if (message.contains("exnetwork_error"))
    {
        emit errorEvent(message["exnetwork_error"].toString());
    }

    readMessage(message);
}

void ExClient::updateLastActivity()
{
    mLastActivity = QDateTime::currentDateTime().toUTC().toSecsSinceEpoch();
}

void ExClient::onSocketConnected()
{
    updateLastActivity();
    onPingTimerTimeout();
    mPingTimer.start();
    emit connectedEvent();
}

void ExClient::onSocketDisconnected()
{
    connectToHost();
}

void ExClient::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    QTimer::singleShot(1000, [&]()
    {
        connectToHost();
    });
}

void ExClient::onSocketReadyRead()
{
    QByteArray rawMessage = mSocket->readAll();
    mBuffer += rawMessage;
    QList<QByteArray> list = mBuffer.split('\n');

    if (list.size() > 1)
    {
        mBuffer = list.last();
        for (int i = 0; i < list.size() - 1; i++)
        {
            QJsonObject message = QJsonDocument::fromJson(list[i]).object();
            if (!message.isEmpty())
            {
                processMessage(message);
            }
            else
            {
                qDebug() << QString("Wrong message format %0").arg(QString(list[i]));
            }
        }
    }
}

void ExClient::onPingTimerTimeout()
{
    ping();

    qint64 currentDateTime = QDateTime::currentDateTime().toUTC().toSecsSinceEpoch();
    if (currentDateTime - mLastActivity > mPingTimeoutSecs)
    {
        mSocket->disconnectFromHost();
    }
}

void ExClient::sendMessage(QJsonObject message)
{
    for (const QString &key : mTail.keys())
    {
        message[key] = mTail[key];
    }

    if (mSocket->state() == QTcpSocket::ConnectedState)
    {
        mSocket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    }
}

void ExClient::ping()
{
    QJsonObject message;
    message["exnetwork_ping"];
    sendMessage(message);
}

void ExClient::removeFromTail(const QString &key)
{
    mTail.remove(key);
}

ExClient::ExClient(QObject *parent) :
    QObject(parent)
{
    connect(&mPingTimer, SIGNAL(timeout()),
            this, SLOT(onPingTimerTimeout()));

    mPingTimer.setInterval(mPingIntervalSecs * 1000);
}

ExClient::~ExClient()
{
    mSocket->disconnect();
    mSocket->deleteLater();
}

void ExClient::connectToHost(const QString &serverAddress, quint16 serverPort)
{
    mServerAddress = serverAddress;
    mServerPort = serverPort;
    connectToHost();
}
