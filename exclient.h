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

private:
    const int mPingIntervalSecs = 10;
    const int mPingTimeoutSecs = int(mPingIntervalSecs * 1.5);

    QTimer mPingTimer;
    QString mServerAddress;
    quint16 mServerPort = 0;
    QTcpSocket *mSocket = nullptr;
    QByteArray mBuffer;
    qint64 mLastActivity = 0;
    QJsonObject mTail;

    void connectToHost();

    void processMessage(const QJsonObject &message);
    void updateLastActivity();

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onSocketReadyRead();
    void onPingTimerTimeout();

protected:
    void sendMessage(QJsonObject message);

    void ping();
    
    template<typename T>
    void addToTail(const QString &key, const T &value) { mTail[key] = value; }
    void removeFromTail(const QString &key);

    virtual void readMessage(const QJsonObject &message) = 0;

public:
    ExClient(QObject *parent = nullptr);
    ~ExClient();
    void connectToHost(const QString &serverAddress, quint16 serverPort);

signals:
    void connectedEvent();
    void errorEvent(const QString &error);
};

#endif // EXCLIENT_H

