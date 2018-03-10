#include <QCoreApplication>
#include "network.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    network run;
    return a.exec();
}
