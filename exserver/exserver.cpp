// version 2

#include "exserver.h"
#include <QJsonDocument>
#include <QDateTime>
#include <QFile>
#include <QDebug>
#include <QDir>

void ExServer::addLog(const QString &filePath, const QString &text)
{
    QFile file(filePath);
    if (file.open(QFile::Append))
    {
        file.write(QString("%0 %1\n")
                   .arg(QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd hh:mm:ss"))
                   .arg(text).toUtf8());
        file.close();
    }
    else
    {
        qDebug() << "Cannot open" << file.fileName() << "file";
    }
}

void ExServer::processMessage(struct exsc_excon &con, QJsonObject &message)
{
    int msgCount = 0;

    time_t t = time(NULL);
    time_t elapsed = t - mRequestsPerMinuteClearTime;
    if (elapsed > 60)
    {
        mRequestsPerMinute.clear();
        mRequestsPerMinuteClearTime = t;
    }

    msgCount = mRequestsPerMinute[con.addr]++;

    if (mRequestsPerMinute[con.addr] > 1000)
    {
        addLog("logs/ban_list_detail.log", QString("%0 : %1\n\n")
               .arg(con.addr)
               .arg(QString(QJsonDocument(message).toJson())).toUtf8());
    }

    if (msgCount < mMaxRequestsPerMinute || QString(con.addr) == "127.0.0.1" || mWhiteList.contains(con.addr))
    {
        readMessage(con, message);
    }
    else
    {
        if (!mBanList.contains(con.addr))
        {
            exsc_banaddr(mExscDescriptor, con.addr);
            mBanList.push_back(con.addr);
            addLog("logs/ban_list.log", con.addr);
        }
    }
}

void ExServer::init(int exscDescriptor)
{
    mExscDescriptor = exscDescriptor;
}

void ExServer::setConnectionName(struct exsc_excon &con, const QString &name)
{
    exsc_setconname(mExscDescriptor, &con, name.toUtf8().data());
    mOnline[name]++;
}

void ExServer::getConnectionsNames(QStringList &connectionsNames)
{
    connectionsNames = mOnline.keys();
}

void ExServer::sendMessage(struct exsc_excon &con, QJsonObject &message)
{
    QJsonDocument doc(message);
    if (!doc.isEmpty())
    {
        QByteArray buffer = doc.toJson(QJsonDocument::Compact) + '\n';
        exsc_send(mExscDescriptor, &con, buffer.data(), buffer.size());
    }
    else
    {
        qDebug() << "ERROR" << Q_FUNC_INFO;
    }
}

void ExServer::sendMessage(const QString &conName, QJsonObject &message)
{
    if (conName != "")
    {
        QJsonDocument doc(message);
        if (!doc.isEmpty())
        {
            QByteArray buffer = doc.toJson(QJsonDocument::Compact) + '\n';
            exsc_sendbyname(mExscDescriptor, conName.toUtf8().data(), buffer.data(), buffer.size());
        }
        else
        {
            qDebug() << "ERROR" << Q_FUNC_INFO;
        }
    }
    else
    {
        qDebug() << "WARNING" << Q_FUNC_INFO << "conName is EMPTY";
    }
}

void ExServer::addToWhiteList(const QString &addr)
{
    mWhiteList.insert(addr);
}

void ExServer::login(const QString &conName)
{
    Q_UNUSED(conName)
}

void ExServer::logout(const QString &conName)
{
    Q_UNUSED(conName)
}

void ExServer::closeConnection(struct exsc_excon &con)
{
    Q_UNUSED(con)
}

ExServer::ExServer()
{
    QDir().mkpath("logs");
}

void ExServer::exsc_newcon(struct exsc_excon con)
{
    mActiveAddresses[con.addr]++;
    if (mActiveAddresses[con.addr] > mMaxActiveAddresses)
    {
        exsc_banaddr(mExscDescriptor, con.addr);
        mBanList.push_back(con.addr);
        mActiveAddresses.remove(con.addr);
    }

    if (!mBanList.contains(con.addr))
    {
        mBuffers[con.id]; // Create buffer
    }
}

void ExServer::exsc_closecon(struct exsc_excon con)
{
    mActiveAddresses[con.addr]--;
    if (mActiveAddresses[con.addr] == 0)
    {
        mActiveAddresses.remove(con.addr);
    }

    mBuffers.remove(con.id);

    bool lo = false;
    mOnline[con.name]--;
    if (mOnline[con.name] == 0)
    {
        mOnline.remove(con.name);
        lo = true;
    }
    if (lo)
    {
        logout(con.name);
    }
}

void ExServer::exsc_recv(struct exsc_excon con, char *buf, int bufsize)
{
    if (!mBanList.contains(con.addr))
    {
        QList<QByteArray> msgs;

        if (mBuffers.contains(con.id))
        {
            mBuffers[con.id].append(buf, bufsize);
            msgs = mBuffers[con.id].split('\n');
            if (msgs.size() > 1)
            {
                mBuffers[con.id] = msgs.last();
                msgs.pop_back();

                for (const QByteArray &msg : qAsConst(msgs))
                {
                    QJsonObject jmsg = QJsonDocument::fromJson(msg).object();
                    if (!jmsg.isEmpty())
                    {
                        processMessage(con, jmsg);
                    }
                    else
                    {
                        //qDebug() << "WRONG MESSAGE" << msg;
                    }
                }
            }
        }
    }
}

bool ExServer::isOnline(const QString &name)
{
    bool io = mOnline.contains(name);
    return io;
}

void ExServer::setMaxRequestsPerMinute(int maxRequestsPerMinute)
{
    mMaxRequestsPerMinute = maxRequestsPerMinute;
}

void ExServer::setMaxActiveAddresses(int maxActiveAddresses)
{
    mMaxActiveAddresses = maxActiveAddresses;
}
