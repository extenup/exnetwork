#ifndef EXSERVER_H
#define EXSERVER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QTimer>

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
    const int mPingIntervalSecs = 10;
    const int mPingTimeoutSecs = mPingIntervalSecs * 3;
    const QString mLogPath = "./exserver.log";

    quint16 mPort = 0;
    QTimer mPingTimer;

    QMap<QTcpSocket *, SocketInfo> mConnections;
    QMap<QString, QVector<QTcpSocket *>> mOnlineIds;

    int mMaxRequestsPerMinute = std::numeric_limits<int>::max();
    QString mBanList;
    QMap<QString, int> mRequestsPerMinute;
    QTimer mClearRequestsPerMinuteTimer;

    void incomingConnection(qintptr socketDescriptor) override;

    void addLog(const QString &text);

    void processMessage(QTcpSocket *socket, QJsonObject &message);
    void ping(QTcpSocket *socket, QJsonObject &message);

    void deleteSocket(QTcpSocket *socket);
    void removeOnlineId(QTcpSocket *socket, QString id);

private slots:
    void onPingTimerTimeout();

protected:
    void setConnectionId(QTcpSocket *socket, const QString &id);
    QString getConnectionId(QTcpSocket *socket);
    void getConnectionIds(QStringList &connectionIds);
    bool isOnline(QTcpSocket *socket);
    bool isOnline(const QString &id);

    void sendMessage(QTcpSocket *socket, QJsonObject &message);
    void sendMessage(const QString &id, QJsonObject &message);

    void sendErrorMessage(QTcpSocket *socket, const QString &text);
    void sendErrorMessage(const QString &id, const QString &text);

    virtual void readMessage(QTcpSocket *socket, QJsonObject &message) = 0;
    virtual void started(bool ok);

public:
    ExServer(quint16 port, QObject *parent = nullptr);
    virtual void start();
    int connectionsCount();
    void setMaxRequestsPerMinute(int maxRequestsPerMinute);
};

#endif // EXSERVER_H
