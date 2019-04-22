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

struct ExConnection
{
    QString id;
    QVector<QTcpSocket *> sockets;

    bool operator == (const QString &id)
    {
        return id == this->id;
    }

    bool operator == (const QTcpSocket *socket)
    {
        return std::find(sockets.begin(), sockets.end(), socket) != sockets.end();
    }
};

class ExServer : public QObject
{
    Q_OBJECT

private:
    const int mPingIntervalSecs = 10;
    const int mPingTimeoutSecs = int(mPingIntervalSecs * 1.5);

    QTcpServer mServer;
    QTimer mPingTimer;

    QVector<ExConnection> mConnections;
    QMap<QTcpSocket *, QByteArray> mBuffers;
    QMap<QTcpSocket *, qint64> mLastActivities;

    void deleteSocket(QTcpSocket *socket);

    void processMessage(QTcpSocket *socket, const QJsonObject &message);
    void ping(QTcpSocket *socket, const QJsonObject &message);

private slots:
    void onSocketReadyRead();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onServerNewConnection();
    void onPingTimerTimeout();

protected:
    ExConnection *addSocketToConnection(const QString &id, QTcpSocket *socket);
    void removeSocketFromConnections(QTcpSocket *socket);

    ExConnection *getConnection(const QString &id);
    ExConnection *getConnection(const QTcpSocket *socket);

    void sendMessage(QTcpSocket *socket, const QJsonObject &message);
    void sendMessage(ExConnection *connection, const QJsonObject &message);

    void sendErrorMessage(QTcpSocket *socket, const QString &text);
    void sendErrorMessage(ExConnection *connection, const QString &text);

    virtual void readMessage(QTcpSocket *socket, const QJsonObject &message);
    virtual void readHttp(QTcpSocket *socket, const QString &http);

public:
    ExServer(quint16 port, QObject *parent = nullptr);

signals:
    void connectionAddedEvent(const QString &id);
    void connectionRemovedEvent(const QString &id);
};

#endif // EXSERVER_H

