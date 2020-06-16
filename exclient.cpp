#include "exclient.h"
#include <QJsonDocument>
#include <QDateTime>
#include <QFile>
#include <QHostAddress>
#include <QStandardPaths>


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
    mPingTimer.start();

    if (mSocket != nullptr)
    {
        mSocket->disconnect();
        mSocket->deleteLater();
    }

    mSocket = new QTcpSocket(this);
    mSocket->setSocketOption(QAbstractSocket::LowDelayOption,1);


    connect(mSocket, &QTcpSocket::connected, this, &ExClient::onSocketConnected);
    connect(mSocket, &QTcpSocket::readyRead, this, &ExClient::onSocketReadyRead);

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
        exlog("Exclient: exnetwork_error: " + message["exnetwork_error"].toString());
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
    mConnected = true;
    updateLastActivity();
    onPingTimerTimeout();
    emit connectedEvent();
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

    if (rawMessage.contains("pass") && !rawMessage.contains("exnetwork_ping")) {
            qDebug() << "ERROR!!!!" << rawMessage;
    }


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
    qint64 currentDateTime = QDateTime::currentDateTime().toUTC().toSecsSinceEpoch();
    if (currentDateTime - mLastActivity > mPingTimeoutSecs)
    {
        exlog("Reconnect");
        updateLastActivity();
        connectToHost();
    }

    ping();
}

void ExClient::sendMessage(QJsonObject message)
{
    for (const QString &key : mTail.keys())
    {
        message[key] = mTail[key];
    }

    if (mSocket)
    if (mSocket->state() == QTcpSocket::ConnectedState)
    {
        mSocket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
        mSocket->flush();
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

QJsonObject ExClient::tail()
{
    return mTail;
}

ExClient::ExClient(const QString &className, QObject *parent) :
    QObject(parent),
    mClassName(className)
{
    connect(&mPingTimer, &QTimer::timeout, this, &ExClient::onPingTimerTimeout);
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
