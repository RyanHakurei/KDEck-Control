#include "Service.hpp"

#include <QGuiApplication>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("galleon-deck"));
    QGuiApplication::setOrganizationName(QStringLiteral("galleon-deck"));
    galleon::Service service;
    return app.exec();
}
