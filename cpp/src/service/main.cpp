#include "Service.hpp"

#include <QGuiApplication>

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME"))
        qputenv("QT_QPA_PLATFORMTHEME", QByteArrayLiteral("kde"));
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("galleon-deck"));
    QGuiApplication::setOrganizationName(QStringLiteral("galleon-deck"));
    galleon::Service service;
    return app.exec();
}
