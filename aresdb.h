#ifndef ARESDB_H
#define ARESDB_H

#include <QObject>
#include <QtSql>
#include <QList>
#include <QTextStream>
#include <QtCoreVersion>

class AresDb : public QObject
{
    Q_OBJECT
public:
    explicit AresDb(QObject *parent = nullptr);
    QSqlDatabase db;
    bool authenticate(QString username, QString password);
    bool init();

public slots:
    QSqlQuery exec(QString sql);
    QJsonObject registerUser(QString username, QString password);

    // selects
    QJsonValue selectUserId(QString username);
    QJsonObject selectUserSystemId(QString user_id);
    bool userExists(QString username);

    // multi selects
    QJsonArray getPlanets(QString star_id);
    QJsonObject getStar(QString star_id);
    QJsonObject getSystem(QString id);


    // inserts
    QVariant insertUser(QString username, QString password);
    QVariant insertSystem(QVariant sectorId, QString user_id = NULL);
    QVariant insertStar();
    QVariant insertTechTree(QString username);
    QVariant insertPlanet(QVariant star_id);
    QVariant insertPlanet(QVariant star_id, QVariant distance);
    QVariant insertShip(QVariant system_id, QString type, QVariant x, QVariant y, QString user_id = QString());

    // updates
    // true on successful update.
    bool updateUserPassword(QString username, QString password, QString newPassword);

private:
    QVariant findSector(); // only used during player registration
    QVariant insertSector(); // dangerous and must be used wisely.
    double doubleRandomRange(double min, double max);

signals:
    void connected();
    void criticalErrorMessage(QString message);
    void errorMessage(QString message);

};

#endif // ARESDB_H
