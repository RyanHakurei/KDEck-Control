#include "Apps.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace galleon {
namespace {

QString unescapeDesktop(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\\') && i + 1 < text.size()) {
            const QChar next = text.at(++i);
            if (next == QLatin1Char('s'))
                out += QLatin1Char(' ');
            else if (next == QLatin1Char('n'))
                out += QLatin1Char('\n');
            else if (next == QLatin1Char('t'))
                out += QLatin1Char('\t');
            else if (next == QLatin1Char('r'))
                out += QLatin1Char('\r');
            else
                out += next;
        } else {
            out += text.at(i);
        }
    }
    return out;
}

QStringList dataDirs()
{
    QStringList dirs;
    const QString home = qEnvironmentVariable("XDG_DATA_HOME");
    dirs.append(home.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/share") : home);
    QString extra = qEnvironmentVariable("XDG_DATA_DIRS");
    if (extra.isEmpty())
        extra = QStringLiteral("/usr/local/share:/usr/share");
    dirs += extra.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    return dirs;
}

QStringList desktopNames()
{
    QStringList names;
    const QString raw = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    for (const QString &part : raw.split(QRegularExpression(QStringLiteral("[:;]")), Qt::SkipEmptyParts))
        names.append(part.trimmed().toLower());
    return names;
}

bool listedIn(const QString &field, const QStringList &names)
{
    if (field.isEmpty() || names.isEmpty())
        return false;
    for (const QString &part : field.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        if (names.contains(part.trimmed().toLower()))
            return true;
    }
    return false;
}

bool tryExecOk(const QString &tryExec)
{
    if (tryExec.isEmpty())
        return true;
    if (tryExec.startsWith(QLatin1Char('/')))
        return QFileInfo::exists(tryExec);
    return !QStandardPaths::findExecutable(tryExec).isEmpty();
}

struct DesktopFields {
    QString name;
    QString localized;
    QString icon;
    QString exec;
    QString type;
    QString onlyShow;
    QString notShow;
    QString tryExec;
    bool noDisplay = false;
    bool hidden = false;
    bool found = false;
};

DesktopFields readDesktop(const QString &path)
{
    DesktopFields fields;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fields;
    const QString locale = QLocale().name();
    const QString lang = locale.section(QLatin1Char('_'), 0, 0);
    bool inEntry = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('['))) {
            if (inEntry)
                break;
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inEntry)
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = unescapeDesktop(line.mid(eq + 1).trimmed());
        if (key == QLatin1String("Name"))
            fields.name = value;
        else if (key == QStringLiteral("Name[") + locale + QStringLiteral("]"))
            fields.localized = value;
        else if (fields.localized.isEmpty() && key == QStringLiteral("Name[") + lang + QStringLiteral("]"))
            fields.localized = value;
        else if (key == QLatin1String("Icon"))
            fields.icon = value;
        else if (key == QLatin1String("Exec"))
            fields.exec = value;
        else if (key == QLatin1String("Type"))
            fields.type = value;
        else if (key == QLatin1String("OnlyShowIn"))
            fields.onlyShow = value;
        else if (key == QLatin1String("NotShowIn"))
            fields.notShow = value;
        else if (key == QLatin1String("TryExec"))
            fields.tryExec = value;
        else if (key == QLatin1String("NoDisplay"))
            fields.noDisplay = value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
        else if (key == QLatin1String("Hidden"))
            fields.hidden = value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
    }
    fields.found = inEntry || !fields.name.isEmpty() || !fields.exec.isEmpty();
    return fields;
}

QString shownName(const DesktopFields &fields)
{
    return fields.localized.isEmpty() ? fields.name : fields.localized;
}

bool showInMenu(const DesktopFields &fields)
{
    if (!fields.found || fields.type != QLatin1String("Application"))
        return false;
    if (fields.noDisplay || fields.hidden || fields.exec.isEmpty() || shownName(fields).isEmpty())
        return false;
    if (!tryExecOk(fields.tryExec))
        return false;
    const QStringList desktops = desktopNames();
    if (listedIn(fields.notShow, desktops))
        return false;
    if (!fields.onlyShow.isEmpty() && !listedIn(fields.onlyShow, desktops))
        return false;
    return true;
}

QString findDesktopFile(const QString &app)
{
    if (app.isEmpty())
        return {};
    if (app.startsWith(QLatin1Char('/')))
        return QFileInfo::exists(app) ? app : QString();
    const QString file = app.endsWith(QLatin1String(".desktop")) ? app : app + QStringLiteral(".desktop");
    for (const QString &dir : dataDirs()) {
        const QString path = QDir(dir).filePath(QStringLiteral("applications/") + file);
        if (QFileInfo::exists(path))
            return path;
    }
    return {};
}

QStringList splitCommand(const QString &text)
{
    QStringList out;
    QString token;
    bool quoted = false;
    bool escape = false;
    for (const QChar ch : text) {
        if (escape) {
            token += ch;
            escape = false;
            continue;
        }
        if (ch == QLatin1Char('\\')) {
            escape = true;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            quoted = !quoted;
            continue;
        }
        if (!quoted && ch.isSpace()) {
            if (!token.isEmpty()) {
                out.append(token);
                token.clear();
            }
            continue;
        }
        token += ch;
    }
    if (!token.isEmpty())
        out.append(token);
    return out;
}

QStringList argvFromExec(const QString &exec, const QString &icon, const QString &args)
{
    QStringList argv;
    for (QString token : splitCommand(exec)) {
        if (token == QLatin1String("%i")) {
            if (!icon.isEmpty()) {
                argv.append(QStringLiteral("--icon"));
                argv.append(icon);
            }
            continue;
        }
        token.replace(QLatin1String("%%"), QStringLiteral("\u0001"));
        const QStringList codes = {
            QStringLiteral("%f"), QStringLiteral("%F"), QStringLiteral("%u"), QStringLiteral("%U"),
            QStringLiteral("%d"), QStringLiteral("%D"), QStringLiteral("%n"), QStringLiteral("%N"),
            QStringLiteral("%c"), QStringLiteral("%k"), QStringLiteral("%v"), QStringLiteral("%m"),
        };
        for (const QString &code : codes)
            token.remove(code);
        token.replace(QLatin1Char('\u0001'), QLatin1Char('%'));
        if (!token.isEmpty())
            argv.append(token);
    }
    argv += splitCommand(args);
    if (argv.isEmpty() || argv.first().isEmpty())
        return {};
    return argv;
}

} // namespace

QList<DesktopApp> installedApplications()
{
    QList<DesktopApp> apps;
    QStringList seen;
    for (const QString &dir : dataDirs()) {
        QDir folder(QDir(dir).filePath(QStringLiteral("applications")));
        const QStringList files = folder.entryList({QStringLiteral("*.desktop")}, QDir::Files);
        for (const QString &file : files) {
            const QString id = QFileInfo(file).completeBaseName();
            if (seen.contains(id))
                continue;
            const DesktopFields fields = readDesktop(folder.filePath(file));
            if (!showInMenu(fields))
                continue;
            seen.append(id);
            apps.append(DesktopApp{id, shownName(fields), fields.icon});
        }
    }
    std::sort(apps.begin(), apps.end(), [](const DesktopApp &a, const DesktopApp &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return apps;
}

QString applicationName(const QString &app)
{
    if (app.isEmpty())
        return {};
    if (app.startsWith(QLatin1Char('/')) && !app.endsWith(QLatin1String(".desktop")))
        return QFileInfo(app).fileName();
    const QString path = findDesktopFile(app);
    if (path.isEmpty())
        return {};
    const DesktopFields fields = readDesktop(path);
    return shownName(fields);
}

QStringList applicationArgv(const QString &app, const QString &args)
{
    if (app.isEmpty())
        return {};
    if (app.startsWith(QLatin1Char('/')) && !app.endsWith(QLatin1String(".desktop"))) {
        if (!QFileInfo::exists(app))
            return {};
        QStringList argv{app};
        argv += splitCommand(args);
        return argv;
    }
    const QString path = findDesktopFile(app);
    if (path.isEmpty())
        return {};
    const DesktopFields fields = readDesktop(path);
    if (fields.exec.isEmpty())
        return {};
    return argvFromExec(fields.exec, fields.icon, args);
}

} // namespace galleon
