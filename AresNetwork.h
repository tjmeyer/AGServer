#ifndef ARESNETWORK_H
#define ARESNETWORK_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include "AresNetworkMessages.h"
#include "AresDb.h"

class AresNetwork : public QObject
{
    Q_OBJECT
public:
    explicit AresNetwork(int port, QObject *parent = nullptr);
    QTcpServer* server;
    AresDb* aresdb;
    QMap<QTcpSocket*, QHostAddress> connectedUsers;
    QMap<QTcpSocket*, QString> authUsers; // key: socket id, value: username (for db lookups)


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
    // if authentication is successful. Returns a response packet
    QJsonObject authenticate(QTcpSocket* socket, QString username, QString password);

    void serverError(QAbstractSocket::SocketError error);
    void socketError(QAbstractSocket::SocketError error);
    void databaseError(QString message);
    void criticalDatabaseError(QString message);

};

#endif // NETWORK_H
