// version 1

#include "exserverhttp.h"
#include <QJsonDocument>
#include <QDateTime>
#include <QFile>
#include <QDebug>
#include <QDir>

void ExServerHttp::addLog(const QString &filePath, const QString &text)
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

void ExServerHttp::processMessage(struct exsc_excon &con, QHash<QString, QString> &headers, QByteArray &data)
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
        addLog("logs/ban_list_detail.log", QString("%0 : %1\n\n").arg(con.addr));
    }

    if (msgCount < mMaxRequestsPerMinute || QString(con.addr) == "127.0.0.1" || mWhiteList.contains(con.addr))
    {
        readMessage(con, headers, data);
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

void ExServerHttp::init(int exscDescriptor)
{
    mExscDescriptor = exscDescriptor;
}

void ExServerHttp::setConnectionName(struct exsc_excon &con, const QString &name)
{
    exsc_setconname(mExscDescriptor, &con, name.toUtf8().data());
    mOnline[name]++;
}

void ExServerHttp::getConnectionsNames(QStringList &connectionsNames)
{
    connectionsNames = mOnline.keys();
}

void ExServerHttp::send(exsc_excon &con, QByteArray data, const QByteArray &contentType)
{
    QByteArray resp = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: " + contentType + "\r\n"
                      "Content-Length: " + QString::number(data.length()).toUtf8() + "\r\n"
                      "\r\n";
    resp += data;
    exsc_send(mExscDescriptor, &con, resp.data(), resp.size());
}

void ExServerHttp::sendMessage(struct exsc_excon &con, QJsonObject &message)
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

void ExServerHttp::sendMessage(const QString &conName, QJsonObject &message)
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

void ExServerHttp::addToWhiteList(const QString &addr)
{
    mWhiteList.insert(addr);
}

void ExServerHttp::login(const QString &conName)
{
    Q_UNUSED(conName)
}

void ExServerHttp::logout(const QString &conName)
{
    Q_UNUSED(conName)
}

void ExServerHttp::closeConnection(struct exsc_excon &con)
{
    Q_UNUSED(con)
}

ExServerHttp::ExServerHttp()
{
    QDir().mkpath("logs");
}

void ExServerHttp::exsc_newcon(struct exsc_excon con)
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
        mRequests[con.id]; // Create buffer
    }
}

void ExServerHttp::exsc_closecon(struct exsc_excon con)
{
    mActiveAddresses[con.addr]--;
    if (mActiveAddresses[con.addr] == 0)
    {
        mActiveAddresses.remove(con.addr);
    }

    mRequests.remove(con.id);

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

void ExServerHttp::exsc_recv(struct exsc_excon con, char *buf, int bufsize)
{
    QByteArray buffer(buf, bufsize);

    if (!mBanList.contains(con.addr))
    {
        if (mRequests.contains(con.id))
        {
            if (mRequests[con.id].headers.isEmpty())
            {
                int p0;
                if ((p0 = buffer.indexOf("\r\n\r\n")) != -1)
                {
                    QString headers = buffer.mid(0, p0);
                    buffer = buffer.mid(p0 + 4);

                    QStringList hrows = headers.mid(0, p0).split("\r\n");
                    if (!hrows.isEmpty())
                    {
                        for (int i = 1; i < hrows.size(); i++)
                        {
                            QStringList hcols = hrows[i].split(": ");
                            if (hcols.size() == 2)
                            {
                                mRequests[con.id].headers[hcols[0]] = hcols[1];
                            }
                        }

                        QString fr = hrows[0];
                        QStringList frsl = fr.split(" ");
                        if (frsl.size() == 3)
                        {
                            mRequests[con.id].headers[":method"] = frsl[0];
                            mRequests[con.id].headers[":path"] = frsl[1];
                            mRequests[con.id].headers[":scheme"] = frsl[2];
                        }

                        if (mRequests[con.id].headers[":method"] == "GET")
                        {
                            processMessage(con, mRequests[con.id].headers, mRequests[con.id].buffer);
                            mRequests[con.id].headers.clear();
                            mRequests[con.id].buffer = "";
                        }
                    }
                }
            }

            if (!mRequests[con.id].headers.isEmpty())
            {
                mRequests[con.id].buffer += buffer;
                if (mRequests[con.id].buffer.size() == mRequests[con.id].headers["Content-Length"].toInt())
                {
                    processMessage(con, mRequests[con.id].headers, mRequests[con.id].buffer);
                    mRequests[con.id].headers.clear();
                    mRequests[con.id].buffer = "";
                }
            }
        }
    }
}

bool ExServerHttp::isOnline(const QString &name)
{
    bool io = mOnline.contains(name);
    return io;
}

void ExServerHttp::setMaxRequestsPerMinute(int maxRequestsPerMinute)
{
    mMaxRequestsPerMinute = maxRequestsPerMinute;
}

void ExServerHttp::setMaxActiveAddresses(int maxActiveAddresses)
{
    mMaxActiveAddresses = maxActiveAddresses;
}
