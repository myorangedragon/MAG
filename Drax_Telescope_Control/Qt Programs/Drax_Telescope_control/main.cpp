#include "draxtelescopecontrol.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    DraxTelescopeControl w;
    w.show();
    return QApplication::exec();
}
