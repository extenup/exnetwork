// version 1

#ifndef EXCLIENT_H
#define EXCLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QJsonObject>
#include <QTimer>
#include <QMap>

class ExClient : public QObject
{
    Q_OBJECT

protected:
    const int mPingIntervalSecs = 10;
    const int mPingTimeoutSecs = int(mPingIntervalSecs * 2.5);

    QTimer *mPingTimer = nullptr;
    QString mServerAddress;
    quint16 mServerPort = 0;
    QTcpSocket *mSocket = nullptr;
    QByteArray mBuffer;
    qint64 mLastActivity = 0;
    QString mClassName;
    QVector<QJsonObject> mMessagesQueue;

    void exlog(QString text);

protected:
    QJsonObject mTail;
    bool mConnected = false;

    void connectToHost();

    void processMessage(const QJsonObject &message);
    void updateLastActivity();

private slots:
    void onSocketConnected();
    void onSocketReadyRead();
    void onPingTimerTimeout();


protected:
    bool sendMessage(QJsonObject message);

    void ping();
    
    template<typename T>
    void addToTail(const QString &key, const T &value) { mTail[key] = value; }
    void removeFromTail(const QString &key);
    QJsonObject tail();

    virtual void readMessage(const QJsonObject &message) = 0;

public:
    ExClient(const QString &className = "", QObject *parent = nullptr);
    ~ExClient();
    void connectToHost(const QString &serverAddress, quint16 serverPort);
    bool isConnected();

signals:
    void connectedEvent();
    void errorEvent(const QString &error, const QString &errorCode);
};

#endif // EXCLIENT_H

