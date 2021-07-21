// version 1

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

public:
    ExSmtpClient(QObject *parent = nullptr);
    void init(const QString &user, const QString &pass, const QString &host, quint16 port = 465);
    bool sendMail( const QString &from, const QString &to,
                   const QString &subject, const QString &body,
                   QStringList files = QStringList());
};
#endif
