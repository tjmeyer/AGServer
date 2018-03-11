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
        case packetType::SYSTEM_REQUEST:

            /**********************************************
             * Packet breakdown:
             *
             *  {
             *      "type":6,
             *      "image_index":"INT",
             *      "user_id":"INT",
             *      "message":"success" OR "failed" OR "invalid",
             *      "obj_star":
             *      {
             *          "type":"INT",
             *          "radius":"FLOAT",
             *          "anomaly":"INT",
             *          "id":"INT"
             *      }, (only 1 star)
             *      "array_planets":
             *      [
             *          {
             *              "progress":"FLOAT",
             *              "name":"some string",
             *              "id":"INT",
             *              "level":"INT",
             *              "sprite_index":"INT",
             *              "type":"INT",
             *              "distance":"FLOAT",
             *              "radius":"FLOAT",
             *              "speed":"FLOAT"
             *          },
             *          { }, 1 ... n
             *      ]
             *  }
             *
             * *******************************************/
            QJsonObject obj_system;
            obj_system.insert(QString("type"), SYSTEM_REQUEST);
            if(packet.contains(QString("id")))
            {
                // get system information
                QString sql = "SELECT image_index, star_id, user_id FROM system WHERE id = " + packet.value("id").toString();
                out << "sql: " << sql << endl;
                QSqlQuery query = db.exec(sql);
                if(query.next())
                {
                    // get system information
                    QJsonValue image_index(query.value(0).toString());
                    QJsonValue star_id(query.value(1).toString());
                    QJsonValue user_id(query.value(2).toString());
                    obj_system.insert(QString("image_index"), image_index);
                    obj_system.insert(QString("user_id"), user_id);

                    // get star information
                    sql = "SELECT type, radius, anomaly FROM star WHERE id = " + star_id.toString();
                    out << "sql: " << sql << endl;
                    query = db.exec(sql);
                    if(query.next())
                    {
                        QJsonObject star;

                        QJsonValue star_type(query.value(0).toString());
                        QJsonValue star_radius(query.value(1).toString());
                        QJsonValue star_anomaly(query.value(2).toString());
                        star.insert(QString("type"), star_type);
                        star.insert(QString("radius"), star_radius);
                        star.insert(QString("anomaly"), star_anomaly);
                        star.insert(QString("id"), star_id);

                        obj_system.insert(QString("obj_star"), star);
                    }

                    sql = "SELECT id, colony_lvl, name, sprite_index, speed, progress, type, radius, last_update, distance FROM planet WHERE star_id = " + star_id.toString();
                    out << "sql: " << sql << endl;
                    query = db.exec(sql);
                    QJsonArray planetArray;
                    while(query.next())
                    {
                        QJsonObject planet;
                        QJsonValue planet_id(query.value(0).toString());
                        QJsonValue planet_lvl(query.value(1).toString());
                        QJsonValue planet_name(query.value(2).toString());
                        QJsonValue planet_sprite(query.value(3).toString());
                        QJsonValue planet_speed(query.value(4).toDouble());
                        QJsonValue planet_last_progress(query.value(5).toDouble());
                        QJsonValue planet_type(query.value(6).toString());
                        QJsonValue planet_radius(query.value(7).toDouble());
                        QDateTime planet_last_update(query.value(8).toDateTime());
                        QJsonValue planet_distance(query.value(9).toDouble());

                        // speed is days per orbit.
                        // first calculate the amount of time that has passed since the last update.
                        // precision is up to seconds, no need for ms.
                        QDateTime timeNow = QDateTime::currentDateTimeUtc();
                        qint64 secondsDiff = planet_last_update.secsTo(timeNow);
                        double orbitTime = planet_speed.toDouble() * 24 * 3600; // convert to number of seconds for 1 orbit
                        double progress = secondsDiff / orbitTime; // percentage of how far we've moved in orbit since last update (.01 = 1% of a circle).
                        progress = planet_last_progress.toDouble() + progress;
                        while(progress >= 1.0000000)
                        {
                            progress -= 1.0000;
                        }

                        planet.insert(QString("progress"), QJsonValue(progress).toDouble());
                        planet.insert(QString("name"), planet_name);
                        planet.insert(QString("id"), planet_id);
                        planet.insert(QString("level"), planet_lvl);
                        planet.insert(QString("sprite_index"), planet_sprite);
                        planet.insert(QString("type"), planet_type);
                        planet.insert(QString("distance"), planet_distance);
                        planet.insert(QString("radius"), planet_radius);
                        planet.insert(QString("speed"), planet_speed);

                        // add planet object to the array of planets.
                        planetArray.append(QJsonValue(planet));

                        // update database to contain new planet position
                        sql = "UPDATE planet SET progress = " + QString::number(progress) + ", last_update = " +
                                QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd hh:mm:ss") + " WHERE id = " + planet_id.toString();
                        db.exec(sql);
                    }
                    // add planet array to the system obj
                    obj_system.insert(QString("array_planet"), QJsonValue(planetArray));

                    // TO DO: add ship array to obj_system

                    // append a "success" message for client ease of parsing
                    obj_system.insert(QString("message"), QJsonValue(QString("success")));
                }
                else
                {
                    obj_system.insert(QString("message"), QJsonValue(QString("failed")));
                }

                // send response
                out << QJsonDocument(obj_system).toJson() << endl;
                socket->write(QJsonDocument(obj_system).toJson()+'\0');
            }
            else
            {
                // in the event that no id field was returned when requesting the system data
                QJsonObject invalidResponse;
                invalidResponse.insert(QString("message"), QJsonValue(QString("invalid")));
                socket->write(QJsonDocument(invalidResponse).toJson());
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
        out << "Authenticating: \'" << username << "\'" << endl;
        QSqlQuery query = db.exec("SELECT password FROM users WHERE username = '" + username +"'");
        if(query.next())
        {
            out << "\tUser \'" << username << "\' located" << endl;
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
                out << doc.toJson() << endl;
                socket->write(doc.toJson());
            }
            else
            {
                out << "\tInvalid password for \'" + username + "\'" << endl;
            }
        }
    }
    return valid;
}
