#include "AresNetwork.h"

AresNetwork::AresNetwork(int port, QObject *parent) : QObject(parent)
{
    QTextStream debug(stderr);

    // START -- database
    aresdb = new AresDb(this);
    aresdb->init();

    connect(aresdb, SIGNAL(errorMessage(QString)), this, SLOT(databaseError(QString)));
    connect(aresdb, SIGNAL(criticalErrorMessage(QString)), this, SLOT(criticalDatabaseError(QString))); // will cause abort

    // START -- server
    server = new QTcpServer(this);
    server->listen(QHostAddress::Any, port);
    if(!server->isListening())
    {
        debug << "ERROR: Server failed to start on port " << port << "." << endl;
        exit(0);
    }
    else
    {
        debug << "Server started successfully on port: " << port << "." << endl;
    }

    // establish server connections
    connect(server, SIGNAL(newConnection()), this, SLOT(userConnected()));
    connect(server, SIGNAL(acceptError(QAbstractSocket::SocketError)), this, SLOT(serverError(QAbstractSocket::SocketError)));
}

// authenticated users can use this after logging in.
void AresNetwork::authReader()
{
    QTextStream debug(stderr);
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if(socket->isValid())
    {
        QString data = socket->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
        QJsonObject packet = doc.object();
        switch (packet.value("type").toInt())
        {
        case packetType::CONNECTION_CHECK:
            // ping back
            packet.empty();
            debug << "connection check\n";
            packet.insert(QString("type"), CONNECTION_CHECK);
            socket->write(QJsonDocument(packet).toJson());
            break;
        case packetType::SYSTEM_REQUEST:
            debug << "SYSTEM REQUEST START\n";
            if(packet.contains(QString("id")))
            {
                debug << "Getting system for id " << packet.value("id").toString() << endl;
                QJsonObject system_packet = aresdb->getSystem(packet.value(QString("id")).toString());
                system_packet.insert(QString("type"), SYSTEM_REQUEST);
                system_packet.insert(QString("message"), QJsonValue(QString("success")));
                socket->write(QJsonDocument(system_packet).toJson());
            }
            else
            {
                // in the event that no id field was returned when requesting the system data
                QJsonObject invalidResponse;
                invalidResponse.insert(QString("message"), QJsonValue(QString("failed")));
                socket->write(QJsonDocument(invalidResponse).toJson());
            }
            break;
        case packetType::GET_USER_SYSTEM_ID:
            if(packet.contains(QString("user_id")))
            {
                debug << "\tGET_USER_SYSTEM_ID\n";
                packet = aresdb->selectUserSystemId(packet.value(QString("user_id")).toString());
                debug << "\t\tid = " << packet.value(QString("system_id")).toString() << endl;
                packet.insert(QString("type"), GET_USER_SYSTEM_ID);
                socket->write(QJsonDocument(packet).toJson());
            }
            break;
        }
    }
}

void AresNetwork::userConnected()
{
    QTextStream debug(stderr);
    // get next incoming connection
    if(server->hasPendingConnections())
    {
        QTcpSocket* socket = server->nextPendingConnection();
        if(!connectedUsers.contains(socket))
        {
            connectedUsers.insert(socket, socket->peerAddress());
            debug << "New Connection: " << socket->peerAddress().toString() << endl;

            connect(socket, SIGNAL(disconnected()), this, SLOT(userDisconnected()));
            connect(socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(socketError(QAbstractSocket::SocketError)));
            connect(socket, SIGNAL(readyRead()), this, SLOT(loginRequestor()));
        }
    }

}

void AresNetwork::userDisconnected()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if(socket->isValid())
    {

        if(connectedUsers.remove(socket))
        {
            disconnect(socket, SIGNAL(readyRead()), this, SLOT(loginRequestor()));
        }
        if(authUsers.contains(socket))
        {
            emit userLoggedOut(authUsers.value(socket));
            authUsers.remove(socket);
            disconnect(socket, SIGNAL(readyRead()), this, SLOT(authReader()));
        }
        disconnect(socket, SIGNAL(disconnected()), this, SLOT(userDisconnected()));
        disconnect(socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(socketError(QAbstractSocket::SocketError)));
        socket->deleteLater();
    }
}

void AresNetwork::serverError(QAbstractSocket::SocketError error)
{
    QTextStream debug(stderr);
    QString message = "ERROR: ";
    switch (error) {
    case QAbstractSocket::ConnectionRefusedError:
        message += "The connection was refused by the peer (or timed out).";
        break;
    case QAbstractSocket::SocketAccessError:
        message += "The socket operation failed because the application lacked the required privileges.";
        break;
    default:
        message += "An unknown socket error occured when attempting to connect.";
        break;
    }
    debug << message << endl;
}

void AresNetwork::socketError(QAbstractSocket::SocketError error)
{
    QTextStream debug(stderr);
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    switch (error)
    {
    case QAbstractSocket::RemoteHostClosedError:
        debug << "SOCKET CLOSED:" << socket->peerAddress().toString() << endl;
        break;
    default:
        debug << "SOCKET ERROR: " << error << endl;
        break;
    }
}

void AresNetwork::databaseError(QString message)
{
    QTextStream debug(stderr);
    debug << "DB ERROR: " << message << endl;
}

void AresNetwork::criticalDatabaseError(QString message)
{
    QTextStream debug(stderr);
    debug << "CRITICAL DB ERROR: " << message << endl;
    abort();
}

void AresNetwork::loginRequestor()
{
    QTextStream debug(stderr);
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if(socket->isValid())
    {
        QString data = socket->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
        QJsonObject packet = doc.object();
        switch (packet.value("type").toInt())
        {
        case LOGIN_REQUEST:
            if(packet.contains("username") && packet.contains("password"))
            {
                packet = authenticate(socket, packet.value("username").toString(), packet.value("password").toString());
                debug << "sending auth packet:\n" << QJsonDocument(packet).toJson() << endl;
                socket->write(QJsonDocument(packet).toJson());
            }
            break;
        case CONNECTION_CHECK:
            // ping back
            packet.empty();
            packet.insert(QString("type"), CONNECTION_CHECK);
            socket->write(QJsonDocument(packet).toJson());
            break;
        case REGISTRATION_REQUEST:
            debug << "REGISTRATION_REQUEST Received.\n";
            if(packet.contains("username") && packet.contains("password"))
            {
                debug << "\tusername and password available\n";
                packet = aresdb->registerUser(packet.value("username").toString(), packet.value("password").toString());
                packet.insert(QString("type"), REGISTRATION_REQUEST);
                socket->write(QJsonDocument(packet).toJson());
            }
            else
            {
                packet.empty();
                packet.insert(QString("username"), QJsonValue(false));
                socket->write(QJsonDocument(packet).toJson());
            }
            break;
        default:
            debug << "default packet sort\n";
            // not authorized to ask anything else of the server until authentication
            packet.empty();
            packet.insert(QString("type"), QJsonValue(packetType::UNAUTHORIZED));
            socket->write(QJsonDocument(packet).toJson());
            break;
        }
    }
}

QJsonObject AresNetwork::authenticate(QTcpSocket *socket, QString username, QString password)
{
    QTextStream debug(stderr);
    QJsonObject packet; // response packet
    packet.insert(QString("type"), QJsonValue(LOGIN_REQUEST));
    if(aresdb->authenticate(username, password) && !authUsers.contains(socket))
    {
        authUsers.insert(socket, username);
        connect(socket, SIGNAL(readyRead()), this, SLOT(authReader()));
        disconnect(socket, SIGNAL(readyRead()), this, SLOT(loginRequestor()));
        packet.insert(QString("message"), QJsonValue(QString("Login Successful")));
        packet.insert(QString("user_id"), aresdb->selectUserId(username));
    }
    else
    {
        packet.insert(QString("message"), QJsonValue(QString("Username or Password incorrect.")));
        packet.insert(QString("user_id"), QJsonValue(-1));
    }
    return packet;
}
