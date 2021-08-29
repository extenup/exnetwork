// version 2

#ifndef EXSMTPCLIENT_H
#define EXSMTPCLIENT_H

#include <QSslSocket>

class ExSmtpClient : public QObject
{
    Q_OBJECT

protected:
    QString mUser;
    QString mPass;
    QString mHost;
    quint16 mPort;
    QSslSocket *mSocket = nullptr;

public:
    ExSmtpClient(QObject *parent = nullptr);
    int sendMessage(const QString &msg, QString &response);

    bool logIn(const QString &user, const QString &pass, const QString &host, quint16 port);
    int sendMail(const QString &from, const QString &to,
                   const QString &subject, const QString &body,
                   QStringList files);
};
#endif
