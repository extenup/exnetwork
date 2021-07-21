// version 1

#include "exsmtpclient.h"
#include <QFile>
#include <QFileInfo>

ExSmtpClient::ExSmtpClient(QObject *parent) :
    QObject(parent)
{
}

bool ExSmtpClient::sendMail(const QString &from, const QString &to, const QString &subject, const QString &body, QStringList files)
{
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
                    return false;
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

    QSslSocket soc;

    auto sendMessage = [&soc](const QString &msg)
    {
        soc.write(QString("%0").arg(msg).toUtf8());
        soc.waitForReadyRead();
        return soc.readAll().mid(0, 3).toInt();
    };

    soc.connectToHostEncrypted(mHost, mPort);
    if (soc.waitForConnected())
    {
        if (soc.waitForReadyRead())
        {
            if (soc.readAll().mid(0, 3).toInt() == 220)
            {
                if (sendMessage("EHLO localhost\r\n") == 250)
                {
                    if (sendMessage("AUTH LOGIN\r\n") == 334)
                    {
                        if (sendMessage(mUser.toUtf8().toBase64() + "\r\n") == 334)
                        {
                            if (sendMessage(mPass.toUtf8().toBase64() + "\r\n") == 235)
                            {
                                if (sendMessage(QString("MAIL FROM:<%0>\r\n").arg(from)) == 250)
                                {
                                    if (sendMessage(QString("RCPT TO:<%0>\r\n").arg(to)) == 250)
                                    {
                                        if (sendMessage("DATA\r\n") == 354)
                                        {
                                            if (sendMessage(QString("%0\r\n.\r\n").arg(message)) == 250)
                                            {
                                                if (sendMessage("QUIT\r\n") == 221)
                                                {
                                                    return true;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return false;
}

void ExSmtpClient::init(const QString &user, const QString &pass, const QString &host, quint16 port)
{
    mUser = user;
    mPass = pass;
    mHost = host;
    mPort = port;
}

