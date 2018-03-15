#ifndef NETWORK_H
#define NETWORK_H

#include <QObject>
#include <QtSql>
#include <QtCoreVersion>
#include <QTextStream>
#include <QTcpServer>
#include <QTcpSocket>
#include <QList>
#include "network_messages.h"

class network : public QObject
{
    Q_OBJECT
public:
    explicit network(QObject *parent = nullptr);
    QSqlDatabase db;
    QTcpServer* server;
    QMap<QTcpSocket*, QHostAddress> connectedUsers;
    QMap<QTcpSocket*, QString> authUsers; // key: socket id, value: username (for db lookups)
    bool databaseInit();
    QSqlQuery dbExec(QString sql);

private:


signals:
    void userLoggedIn(QString username);
    void userLoggedOut(QString username);

public slots:
    // connection and disconnection handlers
    void userConnected();
    void userDisconnected();

    // packet receivers and switches
    void loginRequestor(); // only slot to be connected to unauthenticated sockets
    void authReader(); // only called by authenticated users for gameplay use

    // moves a client socket from the loginRequestor slot to the authReader slot
    // if authentication is successful
    bool authenticate(QTcpSocket* socket, QString username, QString password);

    // database wrappers
    QJsonArray getPlanets(QString star_id);
    QJsonObject getStar(QString star_id);
    QJsonObject getSystem(QString id);

    void serverError(QAbstractSocket::SocketError error);
    void socketError(QAbstractSocket::SocketError error);

};

#endif // NETWORK_H
