// version 2

#include "exsmtpclient.h"
#include <QFile>
#include <QFileInfo>

ExSmtpClient::ExSmtpClient(QObject *parent) :
    QObject(parent)
{
}

int ExSmtpClient::sendMessage(const QString &msg, QString &response)
{
    int res = -1;
    if (mSocket != nullptr)
    {
        mSocket->write(QString("%0").arg(msg).toUtf8());
        mSocket->waitForReadyRead();
        response = mSocket->readAll();
        res = response.mid(0, 3).toInt();
    }
    return res;
}

bool ExSmtpClient::logIn(const QString &user, const QString &pass, const QString &host, quint16 port)
{
    QString lastResponse;

    mUser = user;
    mPass = pass;
    mHost = host;
    mPort = port;

    int res = -1;

    if (mSocket != nullptr)
    {
        mSocket->deleteLater();
    }
    mSocket = new QSslSocket();

    mSocket->connectToHostEncrypted(mHost, mPort);
    if (mSocket->waitForEncrypted())
    {
        if (mSocket->waitForReadyRead())
        {
            if (mSocket->readAll().mid(0, 3).toInt() == 220)
            {
                if ((res = sendMessage("EHLO localhost\r\n", lastResponse)) == 250)
                //if ((res = sendMessage("EHLO localhost\r\n", lastResponse)) == 220)
                {
                    if ((res = sendMessage("AUTH LOGIN\r\n", lastResponse)) == 334)
                    {
                        if ((res = sendMessage(mUser.toUtf8().toBase64() + "\r\n", lastResponse)) == 334)
                        {
                            if ((res = sendMessage(mPass.toUtf8().toBase64() + "\r\n", lastResponse)) == 235)
                            {
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }

    qDebug() << Q_FUNC_INFO << "ERROR" << res << lastResponse;
    return false;
}

int ExSmtpClient::sendMail(const QString &from, const QString &to, const QString &subject, const QString &body, QStringList files)
{
    QString lastResponse;

    QString message;
    message.append("To: " + to + "\n");
    message.append("From: " + from + "\n");
    message.append("Subject: " + subject + "\n");

    //Let's intitiate multipart MIME with cutting boundary "frontier"
    message.append("MIME-Version: 1.0\n");
    message.append("Content-Type: multipart/mixed; boundary=frontier\n\n");

    message.append( "--frontier\n" );
    //message.append( "Content-Type: text/html\n\n" );  //Uncomment this for HTML formating, coment the line below
    //message.append( "Content-Type: text/plain\n\n" );
    message.append("Content-Type: text/html; charset=UTF8;\n\n");
    message.append(body);
    message.append("\n\n");

    if (!files.isEmpty())
    {
        //qDebug() << "Files to be sent: " << files.size();
        foreach(QString filePath, files)
        {
            QFile file(filePath);
            if(file.exists())
            {
                if (!file.open(QIODevice::ReadOnly))
                {
                    qDebug() << "Couldn't open the file";
                    return -1;
                }
                QByteArray bytes = file.readAll();
                message.append( "--frontier\n" );
                message.append( "Content-Type: application/octet-stream\nContent-Disposition: attachment; filename=" + QFileInfo(file.fileName()).fileName() +";\nContent-Transfer-Encoding: base64\n\n" );
                message.append(bytes.toBase64());
                message.append("\n");
            }
        }
    }

    message.append( "--frontier--\n" );

    message.replace( QString::fromLatin1( "\n" ), QString::fromLatin1( "\r\n" ) );
    message.replace( QString::fromLatin1( "\r\n.\r\n" ),QString::fromLatin1( "\r\n..\r\n" ) );

    int res = -1;

//    soc.connectToHostEncrypted(mHost, mPort);
//    if (soc.waitForConnected())
//    {
//        if (soc.waitForReadyRead())
//        {
//            if (soc.readAll().mid(0, 3).toInt() == 220)
//            {
//                if ((res = sendMessage("EHLO localhost\r\n")) == 250)
//                {
//                    if ((res = sendMessage("AUTH LOGIN\r\n")) == 334)
//                    {
//                        if ((res = sendMessage(mUser.toUtf8().toBase64() + "\r\n")) == 334)
//                        {
//                            if ((res = sendMessage(mPass.toUtf8().toBase64() + "\r\n")) == 235)
//                            {
                                if ((res = sendMessage(QString("MAIL FROM:<%0>\r\n").arg(from), lastResponse)) == 250)
                                {
                                    if ((res = sendMessage(QString("RCPT TO:<%0>\r\n").arg(to), lastResponse)) == 250)
                                    {
                                        if ((res = sendMessage("DATA\r\n", lastResponse)) == 354)
                                        {
                                            if ((res = sendMessage(QString("%0\r\n.\r\n").arg(message), lastResponse)) == 250)
                                            {
                                                return 0;
                                                //if ((res = sendMessage("QUIT\r\n")) == 221)
                                                //{
                                                //    return true;
                                                //}
                                            }
                                        }
                                    }
                                }
//                            }
//                        }
//                    }
//                }
//            }
//        }
//    }

    qDebug() << Q_FUNC_INFO << "ERROR" << res << lastResponse;
    return res;
}
