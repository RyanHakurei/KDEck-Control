#include "Service.hpp"

#include <QGuiApplication>

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME"))
        qputenv("QT_QPA_PLATFORMTHEME", QByteArrayLiteral("kde"));
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("kdeck-control"));
    QGuiApplication::setOrganizationName(QStringLiteral("kdeck-control"));
    kdeck::Service service;
    return app.exec();
}
