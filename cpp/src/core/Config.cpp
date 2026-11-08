#include "Media.hpp"
#include "Types.hpp"

#include <toml++/toml.hpp>

#include <QDebug>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

namespace kdeck {
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

template <typename View>
double fromDouble(const View &node, double fallback)
{
    if (!node)
        return fallback;
    if (auto value = node.template value<double>())
        return *value;
    if (auto value = node.template value<int64_t>())
        return static_cast<double>(*value);
    return fallback;
}

QList<SwitchRule> rulesFromArray(const toml::array *array)
{
    QList<SwitchRule> rules;
    if (!array)
        return rules;
    for (auto &&node : *array) {
        const toml::table *table = node.as_table();
        if (!table)
            continue;
        SwitchRule rule;
        rule.classPattern = fromToml((*table)["class"]);
        rule.titlePattern = fromToml((*table)["title"]);
        rule.profile = fromToml((*table)["profile"]);
        rules.append(rule);
    }
    return rules;
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
    key.app = fromToml(table["app"]);
    key.args = fromToml(table["args"]);
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
        && exec.isEmpty() && app.isEmpty() && args.isEmpty() && media.isEmpty() && page.isEmpty() && profile.isEmpty() && stat.isEmpty()
        && sequence.isEmpty() && hold.isEmpty() && repeat.isEmpty() && setTheme.isEmpty() && folder.isEmpty() && !back
        && !wallpaperTheme && !hasBrightness && fg.isEmpty() && bg.isEmpty() && border.isEmpty()
        && stepMs == 40 && repeatMs == 100;
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
    if (!app.isEmpty() || !args.isEmpty())
        return QStringLiteral("app");
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
    const QString current = configHome + QStringLiteral("/kdeck-control");
    if (QFileInfo::exists(current + QStringLiteral("/config.toml")))
        return current;
    // Profiles from the previous program name. Read them in place.
    const QString previous = configHome + QStringLiteral("/galleon-deck");
    if (QFileInfo::exists(previous + QStringLiteral("/config.toml")))
        return previous;
    return current;
}

namespace {

QString examplesDir()
{
    QStringList candidates{QString::fromUtf8(KDECK_EXAMPLES_DIR)};
#ifdef KDECK_EXAMPLES_SRC
    candidates.append(QString::fromUtf8(KDECK_EXAMPLES_SRC));
#endif
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate + QStringLiteral("/config.toml")))
            return candidate;
    }
    return {};
}

// The System Stats pages used to be an add-on. They ship with the app now.
// Copy the profile only when it is missing, so an edited copy is left alone.
void ensureBuiltinProfiles()
{
    const QString dir = Config::configDir();
    if (!QFileInfo::exists(dir + QStringLiteral("/config.toml")))
        return;
    const QString dest = dir + QStringLiteral("/profiles/system-stats.toml");
    if (QFileInfo::exists(dest))
        return;
    QString source;
    QStringList candidates{QString::fromUtf8(KDECK_EXAMPLES_DIR)};
#ifdef KDECK_EXAMPLES_SRC
    candidates.append(QString::fromUtf8(KDECK_EXAMPLES_SRC));
#endif
    for (const QString &candidate : candidates) {
        const QString path = candidate + QStringLiteral("/profiles/system-stats.toml");
        if (QFileInfo::exists(path)) {
            source = path;
            break;
        }
    }
    if (source.isEmpty())
        return;
    QDir().mkpath(dir + QStringLiteral("/profiles"));
    QFile::copy(source, dest);
}

// Drop the old add-on record when it only owns the built-in profile. Removing
// the add-on from the list would otherwise delete that profile.
void retireSystemStatsAddon()
{
    const QString state = QStandardPaths::writableLocation(QStandardPaths::StateLocation)
        + QStringLiteral("/kdeck-control/addons/system-stats.json");
    QFile file(state);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray files = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("files")).toArray();
    file.close();
    if (files.isEmpty())
        return;
    const QString profile = Config::configDir() + QStringLiteral("/profiles/system-stats.toml");
    for (const QJsonValue &value : files) {
        if (value.toString() != profile)
            return;
    }
    QFile::remove(state);
}

} // namespace

bool Config::ensureExamples()
{
    ensureBuiltinProfiles();
    retireSystemStatsAddon();
    const QString dir = configDir();
    if (QFileInfo::exists(dir + QStringLiteral("/config.toml")))
        return true;
    const QString examples = examplesDir();
    if (examples.isEmpty())
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
    if (const toml::array *players = root["media_players"].as_array()) {
        for (auto &&node : *players) {
            if (auto value = node.value<std::string>()) {
                const QString name = normalizeMediaPlayer(QString::fromStdString(*value));
                if (!name.isEmpty() && !config.mediaPlayers.contains(name))
                    config.mediaPlayers.append(name);
            }
        }
    }
    if (const toml::table *addons = root["addons"].as_table()) {
        config.addonDir = fromToml((*addons)["dir"]);
        if (config.addonDir.startsWith(QLatin1Char('~')))
            config.addonDir = QDir::homePath() + config.addonDir.mid(1);
    }
    if (const toml::table *wallpaper = root["wallpaper_theme"].as_table()) {
        config.wallpaperAuto = fromBool((*wallpaper)["auto"]);
        config.wallpaperCommand = fromToml((*wallpaper)["command"]);
        const QString font = fromToml((*wallpaper)["font"]);
        const QString clockFont = fromToml((*wallpaper)["clock_font"]);
        if (!font.isEmpty())
            config.wallpaperFont = font;
        if (!clockFont.isEmpty())
            config.wallpaperClockFont = clockFont;
    }
    if (const toml::table *transition = root["transition"].as_table()) {
        config.transitionEnabled = fromBool((*transition)["enabled"], true);
        config.transitionSpeed = fromDouble((*transition)["speed"], 1.0);
        config.transitionHold = fromDouble((*transition)["hold"], 0.5);
    }
    if (const toml::table *boot = root["boot"].as_table()) {
        config.bootEnabled = fromBool((*boot)["enabled"], true);
        config.bootSpeed = fromDouble((*boot)["speed"], 1.0);
        const QString title = fromToml((*boot)["title"]);
        if (!title.isEmpty())
            config.bootTitle = title;
        if (const toml::array *lines = (*boot)["lines"].as_array()) {
            for (auto &&node : *lines) {
                if (auto value = node.value<std::string>())
                    config.bootLines.append(QString::fromStdString(*value));
            }
        }
        config.bootLogo = fromToml((*boot)["logo"]);
        config.bootGif = fromToml((*boot)["gif"]);
    }
    if (const toml::table *automatic = root["auto_switch"].as_table()) {
        config.autoFallback = fromToml((*automatic)["fallback"]);
        config.autoRules = rulesFromArray((*automatic)["rules"].as_array());
    }
    if (config.bootLines.isEmpty()) {
        config.bootLines = {QStringLiteral("DECK LINK 1B1C:2B18|OK"), QStringLiteral("PROFILES|LOADED"),
                            QStringLiteral("KEYS|ENCRYPTED"), QStringLiteral("DIALS|READY")};
    }

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
        if (table.contains("follow_theme") && profile.themeName.isEmpty()) {
            profile.followTheme = true;
            profile.followTarget = fromToml(table["follow_theme"]);
        }
        profile.autoSwitch = rulesFromArray(table["auto_switch"].as_array());
        if (profile.themeName.isEmpty() && !profile.followTheme)
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

    auto applyOverrides = [](Profile &profile) {
        toml::table file;
        try {
            file = toml::parse_file(profile.path.toStdString());
        } catch (const toml::parse_error &) {
            return;
        }
        if (auto *overrides = file["theme_overrides"].as_table())
            applyThemeTable(profile.theme, *overrides);
    };
    auto bindTheme = [&](Profile &profile, const QString &themeName, const Theme &base) {
        if (!config.themes.contains(themeName)) {
            config.error = QStringLiteral("profiles/%1.toml: unknown theme %2").arg(profile.name, themeName);
            return false;
        }
        // A follower keeps follow_theme in the file. themeName stays empty so a
        // later save does not pin the copied colours as its own theme.
        if (!profile.followTheme)
            profile.themeName = themeName;
        profile.theme = base;
        applyOverrides(profile);
        return true;
    };

    QStringList waiting;
    for (Profile &profile : config.profiles) {
        if (profile.followTheme) {
            waiting.append(profile.name);
            continue;
        }
        if (!bindTheme(profile, profile.themeName, config.themes.value(profile.themeName))) {
            config.profiles.clear();
            return config;
        }
    }
    while (!waiting.isEmpty()) {
        QStringList later;
        for (const QString &name : waiting) {
            Profile *profile = config.profile(name);
            if (!profile)
                continue;
            const QString sourceName = profile->followTarget.isEmpty() ? config.startProfile : profile->followTarget;
            const Profile *source = !sourceName.isEmpty() && sourceName != name ? config.profile(sourceName) : nullptr;
            if (!source) {
                if (!bindTheme(*profile, QStringLiteral("amber"), config.themes.value(QStringLiteral("amber")))) {
                    config.profiles.clear();
                    return config;
                }
                continue;
            }
            if (waiting.contains(sourceName) || later.contains(sourceName)) {
                later.append(name);
                continue;
            }
            if (!bindTheme(*profile, source->themeName, source->theme)) {
                config.profiles.clear();
                return config;
            }
        }
        if (later.size() == waiting.size()) {
            for (const QString &name : later) {
                Profile *profile = config.profile(name);
                if (profile && !bindTheme(*profile, QStringLiteral("amber"), config.themes.value(QStringLiteral("amber")))) {
                    config.profiles.clear();
                    return config;
                }
            }
            break;
        }
        waiting = later;
    }
    return config;
}

qint64 configStamp()
{
    const QString dir = Config::configDir();
    qint64 latest = 0;
    QList<QFileInfo> files;
    files.append(QFileInfo(dir + QStringLiteral("/config.toml")));
    for (const QFileInfo &info : QDir(dir + QStringLiteral("/profiles")).entryInfoList({QStringLiteral("*.toml")}, QDir::Files))
        files.append(info);
    for (const QFileInfo &info : QDir(dir + QStringLiteral("/themes")).entryInfoList({QStringLiteral("*.toml")}, QDir::Files))
        files.append(info);
    for (const QFileInfo &info : files)
        latest = std::max(latest, info.lastModified().toMSecsSinceEpoch());
    return latest + files.size();
}

bool wallpaperThemeMatches(const QString &dir, const QString &imagePath)
{
    if (imagePath.isEmpty())
        return true;
    const QFileInfo image(imagePath);
    if (!image.exists())
        return false;
    try {
        const QString file = dir + QStringLiteral("/themes/wallpaper.toml");
        const toml::table table = toml::parse_file(file.toStdString());
        const auto source = table["source"].value<std::string>();
        const auto stored = table["source_mtime"].value<int64_t>();
        if (!source || !stored)
            return false;
        return QString::fromStdString(*source) == imagePath && *stored == image.lastModified().toSecsSinceEpoch();
    } catch (const toml::parse_error &) {
        return false;
    }
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

namespace {

int bracketDepth(const QString &line)
{
    int depth = 0;
    QChar quote;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (!quote.isNull()) {
            if (ch == QLatin1Char('\\') && quote == QLatin1Char('"'))
                ++i;
            else if (ch == quote)
                quote = QChar();
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\''))
            quote = ch;
        else if (ch == QLatin1Char('#'))
            break;
        else if (ch == QLatin1Char('[') || ch == QLatin1Char('{'))
            ++depth;
        else if (ch == QLatin1Char(']') || ch == QLatin1Char('}'))
            --depth;
    }
    return depth;
}

QString trailingComment(const QString &line)
{
    QChar quote;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (!quote.isNull()) {
            if (ch == QLatin1Char('\\') && quote == QLatin1Char('"'))
                ++i;
            else if (ch == quote)
                quote = QChar();
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\''))
            quote = ch;
        else if (ch == QLatin1Char('#'))
            return line.mid(i);
    }
    return {};
}

int tableSection(const QStringList &lines, const QString &table, int *headerAt)
{
    const QRegularExpression header(QStringLiteral("^\\[%1\\]\\s*(?:#.*)?$").arg(QRegularExpression::escape(table)));
    const QRegularExpression anyHeader(QStringLiteral("^\\[([^\\[\\]]+)\\]\\s*(?:#.*)?$"));
    for (int i = 0; i < lines.size(); ++i) {
        if (!header.match(lines.at(i)).hasMatch())
            continue;
        *headerAt = i;
        for (int j = i + 1; j < lines.size(); ++j) {
            if (anyHeader.match(lines.at(j)).hasMatch())
                return j;
        }
        return lines.size();
    }
    *headerAt = -1;
    return lines.size();
}

} // namespace

bool assignTableKey(const QString &path, const QString &table, const QString &key, const QString &literal)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();
    int headerAt = -1;
    const int sectionEnd = tableSection(lines, table, &headerAt);
    const QRegularExpression match(QStringLiteral("^\\s*%1\\s*=").arg(QRegularExpression::escape(key)));
    const QString replacement = key + QStringLiteral(" = ") + literal;
    if (headerAt >= 0) {
        for (int i = headerAt + 1; i < sectionEnd; ++i) {
            if (match.match(lines.at(i)).hasMatch()) {
                lines[i] = replacement;
                return writeText(path, lines.join(QLatin1Char('\n')));
            }
        }
        lines.insert(headerAt + 1, replacement);
        return writeText(path, lines.join(QLatin1Char('\n')));
    }
    if (!lines.isEmpty() && !lines.last().isEmpty())
        lines.append(QString());
    lines.append(QLatin1Char('[') + table + QLatin1Char(']'));
    lines.append(replacement);
    return writeText(path, lines.join(QLatin1Char('\n')));
}

bool removeTableKey(const QString &path, const QString &table, const QString &key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();
    int headerAt = -1;
    const int sectionEnd = tableSection(lines, table, &headerAt);
    if (headerAt < 0)
        return true;
    const QRegularExpression match(QStringLiteral("^\\s*%1\\s*=").arg(QRegularExpression::escape(key)));
    for (int i = headerAt + 1; i < sectionEnd; ++i) {
        if (!match.match(lines.at(i)).hasMatch())
            continue;
        lines.removeAt(i);
        bool empty = true;
        for (int j = headerAt + 1; j < sectionEnd - 1; ++j) {
            const QString trimmed = lines.at(j).trimmed();
            if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('#'))) {
                empty = false;
                break;
            }
        }
        if (empty) {
            int drop = headerAt;
            if (drop > 0 && lines.at(drop - 1).trimmed().isEmpty())
                --drop;
            const int count = headerAt - drop + 1;
            for (int n = 0; n < count; ++n)
                lines.removeAt(drop);
        }
        return writeText(path, lines.join(QLatin1Char('\n')));
    }
    return true;
}

bool replaceTableValue(const QString &path, const QString &table, const QString &key, const QString &literal)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();
    int headerAt = -1;
    const int sectionEnd = tableSection(lines, table, &headerAt);
    if (headerAt < 0)
        return false;
    const QRegularExpression match(QStringLiteral("^\\s*%1\\s*=").arg(QRegularExpression::escape(key)));
    for (int i = headerAt + 1; i < sectionEnd; ++i) {
        if (!match.match(lines.at(i)).hasMatch())
            continue;
        int end = i;
        int depth = bracketDepth(lines.at(i));
        while (depth > 0 && end + 1 < sectionEnd) {
            ++end;
            depth += bracketDepth(lines.at(end));
        }
        QStringList replacement = literal.split(QLatin1Char('\n'));
        if (replacement.isEmpty())
            replacement.append(QString());
        replacement[0] = key + QStringLiteral(" = ") + replacement.at(0);
        if (replacement.size() == 1 && end == i) {
            const QString comment = trailingComment(lines.at(i));
            if (!comment.isEmpty()) {
                const int column = lines.at(i).size() - comment.size();
                if (replacement[0].size() < column)
                    replacement[0] = replacement[0].leftJustified(column);
                else
                    replacement[0] += QStringLiteral("  ");
                replacement[0] += comment;
            }
        }
        for (int n = i; n <= end; ++n)
            lines.removeAt(i);
        for (int n = 0; n < replacement.size(); ++n)
            lines.insert(i + n, replacement.at(n));
        return writeText(path, lines.join(QLatin1Char('\n')));
    }
    return false;
}

} // namespace kdeck
