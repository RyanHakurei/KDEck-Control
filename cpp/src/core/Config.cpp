#include "Types.hpp"

#include <toml++/toml.hpp>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace galleon {
namespace {

template <typename View>
QString fromToml(const View &node)
{
    if (!node)
        return {};
    if (auto value = node.template value<std::string>())
        return QString::fromStdString(*value);
    return {};
}

template <typename View>
int fromInt(const View &node, int fallback)
{
    if (!node)
        return fallback;
    if (auto value = node.template value<int64_t>())
        return static_cast<int>(*value);
    return fallback;
}

template <typename View>
bool fromBool(const View &node, bool fallback = false)
{
    if (!node)
        return fallback;
    if (auto value = node.template value<bool>())
        return *value;
    return fallback;
}

QColor parseColour(const QString &text, const QColor &fallback)
{
    if (text.size() == 7 && text.startsWith(QLatin1Char('#'))) {
        const QColor colour(text);
        if (colour.isValid())
            return colour;
    }
    return fallback;
}

Theme amberTheme()
{
    Theme theme;
    theme.name = QStringLiteral("amber");
    theme.bg = QColor(QStringLiteral("#1f1204"));
    theme.keyBg = QColor(QStringLiteral("#251505"));
    theme.fg = QColor(QStringLiteral("#f04a14"));
    theme.accent = QColor(QStringLiteral("#e0a93f"));
    theme.dim = QColor(QStringLiteral("#7f6a63"));
    theme.border = QColor(QStringLiteral("#5a2a0c"));
    theme.frame = QColor(QStringLiteral("#e0231a"));
    theme.tick = theme.accent;
    theme.radius = 6;
    theme.borderWidth = 3;
    theme.font = QStringLiteral("Quantico");
    theme.clockFont = QStringLiteral("Quantico:bold");
    return theme;
}

Theme builtin(const char *name, Theme theme)
{
    theme.name = QString::fromLatin1(name);
    return theme;
}

QHash<QString, Theme> builtinThemes()
{
    Theme amber = amberTheme();
    QHash<QString, Theme> themes;
    themes.insert(amber.name, amber);

    Theme cyber = amber;
    cyber.bg = QColor(QStringLiteral("#08040f"));
    cyber.keyBg = QColor(QStringLiteral("#0d0718"));
    cyber.fg = QColor(QStringLiteral("#00f0ff"));
    cyber.accent = QColor(QStringLiteral("#ff00aa"));
    cyber.dim = QColor(QStringLiteral("#6e6a86"));
    cyber.border = QColor(QStringLiteral("#2a1f45"));
    cyber.frame = cyber.fg;
    cyber.radius = 18;
    cyber.font = QStringLiteral("JetBrains Mono");
    cyber.clockFont = QStringLiteral("JetBrains Mono:bold");
    themes.insert(QStringLiteral("cyberpunk"), builtin("cyberpunk", cyber));

    Theme synth = amber;
    synth.bg = QColor(QStringLiteral("#12041f"));
    synth.keyBg = QColor(QStringLiteral("#1c0833"));
    synth.fg = QColor(QStringLiteral("#ff9e3d"));
    synth.accent = QColor(QStringLiteral("#ff2a6d"));
    synth.dim = QColor(QStringLiteral("#8a5fa8"));
    synth.border = QColor(QStringLiteral("#4a1d6e"));
    synth.frame = synth.accent;
    synth.radius = 24;
    synth.font = QStringLiteral("Orbitron");
    synth.clockFont = QStringLiteral("Orbitron:bold");
    themes.insert(QStringLiteral("synthwave"), builtin("synthwave", synth));

    Theme matrix = amber;
    matrix.bg = QColor(QStringLiteral("#000000"));
    matrix.keyBg = QColor(QStringLiteral("#020a03"));
    matrix.fg = QColor(QStringLiteral("#00ff41"));
    matrix.accent = QColor(QStringLiteral("#b6ff5c"));
    matrix.dim = QColor(QStringLiteral("#0f6b2a"));
    matrix.border = QColor(QStringLiteral("#0c3d18"));
    matrix.frame = matrix.fg;
    matrix.radius = 4;
    matrix.borderWidth = 2;
    matrix.font = QStringLiteral("FiraCode Nerd Font Mono");
    matrix.clockFont = QStringLiteral("FiraCode Nerd Font Mono:bold");
    themes.insert(QStringLiteral("matrix"), builtin("matrix", matrix));

    Theme arctic = amber;
    arctic.bg = QColor(QStringLiteral("#0b1018"));
    arctic.keyBg = QColor(QStringLiteral("#121a26"));
    arctic.fg = QColor(QStringLiteral("#d8e6f3"));
    arctic.accent = QColor(QStringLiteral("#88c0d0"));
    arctic.dim = QColor(QStringLiteral("#5b6b80"));
    arctic.border = QColor(QStringLiteral("#26344a"));
    arctic.frame = arctic.accent;
    arctic.radius = 22;
    arctic.borderWidth = 2;
    arctic.font = QStringLiteral("Rajdhani:semibold");
    arctic.clockFont = QStringLiteral("Rajdhani:bold");
    themes.insert(QStringLiteral("arctic"), builtin("arctic", arctic));

    Theme hud = amber;
    hud.bg = QColor(QStringLiteral("#050505"));
    hud.keyBg = QColor(QStringLiteral("#0d0d0d"));
    hud.fg = QColor(QStringLiteral("#ff8c1a"));
    hud.accent = QColor(QStringLiteral("#19e6d4"));
    hud.dim = QColor(QStringLiteral("#6b6b6b"));
    hud.border = QColor(QStringLiteral("#333333"));
    hud.frame = hud.fg;
    hud.radius = 2;
    hud.borderWidth = 2;
    hud.keyStyle = QStringLiteral("hud");
    hud.font = QStringLiteral("Quantico");
    hud.clockFont = QStringLiteral("Quantico:bold");
    themes.insert(QStringLiteral("hud"), builtin("hud", hud));

    Theme mono = amber;
    mono.bg = QColor(QStringLiteral("#000000"));
    mono.keyBg = QColor(QStringLiteral("#0a0a0a"));
    mono.fg = QColor(QStringLiteral("#f2f2f2"));
    mono.accent = QColor(QStringLiteral("#ffffff"));
    mono.dim = QColor(QStringLiteral("#777777"));
    mono.border = QColor(QStringLiteral("#2a2a2a"));
    mono.frame = mono.fg;
    mono.radius = 20;
    mono.borderWidth = 2;
    mono.font = QStringLiteral("Roboto");
    mono.clockFont = QStringLiteral("Roboto:light");
    themes.insert(QStringLiteral("mono"), builtin("mono", mono));
    return themes;
}

void applyThemeTable(Theme &theme, const toml::table &table)
{
    auto colourField = [&](const char *key, QColor &slot) {
        if (auto value = table[key].value<std::string>())
            slot = parseColour(QString::fromStdString(*value), slot);
    };
    colourField("bg", theme.bg);
    colourField("key_bg", theme.keyBg);
    colourField("fg", theme.fg);
    colourField("accent", theme.accent);
    colourField("dim", theme.dim);
    colourField("border", theme.border);
    colourField("frame", theme.frame);
    colourField("tick", theme.tick);
    if (auto value = table["radius"].value<int64_t>())
        theme.radius = static_cast<int>(*value);
    if (auto value = table["border_width"].value<int64_t>())
        theme.borderWidth = static_cast<int>(*value);
    if (auto value = table["font"].value<std::string>())
        theme.font = QString::fromStdString(*value);
    if (auto value = table["clock_font"].value<std::string>())
        theme.clockFont = QString::fromStdString(*value);
    if (auto value = table["icon_font"].value<std::string>())
        theme.iconFont = QString::fromStdString(*value);
    if (auto value = table["key_style"].value<std::string>())
        theme.keyStyle = QString::fromStdString(*value);
    if (!table["tick"])
        theme.tick = theme.accent;
}

KeySpec keyFromTable(const toml::table &table, int depth)
{
    KeySpec key;
    key.label = fromToml(table["label"]);
    key.icon = fromToml(table["icon"]);
    key.image = fromToml(table["image"]);
    key.fg = fromToml(table["fg"]);
    key.bg = fromToml(table["bg"]);
    key.border = fromToml(table["border"]);
    key.key = fromToml(table["key"]);
    key.keys = fromToml(table["keys"]);
    key.exec = fromToml(table["exec"]);
    key.media = fromToml(table["media"]);
    key.page = fromToml(table["page"]);
    key.profile = fromToml(table["profile"]);
    key.stat = fromToml(table["stat"]);
    key.sequence = fromToml(table["sequence"]);
    key.hold = fromToml(table["hold"]);
    key.repeat = fromToml(table["repeat"]);
    key.setTheme = fromToml(table["set_theme"]);
    key.confirm = fromBool(table["confirm"]);
    key.back = fromBool(table["back"]);
    key.fill = fromBool(table["fill"]);
    key.wallpaperTheme = fromBool(table["wallpaper_theme"]);
    key.stepMs = fromInt(table["step_ms"], 40);
    key.repeatMs = fromInt(table["repeat_ms"], 100);
    if (table["brightness"]) {
        key.hasBrightness = true;
        key.brightness = fromInt(table["brightness"], 0);
    }
    if (depth < 8) {
        if (auto *folder = table["folder"].as_array()) {
            for (auto &&child : *folder) {
                if (auto *childTable = child.as_table())
                    key.folder.append(keyFromTable(*childTable, depth + 1));
                else
                    key.folder.append(KeySpec{});
            }
            while (key.folder.size() < Keys)
                key.folder.append(KeySpec{});
            if (key.folder.size() > Keys)
                key.folder.resize(Keys);
        }
    }
    return key;
}

QList<KeySpec> keysFromArray(const toml::array *array)
{
    QList<KeySpec> keys;
    if (array) {
        for (auto &&node : *array) {
            if (auto *table = node.as_table())
                keys.append(keyFromTable(*table, 0));
            else
                keys.append(KeySpec{});
            if (keys.size() == Keys)
                break;
        }
    }
    while (keys.size() < Keys)
        keys.append(KeySpec{});
    return keys;
}

bool writeText(const QString &path, const QString &text)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(text.toUtf8());
    return file.commit();
}

} // namespace

QColor Theme::colour(const QString &value) const
{
    if (value.isEmpty())
        return fg;
    if (!value.startsWith(QLatin1Char('#'))) {
        if (value == QLatin1String("bg"))
            return bg;
        if (value == QLatin1String("key_bg"))
            return keyBg;
        if (value == QLatin1String("fg"))
            return fg;
        if (value == QLatin1String("accent"))
            return accent;
        if (value == QLatin1String("dim"))
            return dim;
        if (value == QLatin1String("border"))
            return border;
        if (value == QLatin1String("frame"))
            return frame;
        if (value == QLatin1String("tick"))
            return tick;
    }
    const QColor parsed(value);
    return parsed.isValid() ? parsed : fg;
}

bool KeySpec::isEmpty() const
{
    return label.isEmpty() && icon.isEmpty() && image.isEmpty() && key.isEmpty() && keys.isEmpty()
        && exec.isEmpty() && media.isEmpty() && page.isEmpty() && profile.isEmpty() && stat.isEmpty()
        && sequence.isEmpty() && repeat.isEmpty() && setTheme.isEmpty() && folder.isEmpty() && !back
        && !wallpaperTheme && !hasBrightness && fg.isEmpty() && bg.isEmpty() && border.isEmpty();
}

QString KeySpec::actionKind() const
{
    if (back)
        return QStringLiteral("back");
    if (!folder.isEmpty())
        return QStringLiteral("folder");
    if (!stat.isEmpty())
        return QStringLiteral("stat");
    if (!key.isEmpty())
        return QStringLiteral("key");
    if (!keys.isEmpty())
        return QStringLiteral("shortcut");
    if (!exec.isEmpty())
        return QStringLiteral("command");
    if (!media.isEmpty())
        return QStringLiteral("media");
    if (!page.isEmpty())
        return QStringLiteral("page");
    if (!profile.isEmpty())
        return QStringLiteral("profile");
    if (!sequence.isEmpty())
        return QStringLiteral("sequence");
    if (!repeat.isEmpty())
        return QStringLiteral("repeat");
    return QStringLiteral("none");
}

const Profile *Config::profile(const QString &name) const
{
    for (const Profile &profile : profiles) {
        if (profile.name == name)
            return &profile;
    }
    return nullptr;
}

Profile *Config::profile(const QString &name)
{
    for (Profile &profile : profiles) {
        if (profile.name == name)
            return &profile;
    }
    return nullptr;
}

QString Config::configDir()
{
    const QString configHome = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return configHome + QStringLiteral("/galleon-deck");
}

bool Config::ensureExamples()
{
    const QString dir = configDir();
    if (QFileInfo::exists(dir + QStringLiteral("/config.toml")))
        return true;
    const QString examples = QString::fromUtf8(GALLEAN_EXAMPLES_DIR);
    if (!QFileInfo::exists(examples + QStringLiteral("/config.toml")))
        return false;
    QDir().mkpath(dir);
    const auto entries = QDir(examples).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        const QString dest = dir + QLatin1Char('/') + entry.fileName();
        if (entry.isDir()) {
            QDir().mkpath(dest);
            const auto files = QDir(entry.absoluteFilePath()).entryInfoList(QDir::Files);
            for (const QFileInfo &file : files)
                QFile::copy(file.absoluteFilePath(), dest + QLatin1Char('/') + file.fileName());
        } else {
            QFile::copy(entry.absoluteFilePath(), dest);
        }
    }
    return QFileInfo::exists(dir + QStringLiteral("/config.toml"));
}

Config Config::load()
{
    Config config;
    config.dir = configDir();
    config.path = config.dir + QStringLiteral("/config.toml");
    config.themes = builtinThemes();

    if (!QFileInfo::exists(config.path)) {
        config.error = QStringLiteral("No config in %1").arg(config.dir);
        return config;
    }

    toml::table root;
    try {
        root = toml::parse_file(config.path.toStdString());
    } catch (const toml::parse_error &error) {
        const auto description = error.description();
        config.error = QStringLiteral("config.toml: %1")
                           .arg(QString::fromUtf8(description.data(), static_cast<qsizetype>(description.size())));
        return config;
    }

    config.brightness = fromInt(root["brightness"], 70);
    config.volumeStep = fromInt(root["volume_step"], 2);
    config.clockFormat = fromToml(root["clock_format"]);
    if (config.clockFormat.isEmpty())
        config.clockFormat = QStringLiteral("%H:%M");
    config.startProfile = fromToml(root["start_profile"]);

    const QDir themeDir(config.dir + QStringLiteral("/themes"));
    const auto themeFiles = themeDir.entryList({QStringLiteral("*.toml")}, QDir::Files, QDir::Name);
    for (const QString &fileName : themeFiles) {
        const QString name = QFileInfo(fileName).completeBaseName();
        try {
            const toml::table table = toml::parse_file((themeDir.absoluteFilePath(fileName)).toStdString());
            Theme theme = config.themes.value(name, amberTheme());
            theme.name = name;
            applyThemeTable(theme, table);
            config.themes.insert(name, theme);
        } catch (const toml::parse_error &error) {
            const auto description = error.description();
            qWarning().noquote() << "theme" << name << QString::fromUtf8(description.data(), static_cast<qsizetype>(description.size()));
        }
    }

    const QDir profileDir(config.dir + QStringLiteral("/profiles"));
    const auto profileFiles = profileDir.entryList({QStringLiteral("*.toml")}, QDir::Files, QDir::Name);
    for (const QString &fileName : profileFiles) {
        const QString name = QFileInfo(fileName).completeBaseName();
        const QString path = profileDir.absoluteFilePath(fileName);
        toml::table table;
        try {
            table = toml::parse_file(path.toStdString());
        } catch (const toml::parse_error &error) {
            const auto description = error.description();
            config.error = QStringLiteral("profiles/%1.toml: %2")
                               .arg(name, QString::fromUtf8(description.data(), static_cast<qsizetype>(description.size())));
            config.profiles.clear();
            return config;
        }

        Profile profile;
        profile.name = name;
        profile.path = path;
        profile.startPage = fromToml(table["start_page"]);
        profile.screen = fromToml(table["screen"]);
        profile.logo = fromToml(table["logo"]);
        profile.settingsPage = fromBool(table["settings_page"], true);
        profile.themeName = fromToml(table["theme"]);
        const bool follow = table.contains("follow_theme") && fromToml(table["theme"]).isEmpty();
        if (profile.themeName.isEmpty() && !follow)
            profile.themeName = QStringLiteral("amber");

        auto *pages = table["page"].as_array();
        if (!pages || pages->empty()) {
            config.error = QStringLiteral("profiles/%1.toml: no [[page]] entries").arg(name);
            config.profiles.clear();
            return config;
        }
        for (auto &&node : *pages) {
            auto *pageTable = node.as_table();
            if (!pageTable) {
                config.error = QStringLiteral("profiles/%1.toml: a [[page]] is not a table").arg(name);
                config.profiles.clear();
                return config;
            }
            Page page;
            page.name = fromToml((*pageTable)["name"]);
            if (page.name.isEmpty()) {
                config.error = QStringLiteral("profiles/%1.toml: a [[page]] has no name").arg(name);
                config.profiles.clear();
                return config;
            }
            auto *keyArray = (*pageTable)["keys"].as_array();
            if (keyArray && keyArray->size() > static_cast<size_t>(Keys)) {
                config.error = QStringLiteral("profiles/%1.toml: page %2 has more than %3 keys")
                                   .arg(name, page.name)
                                   .arg(Keys);
                config.profiles.clear();
                return config;
            }
            page.keys = keysFromArray(keyArray);
            profile.pages.append(page);
        }
        if (profile.settingsPage) {
            Page settings;
            settings.name = QStringLiteral("settings");
            settings.settings = true;
            settings.keys = QList<KeySpec>(Keys);
            profile.pages.append(settings);
        }
        config.profiles.append(profile);
    }

    if (config.profiles.isEmpty()) {
        config.error = QStringLiteral("no profiles in profiles/*.toml");
        return config;
    }

    for (Profile &profile : config.profiles) {
        if (profile.themeName.isEmpty()) {
            const QString start = config.startProfile;
            const Profile *source = config.profile(start);
            profile.themeName = source ? source->themeName : QStringLiteral("amber");
            if (profile.themeName.isEmpty())
                profile.themeName = QStringLiteral("amber");
        }
        if (!config.themes.contains(profile.themeName)) {
            config.error = QStringLiteral("profiles/%1.toml: unknown theme %2").arg(profile.name, profile.themeName);
            config.profiles.clear();
            return config;
        }
        profile.theme = config.themes.value(profile.themeName);
        toml::table file;
        try {
            file = toml::parse_file(profile.path.toStdString());
        } catch (const toml::parse_error &) {
            continue;
        }
        if (auto *overrides = file["theme_overrides"].as_table())
            applyThemeTable(profile.theme, *overrides);
    }
    return config;
}

QString configPath(const QString &dir, const QString &path)
{
    if (path.isEmpty())
        return {};
    const QString expanded = path.startsWith(QLatin1Char('~')) ? QDir::homePath() + path.mid(1) : path;
    if (QFileInfo(expanded).isAbsolute())
        return expanded;
    return dir + QLatin1Char('/') + expanded;
}

QList<KeySpec> keysAt(const QList<KeySpec> &pageKeys, const QList<int> &folderPath, QList<int> *resolved)
{
    QList<KeySpec> current = pageKeys;
    QList<int> used;
    for (int index : folderPath) {
        if (index < 0 || index >= current.size() || current.at(index).folder.isEmpty())
            break;
        used.append(index);
        current = current.at(index).folder;
        while (current.size() < Keys)
            current.append(KeySpec{});
    }
    if (resolved)
        *resolved = used;
    return current;
}

QString placeLabel(const QString &pageName, const QList<KeySpec> &keys, const QList<int> &folderPath)
{
    QStringList parts{pageName};
    QList<KeySpec> current = keys;
    for (int index : folderPath) {
        if (index < 0 || index >= current.size() || current.at(index).folder.isEmpty())
            break;
        const QString label = current.at(index).label;
        parts.append(label.isEmpty() ? QStringLiteral("Folder") : label);
        current = current.at(index).folder;
    }
    return parts.join(QStringLiteral(" / "));
}

bool assignTopLevel(const QString &path, const QString &key, const QString &literal)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();
    const QRegularExpression match(QStringLiteral("^%1\\s*=").arg(QRegularExpression::escape(key)));
    const QString replacement = key + QStringLiteral(" = ") + literal;
    bool found = false;
    for (int i = 0; i < lines.size(); ++i) {
        if (match.match(lines.at(i)).hasMatch()) {
            lines[i] = replacement;
            found = true;
            break;
        }
    }
    if (!found) {
        int at = 0;
        while (at < lines.size() && (lines.at(at).startsWith(QLatin1Char('#')) || lines.at(at).trimmed().isEmpty()))
            ++at;
        lines.insert(at, replacement);
    }
    return writeText(path, lines.join(QLatin1Char('\n')));
}

bool removeTopLevel(const QString &path, const QString &key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();
    const QRegularExpression match(QStringLiteral("^%1\\s*=").arg(QRegularExpression::escape(key)));
    for (int i = 0; i < lines.size(); ++i) {
        if (match.match(lines.at(i)).hasMatch()) {
            lines.removeAt(i);
            return writeText(path, lines.join(QLatin1Char('\n')));
        }
    }
    return true;
}

} // namespace galleon
