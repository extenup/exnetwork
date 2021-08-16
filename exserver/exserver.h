// version 2

#ifndef EXSERVER_H
#define EXSERVER_H

#include "../exsc/exsc.h"
#include <QObject>
#include <QString>
#include <QMap>
#include <QHash>
#include <QVector>
#include <QJsonObject>
#include <QSet>

class ExServer
{
protected:
    int mExscDescriptor = 0;

    QMap<QString, int> mRequestsPerMinute;
    int mMaxRequestsPerMinute = INT_MAX;
    time_t mRequestsPerMinuteClearTime = 0;

    QHash<QString, int> mActiveAddresses;
    int mMaxActiveAddresses = INT_MAX;

    QString mBanList;

    QMap<int, QByteArray> mBuffers;
    QMap<QString, int> mOnline;

    QSet<QString> mWhiteList;

    void addLog(const QString &filename, const QString &text);

    void processMessage(struct exsc_excon &con, QJsonObject &message);

    void init(int exscDescriptor);

    void setConnectionName(struct exsc_excon &con, const QString &name);
    void getConnectionsNames(QStringList &connectionIds);

    void sendMessage(struct exsc_excon &con, QJsonObject &message);
    void sendMessage(const QString &conName, QJsonObject &message);

    void addToWhiteList(const QString &addr);

    virtual void readMessage(struct exsc_excon &con, QJsonObject &message) = 0;
    virtual void login(const QString &conName);
    virtual void logout(const QString &conName);
    virtual void closeConnection(struct exsc_excon &con);

public:
    ExServer();

    void exsc_newcon(struct exsc_excon con);
    void exsc_closecon(struct exsc_excon con);
    void exsc_recv(struct exsc_excon con, char *buf, int bufsize);

    QJsonObject conToJcon(exsc_excon &con);
    exsc_excon jconToCon(const QJsonObject &jcon);

    bool isOnline(const QString &name);
    void setMaxRequestsPerMinute(int maxRequestsPerMinute);
    void setMaxActiveAddresses(int maxActiveAddresses);
};

#endif // EXSERVER_H
