#include "exsmtpclient.h"

ExSmtpClient::ExSmtpClient(QObject *parent) : QObject(parent)
{   
}

void ExSmtpClient::sendMessage(const QString &host, quint16 port,
                               const QString &senderEmail, const QString &senderPassword, const QString &senderName,
                               const QString &recipientEmail, const QString &subject, const QString &text)
{
    mWaitTimer.disconnect();
    mSocket.disconnected();
    mStep = 0;

    connect(&mWaitTimer, &QTimer::timeout, this, [this]()
    {
        mWaitTimer.stop();
        mSocket.disconnect();
        emit sendMessageFinishedEvent(false);
    });

    connect(&mSocket, &QSslSocket::readyRead, this, [=]()
    {
        QString response = mSocket.readAll();
        if (mStep == 0)
        {
            mSocket.write(QString("EHLO %0\r\n").arg(host).toUtf8());
        }
        else
        if (mStep == 1)
        {
            mSocket.write("AUTH PLAIN " + ('\0' + senderEmail.toUtf8() + '\0' + senderPassword.toUtf8()).toBase64() + "\r\n");
        }
        else
        if (mStep == 2)
        {
            mSocket.write(QString("MAIL FROM:<%0>\r\n").arg(senderEmail).toUtf8());
        }
        else
        if (mStep == 3)
        {
            mSocket.write(QString("RCPT TO:<%0>\r\n").arg(recipientEmail).toUtf8());
        }
        else
        if (mStep == 4)
        {
            mSocket.write("DATA\r\n");
        }
        else
        if (mStep == 5)
        {
            QString message;
            message += QString("From: %0 <%1>\r\n").arg(senderName).arg(senderEmail);
            message += QString("To: <%0>\r\n").arg(recipientEmail);
            message += QString("Subject: %0\r\n").arg(subject);
            message += QString("%0\r\n").arg(text).toUtf8();
            message += ".\r\n";
            mSocket.write(message.toUtf8());
        }
        else
        if (mStep == 6)
        {
            mSocket.write("QUIT\r\n");
        }
        else
        if (mStep == 7)
        {
            mWaitTimer.stop();
            emit sendMessageFinishedEvent(true);
        }
        mStep++;
    });

    mWaitTimer.start(30000);
    mSocket.connectToHostEncrypted(host, port);
}
