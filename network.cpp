#include "network.h"

network::network(QObject *parent) : QObject(parent)
{
    QTextStream out(stdout);

    // START -- database
    db = QSqlDatabase::addDatabase("QMYSQL");
    db.setHostName("den1.mysql6.gear.host");
    db.setDatabaseName("aresdb");
    db.setUserName("aresdb");
    db.setPassword("Ma6sf8JR!?L4");
    db.setPort(3306);
    if(!db.open())
    {
        out << "ERROR: Connection to ARESDB not established" << endl;
        exit(0);
    }
    else
    {
        out << "ARESDB connection established." << endl;
    }

    // START -- server
    server = new QTcpServer(this);
    quint16 port = 12345;
    server->listen(QHostAddress::Any, port);
    if(!server->isListening())
    {
        out << "ERROR: Server failed to start on port " << port << "." << endl;
        exit(0);
    }
    else
    {
        out << "Server started successfully on port: " << port << "." << endl;
    }

    // establish server connections
    connect(server, SIGNAL(newConnection()), this, SLOT(userConnected()));
    connect(server, SIGNAL(acceptError(QAbstractSocket::SocketError)), this, SLOT(serverError(QAbstractSocket::SocketError)));
}

// authenticated users can use this after logging in.
void network::authReader()
{
    QTextStream out(stdout);
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
            packet.insert(QString("type"), CONNECTION_CHECK);
            socket->write(QJsonDocument(packet).toJson());
            break;
        case packetType::SYSTEM_MAP:
            QJsonObject packet;
            if(packet.contains("id"))
            {
                int id = packet.value("id").toInt();
            }
            break;
        }
    }
}

void network::userConnected()
{
    // get next incoming connection
    if(server->hasPendingConnections())
    {
        QTcpSocket* socket = server->nextPendingConnection();
        if(!connectedUsers.contains(socket))
        {
            connectedUsers.insert(socket, socket->peerAddress());
            QTextStream out(stdout);
            out << "New Connection: " << socket->peerAddress().toString() << endl;

            connect(socket, SIGNAL(disconnected()), this, SLOT(userDisconnected()));
            connect(socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(socketError(QAbstractSocket::SocketError)));
            connect(socket, SIGNAL(readyRead()), this, SLOT(loginRequestor()));
        }
    }

}

void network::userDisconnected()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if(socket->isValid())
    {
        QTextStream out(stdout);

        if(connectedUsers.remove(socket))
        {
            out << "Disconnected: " << socket->peerAddress().toString() << endl;
            disconnect(socket, SIGNAL(readyRead()), this, SLOT(loginRequestor()));
        }
        if(authUsers.contains(socket))
        {
            out << "Logged out: " << authUsers.value(socket) << endl;
            authUsers.remove(socket);
            QObject::disconnect(socket, &QTcpSocket::readyRead, this, &network::authReader);
        }
        disconnect(socket, SIGNAL(disconnected()), this, SLOT(userConnected()));
        disconnect(socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(socketError(QAbstractSocket::SocketError)));
        socket->deleteLater();
    }
}

void network::serverError(QAbstractSocket::SocketError error)
{
    QTextStream out(stdout);
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
    out << message << endl;
}

void network::socketError(QAbstractSocket::SocketError error)
{
    QTextStream out(stdout);
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    switch (error)
    {
    case QAbstractSocket::RemoteHostClosedError:
        out << "SOCKET CLOSED:" << socket->peerAddress().toString() << endl;
        break;
    default:
        out << "SOCKET ERROR: " << error << endl;
        break;
    }
}

void network::loginRequestor()
{
    QTextStream out(stdout);
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
                if(authenticate(socket, packet.value("username").toString(), packet.value("password").toString()))
                {
                    emit userLoggedIn(packet.value("username").toString());
                }
            }
            else
            {
                out << "Invalid login request received." << socket->peerAddress().toString() << endl;
            }
            break;
        case CONNECTION_CHECK:
            // ping back
            packet.empty();
            packet.insert(QString("type"), CONNECTION_CHECK);
            socket->write(QJsonDocument(packet).toJson());
            break;
        default:
            // not authorized to ask anything else of the server until authentication
            QJsonObject packet;
            packet.insert(QString("type"), QJsonValue(packetType::UNAUTHORIZED));
            socket->write(QJsonDocument(packet).toJson());
            break;
        }
    }
}

bool network::authenticate(QTcpSocket *socket, QString username, QString password)
{
    bool valid = false;
    if(!authUsers.contains(socket))
    {
        QTextStream out(stdout);
        out << "Authenticating: " << username << endl;
        QSqlQuery query = db.exec("SELECT password FROM users WHERE username = '" + username +"'");
        if(query.next())
        {
            out << "db value: " << query.value(0).toString() << endl;
            if(query.value(0).toString() == password)
            {
                valid = true;
                authUsers.insert(socket, username);
                QObject::connect(socket, &QTcpSocket::readyRead, this, &network::authReader);
                disconnect(socket, SIGNAL(readyRead()), this, SLOT(loginRequestor()));

                QJsonObject packet;
                packet.insert(QString("type"), QJsonValue(LOGIN_REQUEST));
                packet.insert(QString("message"), QJsonValue(QString("Login Successful")));
                QJsonDocument doc(packet);
                socket->write(doc.toJson());
            }
        }
    }
    return valid;
}
