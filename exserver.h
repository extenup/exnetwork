#ifndef EXSERVER_H
#define EXSERVER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QTimer>
#include <QThread>
#include <QMutex>

struct SocketInfo
{
    QString id;
    QByteArray buffer;
    qint64 lastActivity;
};

class ExServer : public QTcpServer
{
    Q_OBJECT

private:
    int mMaxThreadCount = std::numeric_limits<int>::max();
    int mThreadCount = 0;

    void sendMessage2(QTcpSocket *socket, QJsonObject message);
    void sendErrorMessage2(QTcpSocket *socket, QString text);

    const int mPingIntervalSecs = 10;
    const int mPingTimeoutSecs = mPingIntervalSecs * 3;

    quint16 mPort = 0;
    QTimer mPingTimer;

    QMap<QTcpSocket *, SocketInfo> mConnections;
    QMutex mConnectionsMutex;

    void incomingConnection(qintptr socketDescriptor) override;

    void processMessage(QTcpSocket *socket, QJsonObject &message);
    void ping(QTcpSocket *socket, QJsonObject &message);

private slots:
    void onPingTimerTimeout();

protected:
    void setConnectionId(QTcpSocket *socket, const QString &id);
    QString getConnectionId(QTcpSocket *socket);
    void getConnectionIds(QStringList &connectionIds);
    bool isOnline(const QString &id);

    void sendMessage(QTcpSocket *socket, QJsonObject &message);
    void sendMessage(const QString &id, QJsonObject &message);

    void sendErrorMessage(QTcpSocket *socket, const QString &text);
    void sendErrorMessage(const QString &id, const QString &text);

    virtual void readMessage(QTcpSocket *socket, QJsonObject &message) = 0;

public:
    ExServer(quint16 port, QObject *parent = nullptr);
    void setMaxThreadCount(int maxThreadCount);
    virtual void start();
};

#endif // EXSERVER_H
