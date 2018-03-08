#ifndef NETWORK_H
#define NETWORK_H

#include <QObject>
#include <QtSql>
#include <QtCoreVersion>
#include <QTextStream>
#include <QTcpServer>
#include <QTcpSocket>
#include <QList>

class network : public QObject
{
    Q_OBJECT
public:
    explicit network(QObject *parent = nullptr);
    QSqlDatabase db;
    QTcpServer* server;
    QMap<QTcpSocket*, QHostAddress> connectedUsers;
    QMap<QTcpSocket*, QString> authUsers; // key: socket id, value: username (for db lookups)

    enum packetType{
        UNDEFINED = 0,
        LOGIN_REQUEST = 1,
        CONNECTION_CHECK = 2,
        SECTOR_MAP = 3
    };

signals:
    void userLoggedIn(QString username);

public slots:
    void authReader(); // only called by authenticated users for game use
    void userConnected();
    void userDisconnected();
    void serverError(QAbstractSocket::SocketError error);
    void socketError(QAbstractSocket::SocketError error);
    void loginRequestor(); // only slot to be connected to unauthenticated sockets
    bool authenticate(QTcpSocket* socket, QString username, QString password);

};

#endif // NETWORK_H
