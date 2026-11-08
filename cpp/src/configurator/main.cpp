#include "Controller.hpp"

#include <KAboutData>
#include <KIconTheme>
#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>

int main(int argc, char *argv[])
{
    // A shell that did not inherit the Plasma session still has to read the
    // fonts selected in System Settings. Do not replace a theme the user set.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME"))
        qputenv("QT_QPA_PLATFORMTHEME", QByteArrayLiteral("kde"));
    KIconTheme::initTheme();
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("kdeck-control"));
    QApplication::setOrganizationName(QStringLiteral("kdeck-control"));
    QApplication::setDesktopFileName(QStringLiteral("org.kde.kdeckcontrol"));
    KLocalizedString::setApplicationDomain("kdeck-control");

    KAboutData about(QStringLiteral("kdeck-control"), i18n("KDEck Control"), QStringLiteral("1.3.0"),
                     i18n("Stream Deck support for the Corsair Galleon 100 SD"), KAboutLicense::MIT);
    KAboutData::setApplicationData(about);

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    QCommandLineOption checkOption(QStringLiteral("check"), QStringLiteral("Load the window and exit"));
    parser.addOption(checkOption);
    parser.process(app);
    about.processCommandLine(&parser);

    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    kdeck::Controller controller;
    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    engine.addImageProvider(QStringLiteral("keys"), new kdeck::KeyImageProvider(&controller));

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("org.kde.kdeckcontrol", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    if (parser.isSet(checkOption))
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    return app.exec();
}
