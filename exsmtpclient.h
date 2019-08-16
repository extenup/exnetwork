#ifndef EXSMTPCLIENT_H
#define EXSMTPCLIENT_H

#include <QObject>
#include <QSslSocket>
#include <QTimer>

class ExSmtpClient : public QObject
{
    Q_OBJECT
protected:
    QSslSocket mSocket;
    int mStep = 0;
    QTimer mWaitTimer;

public:
    explicit ExSmtpClient(QObject *parent = nullptr);
    void sendMessage(const QString &host, quint16 port,
                     const QString &senderEmail, const QString &password, const QString &senderName,
                     const QString &recipientEmail,
                     const QString &subject, const QString &text);

signals:
    void sendMessageFinishedEvent(bool success);
};

#endif // SMTPCLIENT_H
