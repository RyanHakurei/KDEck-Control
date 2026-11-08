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
    QApplication::setApplicationName(QStringLiteral("galleon-deck"));
    QApplication::setOrganizationName(QStringLiteral("galleon-deck"));
    QApplication::setDesktopFileName(QStringLiteral("org.kde.galleondeck"));
    KLocalizedString::setApplicationDomain("galleon-deck");

    KAboutData about(QStringLiteral("galleon-deck"), i18n("Galleon Deck"), QStringLiteral("1.3.0"),
                     i18n("Stream Deck support for the Corsair Galleon 100 SD"), KAboutLicense::MIT);
    KAboutData::setApplicationData(about);

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    QCommandLineOption checkOption(QStringLiteral("check"), QStringLiteral("Load the window and exit"));
    parser.addOption(checkOption);
    parser.process(app);
    about.processCommandLine(&parser);

    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    galleon::Controller controller;
    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    engine.addImageProvider(QStringLiteral("keys"), new galleon::KeyImageProvider(&controller));

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("org.kde.galleondeck", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    if (parser.isSet(checkOption))
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    return app.exec();
}
