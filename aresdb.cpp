#include "AresDb.h"
#include <QRandomGenerator>

AresDb::AresDb(QObject *parent) : QObject(parent)
{

}

bool AresDb::init()
{
    if(!db.isOpen() || !db.isValid())
    {
        db = QSqlDatabase::addDatabase("QMYSQL");
        db.setHostName("den1.mysql6.gear.host");
        db.setDatabaseName("aresdb");
        db.setUserName("aresdb");
        db.setPassword("Ma6sf8JR!?L4");
        db.setPort(3306);
        db.setConnectOptions("MYSQL_OPT_RECONNECT=1");
        if(!db.open())
        {
            emit criticalErrorMessage("Database failed to open: " + db.lastError().text());
            exit(1);
        }
    }

    // final check on db validity
    if(db.isValid())
    {
        emit connected();
        return true;
    }
    else
    {
        return false;
    }
}

QSqlQuery AresDb::exec(QString sql)
{
    QSqlQuery query;
    if(db.isValid())
    {
        query = db.exec(sql);
    }
    else
    {
        emit errorMessage(QString("Could not execute sql: " + db.lastError().text()));
        if(init())
        {
            query = db.exec(sql);
        }
        else
        {
            emit criticalErrorMessage(QString("Could not reinitialize database, exiting: " + db.lastError().text()));
            exit(1);
        }
    }
    return query;
}

// REGISTER USER
// Sets up everything a new user will need to play the game:
//  Resources
//  a system to start in
//  first colony
//  etc
//
// Compare to "insertUser" which only inserts a user into the db.
QJsonObject AresDb::registerUser(QString username, QString password)
{
    QTextStream debug(stderr);
    QVariant userId = insertUser(username, password);
    QJsonObject information;
    if(!userId.isNull()) // will be null if the username already exists
    {
        QVariant sectorId = findSector();
        QVariant systemId = insertSystem(sectorId, userId.toString());
        insertTechTree(username);
        information.insert(QString("username"), QJsonValue(true));
        information.insert(QString("user_id"), QJsonValue(userId.toDouble()));
        information.insert(QString("system_id"), QJsonValue(systemId.toDouble()));
        information.insert(QString("sector_id"), QJsonValue(sectorId.toDouble()));
    }
    else
    {
        debug << "\tregistered username is null when inserted." << endl;
        information.insert(QString("username"), QJsonValue(false)); // the username is taken
    }
    return information;
}

QJsonValue AresDb::selectUserId(QString username)
{
    QJsonValue id;
    QSqlQuery query = exec(QString("SELECT id FROM users WHERE username = '"+username+"'"));
    if(query.next())
    {
        id = QJsonValue(query.value(0).toString());
    }
    return id;
}

QJsonObject AresDb::selectUserSystemId(QString user_id)
{
    QTextStream debug(stderr);
    debug << "selecting user system id for user " << user_id << endl;
    QString sql = "SELECT id, sector_id FROM system WHERE user_id = " + user_id;
    QSqlQuery query = exec(sql);
    QJsonObject packet;
    if(query.next())
    {
        packet.insert(QString("system_id"), query.value(0).toString());
        packet.insert(QString("sector_id"), query.value(1).toString());
        debug << QJsonDocument(packet).toJson() << endl;
    }
    return packet;
}

bool AresDb::userExists(QString username)
{
    bool exists = false;
    QString sql = "SELECT id FROM users WHERE username = '"+username+"'";
    QSqlQuery query = exec(sql);
    if(query.next())
    {
        exists = true;
    }
    return exists;
}

QJsonArray AresDb::getPlanets(QString star_id)
{
    QJsonArray planetArray;
    QString sql = "SELECT id, colony_lvl, name, sprite_index, speed, progress, type, radius, last_update, distance FROM planet WHERE star_id = " + star_id;
    QSqlQuery query = exec(sql);
    while(query.next())
    {
        QJsonObject planet;
        QJsonValue planet_id(query.value(0).toString());
        QJsonValue planet_lvl(query.value(1).toString());
        QJsonValue planet_name(query.value(2).toString());
        QJsonValue planet_sprite(query.value(3).toString());
        QJsonValue planet_speed(query.value(4).toDouble()); // this is in DAYS per orbit, an INT is stored in the db
        QJsonValue planet_last_progress(query.value(5).toDouble());
        QJsonValue planet_type(query.value(6).toString());
        QJsonValue planet_radius(query.value(7).toDouble());
        QDateTime planet_last_update(query.value(8).toDateTime());
        QJsonValue planet_distance(query.value(9).toDouble());
        planet_last_update.setTimeSpec(Qt::UTC);

        // get offset from database retrieved datetime
        QDateTime timeNow = QDateTime::currentDateTimeUtc();
        qint64 secondsDiff = planet_last_update.secsTo(timeNow);
        double orbitTime = planet_speed.toInt() * 24 * 3600; // convert to number of seconds for 1 orbit
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
        sql = "UPDATE planet SET progress = " + QString::number(progress) + ", last_update = '" +
                QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd hh:mm:ss") + "' WHERE id = " + planet_id.toString();
        exec(sql);
    }
    return planetArray;
}

QJsonObject AresDb::getStar(QString star_id)
{
    QJsonObject star;
    QString sql = "SELECT type, radius, anomaly FROM star WHERE id = " + star_id;
    QSqlQuery query = exec(sql);
    if(query.next())
    {
        QJsonValue star_type(query.value(0).toString());
        QJsonValue star_radius(query.value(1).toString());
        QJsonValue star_anomaly(query.value(2).toString());
        star.insert(QString("type"), star_type);
        star.insert(QString("radius"), star_radius);
        star.insert(QString("anomaly"), star_anomaly);
        star.insert(QString("id"), star_id);
    }
    return star;
}

QJsonObject AresDb::getSystem(QString id)
{
    QTextStream debug(stderr);
    debug << "Getting system for user: " << id << endl;
    QJsonObject obj_system;
    // get system information
    QString sql = "SELECT image_index, star_id, user_id FROM system WHERE id = " + id;
    QSqlQuery query = exec(sql);

    if(query.next())
    {
        // get system information
        QJsonValue image_index(query.value(0).toString());
        QJsonValue star_id(query.value(1).toString());
        QJsonValue user_id(query.value(2).toString());
        obj_system.insert(QString("image_index"), image_index);
        obj_system.insert(QString("user_id"), user_id);
        debug << "\tretrieved system info\n";

        // get star information
        QJsonObject star = getStar(star_id.toString());
        obj_system.insert(QString("obj_star"), star);
        debug << "\tretrieved star info\n";

        // add planet array to the system obj
        QJsonArray planetArray = getPlanets(star_id.toString());
        obj_system.insert(QString("array_planet"), QJsonValue(planetArray));
        debug << "\tretrieved planets info\n";

        //TO DO: add ship array to obj_system
    }
    return obj_system;
}

QVariant AresDb::insertUser(QString username, QString password)
{
    QTextStream out(stdout);
    if(userExists(username))
    {
        emit errorMessage("That username already exists.");
        return QVariant(); // a NULL variant
    }

    out << "User does not exist in insertUser()\n";
    QString sql = "INSERT INTO users (username, password) VALUES ('" + username + "', '" +
            QString(QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha1).toHex()) + "')";
    QSqlQuery query = exec(sql);
    return query.lastInsertId();
}

QVariant AresDb::insertSystem(QVariant sectorId, QString user_id)
{
    QTextStream debug(stderr);
    int x = qrand() % 3000;
    int y = qrand() % 3000;
    // find an x and y and isn't occupied in the sector
    QString sql = "SELECT id FROM system WHERE x = "+
            QString::number(x) + " AND y = " +
            QString::number(y);
    QSqlQuery query = exec(sql);
    while(query.next())
    {
        x = qrand() % 3000;
        y = qrand() % 3000;
        sql = "SELECT id FROM system WHERE x = "+
                    QString::number(x) + " AND y = " +
                    QString::number(y);
        query = exec(sql);
    }

    // create star
    QVariant starId = insertStar();
    int planetNum = 0;
    if(user_id.isNull())
    {
        planetNum = qrand() % 9 + 1; // 1 to 9 planets for non-home systems
    }
    else
    {
        planetNum = qrand() % 3 + 6; // 6 to 8 planets for new player home systems
    }

    // generate planets
    double distanceMax = 1;
    double distanceMin = .2;
    double chosenDistance = doubleRandomRange(distanceMin, distanceMax);
    for(int i = 0; i < planetNum; i++)
    {
        insertPlanet(starId, QVariant(chosenDistance));
        distanceMin = chosenDistance;
        distanceMax *= 2;
        chosenDistance = doubleRandomRange(distanceMin, distanceMax);
    }

    int image_index = qrand() % 7;
    // are we adding a user or not?
    if(user_id.isNull())
    {
        sql = "INSERT INTO system (x, y, image_index, sector_id, star_id) "
              "VALUES (" + QString::number(x) + ", " +
                QString::number(y) + ", " +
                QString::number(image_index) + ", " +
                sectorId.toString() + ", " +
                starId.toString() + ")";
    }
    else
    {
        sql = "INSERT INTO system (x, y, image_index, sector_id, star_id, user_id) "
              "VALUES (" + QString::number(x) + ", " +
                QString::number(y) + ", " +
                QString::number(image_index) + ", " +
                sectorId.toString() + ", " +
                starId.toString() + ", " +
                user_id + ")";
    }
    debug << "Creating the following system:\n" << sql << endl;
    // create sql
    // exec sql
    query = exec(sql);
    // return system id
    return query.lastInsertId();
}

QVariant AresDb::insertStar()
{
    QString stellarClass;
    double max = 3000000;
    double classM = max * 0.76;
    double classK = max * 0.12 + classM;
    double classG = max * 0.075 + classK;
    double classF = max * 0.0303 + classG;
    double classA = max * 0.00925 + classF;
    double classB = max * 0.00425 + classA;
    // classO will the remaining slots: about 0.0012
    double roll = QRandomGenerator::global()->bounded(max); // out of 3 million
    if(roll < classM)
    {
        stellarClass = "M";
    }
    else if(roll < classK)
    {
        stellarClass = "K";
    }
    else if(roll < classG)
    {
        stellarClass = "G";
    }
    else if(roll < classF)
    {
        stellarClass = "F";
    }
    else if(roll < classA)
    {
        stellarClass = "A";
    }
    else if(roll < classB)
    {
        stellarClass = "B";
    }
    else
    {
        stellarClass = "O";
    }

    quint32 sunR = 432288;
    quint32 radius = QRandomGenerator::global()->bounded(0.007*sunR, 1800*sunR);

    roll = QRandomGenerator::global()->bounded(max);
    int anomaly = 0;
    if(roll / max < 0.05)
    {
        anomaly = QRandomGenerator::global()->bounded(1, 5);
    }
    QString sql = "INSERT INTO star (type, radius, anomaly) "
                  "VALUES ('" + stellarClass + "', " + QString::number(radius) + ", " + QString::number(anomaly) + ")";
    QSqlQuery query = exec(sql);
    return query.lastInsertId();
}

QVariant AresDb::insertTechTree(QString username)
{
    QString sql = "SELECT id FROM users WHERE username = '"+username+"'";
    QSqlQuery query = exec(sql);
    QVariant userId;
    if(query.next())
    {
        userId = query.value(0);
    }
    sql = "INSERT INTO tech (user_id) VALUES (" + userId.toString() + ")";
    query = exec(sql);
    return query.lastInsertId();
}
QVariant AresDb::insertPlanet(QVariant star_id, QVariant distance)
{
    QVariant speed = qrand() % 1000 + 1;
    QVariant type = qrand() % 3 + 1;
    QVariant progress = doubleRandomRange(0.01, 0.99);
    QVariant radius = doubleRandomRange(1000.0, 50000.0);
    QVariant sprite_index;
    switch (type.toInt()) {
    case 1:
        sprite_index = qrand() % 5;
        break;
    case 2:
        sprite_index = qrand() % 8;
        break;
    case 3:
        sprite_index = qrand() % 11;
        break;
    default:
        sprite_index = 0;
        break;
    }
    QString sql = "INSERT INTO planet (sprite_index, speed, progress, type, radius, star_id, distance)"
          "VALUES ("+QString::number(sprite_index.toInt())+", "+
            QString::number(speed.toInt()) + ", " +
            QString::number(progress.toDouble()) + ", " +
            QString::number(type.toInt()) + ", " +
            QString::number(radius.toDouble()) + ", " +
            QString::number(star_id.toInt()) + ", " +
            QString::number(distance.toDouble())+ ")";
    QSqlQuery query = exec(sql);
    return query.lastInsertId();
}

QVariant AresDb::insertPlanet(QVariant star_id)
{
    QVariant distance = doubleRandomRange(0.1, 50);
    return insertPlanet(star_id, distance);
}

QVariant AresDb::insertSector()
{
    quint16 max = 3000;
    quint16 x = rand() % max; // pick random coordinates
    quint16 y = rand() % max;
    QString sql = "SELECT id FROM sector WHERE x = "+QString::number(x)+" AND y = "+QString::number(y);
    QSqlQuery query = exec(sql);

    while(query.next()) // just checking to see if one exists, we don't care about the id, yet.
    {
        // reguess
        x = rand() % max;
        y = rand() % max;
        sql = "SELECT id FROM sector WHERE x = "+QString::number(x)+" AND y = "+QString::number(y);
        query = exec(sql);
    }

    // x and y will now be unique
    // fortunately this should not be called often as it is slow
    max = 60;
    quint16 min = 30;
    quint16 image = qrand() % 7;
    quint8 capacity = qrand() % (max - min) + min;
    sql = "INSERT INTO sector (x, y, image_index, capacity) "
            "VALUES ("+
            QString::number(x)+", "+
            QString::number(y)+", "+
            QString::number(image)+", "+
            QString::number(capacity)+")";
    query = exec(sql);

    return query.lastInsertId();
}

double AresDb::doubleRandomRange(double min, double max)
{
    double f = (double)qrand() / RAND_MAX;
    return min + f * (max - min);
}

// FIND SECTOR
// used to find a random sector that is not full.
// if none are found, then it creates one and
// returns the id.
QVariant AresDb::findSector()
{
    QTextStream debug(stderr);
    QString sql = "SELECT MAX(id), capacity FROM sector";
    QSqlQuery sectorQuery = exec(sql);
    QVariant sectorId;
    QVariant capacity;
    bool createNewSector = false;
    if(sectorQuery.next()) // we only want one, even if there is more, so don't use while
    {
        capacity = sectorQuery.value(1);
        sectorId = sectorQuery.value(0);
        sql = "SELECT COUNT(id) FROM system WHERE sector_id = " + sectorId.toString();
        QSqlQuery systemQuery = exec(sql);
        if(systemQuery.next())
        {
            int count = systemQuery.value(0).toInt();
            debug << "\tcomparing count: " << count << " to capacity: " << capacity.toDouble() << endl;
            if(count >= capacity.toDouble())
            {
                createNewSector = true;
            }
        }
    }
    if(createNewSector)
    {
        debug << "CREATING NEW SECTOR!" << endl;
        sectorId = insertSector();
    }
    return sectorId;
}

QVariant AresDb::insertShip(QVariant system_id, QString type, QVariant x, QVariant y, QString user_id)
{

}

bool AresDb::updateUserPassword(QString username, QString password, QString newPassword)
{
    bool success = false;
    if(authenticate(username, password))
    {
        QString sql = "UPDATE users SET password = '" + QCryptographicHash::hash(newPassword.toUtf8(), QCryptographicHash::Sha1).toHex() +
                "' WHERE username = '" + username +"'";
        QSqlQuery query = exec(sql);
        success = true;
    }
    return success;
}

bool AresDb::authenticate(QString username, QString password)
{
    QTextStream debug(stderr);
    bool valid = false;
    QSqlQuery query = exec("SELECT password FROM users WHERE username = '" + username + "'");
    if(query.next())
    {
        debug << "\tDATABASE PASS: \t" << query.value(0).toString() << endl;
        debug << "\tUSER PROVIDED: \t" << QString(QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha1).toHex()) <<  endl;
        if(query.value(0).toString() == QString(QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha1).toHex()))
        {
            valid = true;
        }
    }
    return valid;
}

