#pragma once

#include <QString>
#include <QStringList>
#include <QList>

namespace galleon {

// One installed .desktop application. `id` is the file name without ".desktop".
// `icon` is a theme name, or an absolute path when the desktop file gives one.
struct DesktopApp {
    QString id;
    QString name;
    QString icon;
};

// Applications shown in the desktop menu, sorted by name. User overrides win.
QList<DesktopApp> installedApplications();

// Translated name for a desktop id, an absolute .desktop path, or an executable
// path. Empty when nothing by that name is installed.
QString applicationName(const QString &app);

// Argv that starts `app` directly, with `args` split and appended. Field codes
// in the desktop Exec line are removed. Empty when the application cannot be
// found. This does not go through a shell.
QStringList applicationArgv(const QString &app, const QString &args);

} // namespace galleon
