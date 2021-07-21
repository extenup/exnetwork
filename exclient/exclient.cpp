// version 1

#include "exclient.h"
#include <QJsonDocument>
#include <QDateTime>
#include <QFile>
#include <QHostAddress>
#include <QStandardPaths>
//#include "../mt2/LogClient/logclient.h"

void ExClient::exlog(QString text)
{
    if (mClassName.contains("ChatClient")) return;
    QString dirWrite = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QFile f(dirWrite + "/ProviderTradeStatistics.log");
    if (f.open(QFile::Append))
    {
        f.write(QString("%0 %1 %2\n")
                .arg(QDateTime::currentDateTime().toString("dd.MM.yyyy hh:mm:ss"))
                .arg(mClassName)
                .arg(text).toUtf8());
        f.close();
    }
}

void ExClient::connectToHost()
{
    if (mPingTimer == nullptr)
    {
        mPingTimer = new QTimer();
        connect(mPingTimer, &QTimer::timeout, this, &ExClient::onPingTimerTimeout);
        mPingTimer->setInterval(mPingIntervalSecs * 1000);
    }
    mPingTimer->start();

    if (mSocket != nullptr)
    {
        mConnected = false;
        mSocket->disconnect();
        mSocket->deleteLater();
    }

    mSocket = new QTcpSocket(this);
    //mSocket->setSocketOption(QAbstractSocket::LowDelayOption,1);


    connect(mSocket, &QTcpSocket::connected, this, &ExClient::onSocketConnected);
    connect(mSocket, &QTcpSocket::readyRead, this, &ExClient::onSocketReadyRead);

    mSocket->connectToHost(mServerAddress, mServerPort);
}

void ExClient::processMessage(const QJsonObject &message)
{
    if (message["type"].toString() == "ping")
    {
        updateLastActivity();
    }
    else
    if (message.contains("error"))
    {
        if (!message["error"].toString().isEmpty()) {
            //qDebug()  << Q_FUNC_INFO << "ERROR" << message;
            exlog("Exclient: error: " + message["error"].toString());
            emit errorEvent(message["error"].toString(), message["errorCode"].toString());
        }
    }
    readMessage(message);
}

void ExClient::updateLastActivity()
{
    mLastActivity = QDateTime::currentDateTime().toSecsSinceEpoch();
}

void ExClient::onSocketConnected()
{
    mConnected = true;
    updateLastActivity();
    onPingTimerTimeout();
    emit connectedEvent();

    int currentTime = QDateTime::currentSecsSinceEpoch();
    for (QJsonObject msg : mMessagesQueue)
    {
        if (currentTime - msg["timestamp"].toInt() < 5)
        {
            sendMessage(msg);
        }
    }
    mMessagesQueue.clear();
}

void ExClient::onSocketReadyRead()
{

    /*QString ipSource = mSocket->peerAddress().toString();
    QString myipfilepath = (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/log.txt");
    QFile outFile(myipfilepath);
    outFile.open(QIODevice::WriteOnly | QIODevice::Append);
    QTextStream ts(&outFile);
    ts << ipSource << " " << endl << flush;
    outFile.close();*/


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
    qint64 currentDateTime = QDateTime::currentDateTime().toSecsSinceEpoch();
    if (currentDateTime - mLastActivity > mPingTimeoutSecs)
    {
        exlog("Reconnect");
        updateLastActivity();
        connectToHost();
    }
    else
    {
        ping();
    }
}

bool ExClient::sendMessage(QJsonObject message)
{
    //if (mClassName == "SignalsTransmitter")
    //{
    //    if (message["type"].toString() == "transmitSignal")
    //    {
    //        static int i = 0;
    //        LogClient::sendLog(QString("SignalsTransmitter %0").arg(i++));
    //   }
    //}

    bool ok = false;

    for (const QString &key : mTail.keys())
    {
        message[key] = mTail[key];
    }

    if (mSocket != nullptr)
    {
        if (mSocket->state() == QTcpSocket::ConnectedState)
        {
            QByteArray buf = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
            if(mSocket->isWritable())
            {
                int sent = mSocket->write(buf);
                if (sent == buf.length())
                {
                    ok = true;
                }
                else
                if (sent == -1)
                {
                    //LogClient::sendLog("Error socket write error");
                }
                else
                if (sent < buf.length())
                {
                    //LogClient::sendLog(QString("Error sendMessage: socket writes not all data (%0 %1)").arg(sent).arg(buf.length()));
                }
                else
                if (sent > buf.length())
                {
                    //LogClient::sendLog(QString("Error sendMessage: socket writes more data than required [sent > buf.length] (%0 %1)").arg(sent).arg(buf.length()));
                }
                mSocket->flush();
            }
            else
            {
                //LogClient::sendLog("Error sendMessage: socket is not writeable");
            }
        }
        else
        {
            //LogClient::sendLog("Error sendMessage: socket state is disconnected");
        }

        if (!ok)
        {
            if (!message.contains("timestamp"))
            {
                message["timestamp"] = QDateTime::currentSecsSinceEpoch();
                mMessagesQueue.push_back(message);
            }
            connectToHost();
        }
    }
    else
    {
        //LogClient::sendLog("Error sendMessage: socket is nullptr");
    }

    return ok;
}

void ExClient::ping()
{
    QJsonObject message;
    message["type"] = "ping";
    sendMessage(message);
    //qDebug() << Q_FUNC_INFO;
}

void ExClient::removeFromTail(const QString &key)
{
    mTail.remove(key);
}

QJsonObject ExClient::tail()
{
    return mTail;
}

ExClient::ExClient(const QString &className, QObject *parent) :
    QObject(parent),
    mClassName(className)
{
}

ExClient::~ExClient()
{
    if (mSocket != nullptr)
    {
        mSocket->disconnect();
        mSocket->deleteLater();
    }

    if (mPingTimer != nullptr)
    {
        mPingTimer->disconnect();
        mPingTimer->deleteLater();
    }
}

void ExClient::connectToHost(const QString &serverAddress, quint16 serverPort)
{
    mServerAddress = serverAddress;
    mServerPort = serverPort;
    connectToHost();
}

bool ExClient::isConnected() {
    return mConnected;
}
