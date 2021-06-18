// version 1.0.0

#ifndef EXSERVERHTTP_H
#define EXSERVERHTTP_H

#include "../exsc/exsc.h"
#include <QObject>
#include <QString>
#include <QMap>
#include <QHash>
#include <QVector>
#include <QJsonObject>
#include <QSet>

class ExServerHttp
{
protected:
    struct Request
    {
        QHash<QString, QString> headers;
        QByteArray buffer;
    };

    int mExscDescriptor = 0;

    QMap<QString, int> mRequestsPerMinute;
    int mMaxRequestsPerMinute = INT_MAX;
    time_t mRequestsPerMinuteClearTime = 0;

    QHash<QString, int> mActiveAddresses;
    int mMaxActiveAddresses = INT_MAX;

    QString mBanList;

    QMap<int, Request> mRequests;
    QMap<QString, int> mOnline;

    QSet<QString> mWhiteList;

    void addLog(const QString &filename, const QString &text);

    void processMessage(struct exsc_excon &con, QHash<QString, QString> &headers, QByteArray &data);

    void init(int exscDescriptor);

    void setConnectionName(struct exsc_excon &con, const QString &name);
    void getConnectionsNames(QStringList &connectionIds);

    void send(struct exsc_excon &con, QByteArray data, const QByteArray &contentType);
    void sendMessage(struct exsc_excon &con, QJsonObject &message);
    void sendMessage(const QString &conName, QJsonObject &message);

    void addToWhiteList(const QString &addr);

    virtual void readMessage(struct exsc_excon &con, QHash<QString, QString> headers, QByteArray &data) = 0;
    virtual void login(const QString &conName);
    virtual void logout(const QString &conName);
    virtual void closeConnection(struct exsc_excon &con);

public:
    ExServerHttp();

    void exsc_newcon(struct exsc_excon con);
    void exsc_closecon(struct exsc_excon con);
    void exsc_recv(struct exsc_excon con, char *buf, int bufsize);

    bool isOnline(const QString &name);
    void setMaxRequestsPerMinute(int maxRequestsPerMinute);
    void setMaxActiveAddresses(int maxActiveAddresses);
};

#endif // EXSERVERHTTP_H
