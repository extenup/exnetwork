#ifndef EXSERVER_H
#define EXSERVER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QVector>
#include <QJsonObject>
#include <QTimer>
//#include <QMutex>
#include "exsc.h"

class ExServer
{
protected:
    int mMaxRequestsPerMinute = INT_MAX;
    time_t mRequestsPerMinuteClearTime = 0;

    QMap<int, QByteArray> mBuffers;
    //QMutex mBuffersMutex;

    QMap<QString, int> mOnline;
    //QMutex mOnlineMutex;

    QString mBanList;
    //QMutex mBanListMutex;

    QMap<QString, int> mRequestsPerMinute;
    //QMutex mRequestsPerMinuteMutex;

    int mExscDescriptor = 0;

    void addLog(const QString &filename, const QString &text);

    void processMessage(struct exsc_excon &con, QJsonObject &message);

protected:
    void setConnectionName(struct exsc_excon &con, const QString &name);
    void getConnectionsNames(QStringList &connectionIds);

    void sendMessage(struct exsc_excon &con, QJsonObject &message);
    void sendMessage(const QString &conName, QJsonObject &message);

    //void sendErrorMessage(struct exsc_excon &con, const QString &text, const QString &errorCode);

    virtual void readMessage(struct exsc_excon &con, QJsonObject &message) = 0;
    virtual void login(const QString &conName);
    virtual void logout(const QString &conName);
    virtual void closeConnection(struct exsc_excon &con);

public:
    void init(int exscDescriptor);

    void exsc_newcon(struct exsc_excon con);
    void exsc_closecon(struct exsc_excon con);
    void exsc_recv(struct exsc_excon con, char *buf, int bufsize);

    QJsonObject conToJcon(exsc_excon &con);
    exsc_excon jconToCon(const QJsonObject &jcon);

    //int connectionsCount();
    bool isOnline(const QString &name);
    void setMaxRequestsPerMinute(int maxRequestsPerMinute);
};

#endif // EXSERVER_H
