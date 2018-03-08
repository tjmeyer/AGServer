#include <QCoreApplication>
#include "network.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    network test;
    return a.exec();
}
