#include <QCoreApplication>
#include "AresNetwork.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    AresNetwork run(12345);
    return a.exec();
}
