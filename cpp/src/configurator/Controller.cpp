#include "Controller.hpp"

#include "Apps.hpp"
#include "Media.hpp"
#include "Render.hpp"

#include <toml++/toml.hpp>

#include <KLocalizedString>

#include <QDesktopServices>

#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QColor>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>

#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <map>
#include <sstream>

namespace kdeck {
namespace {

QString tomlString(const QString &text)
{
    QString out = QStringLiteral("\"");
    for (QChar c : text) {
        if (c == QLatin1Char('\\') || c == QLatin1Char('"')) {
            out += QLatin1Char('\\');
            out += c;
        } else if (c == QLatin1Char('\n')) {
            out += QStringLiteral("\\n");
        } else {
            out += c;
        }
    }
    out += QLatin1Char('"');
    return out;
}

QString switchRulesLiteral(const QList<SwitchRule> &rules)
{
    if (rules.isEmpty())
        return QStringLiteral("[]");
    QStringList lines;
    for (const SwitchRule &rule : rules) {
        QStringList fields;
        fields.append(QStringLiteral("class = ") + tomlString(rule.classPattern));
        if (!rule.titlePattern.isEmpty())
            fields.append(QStringLiteral("title = ") + tomlString(rule.titlePattern));
        if (!rule.profile.isEmpty())
            fields.append(QStringLiteral("profile = ") + tomlString(rule.profile));
        lines.append(QStringLiteral("  { ") + fields.join(QStringLiteral(", ")) + QStringLiteral(" },"));
    }
    return QStringLiteral("[\n") + lines.join(QLatin1Char('\n')) + QStringLiteral("\n]");
}

QString formatKey(const KeySpec &key)
{
    if (key.isEmpty())
        return QStringLiteral("{}");
    QStringList parts;
    auto add = [&](const char *name, const QString &value) {
        if (!value.isEmpty())
            parts.append(QString::fromLatin1(name) + QStringLiteral(" = ") + tomlString(value));
    };
    add("label", key.label);
    add("icon", key.icon);
    add("image", key.image);
    add("fg", key.fg);
    add("bg", key.bg);
    add("border", key.border);
    add("key", key.key);
    add("keys", key.keys);
    add("exec", key.exec);
    add("app", key.app);
    add("args", key.args);
    add("media", key.media);
    add("page", key.page);
    add("profile", key.profile);
    add("stat", key.stat);
    add("sequence", key.sequence);
    add("hold", key.hold);
    add("repeat", key.repeat);
    add("set_theme", key.setTheme);
    if (key.confirm)
        parts.append(QStringLiteral("confirm = true"));
    if (key.back)
        parts.append(QStringLiteral("back = true"));
    if (key.fill)
        parts.append(QStringLiteral("fill = true"));
    if (key.wallpaperTheme)
        parts.append(QStringLiteral("wallpaper_theme = true"));
    if (key.hasBrightness)
        parts.append(QStringLiteral("brightness = ") + QString::number(key.brightness));
    if (key.stepMs != 40)
        parts.append(QStringLiteral("step_ms = ") + QString::number(key.stepMs));
    if (key.repeatMs != 100)
        parts.append(QStringLiteral("repeat_ms = ") + QString::number(key.repeatMs));
    if (!key.folder.isEmpty()) {
        QList<KeySpec> children = key.folder;
        while (!children.isEmpty() && children.last().isEmpty())
            children.removeLast();
        QStringList nested;
        for (const KeySpec &child : children)
            nested.append(formatKey(child));
        parts.append(QStringLiteral("folder = [ ") + nested.join(QStringLiteral(", ")) + QStringLiteral(" ]"));
    }
    return QStringLiteral("{ ") + parts.join(QStringLiteral(", ")) + QStringLiteral(" }");
}

QString leadingComments(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("# kdeck-control profile.\n");
    QStringList head;
    const auto lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1Char('#')) || line.trimmed().isEmpty())
            head.append(line);
        else
            break;
    }
    while (!head.isEmpty() && head.last().trimmed().isEmpty())
        head.removeLast();
    return head.isEmpty() ? QString() : head.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString formatProfile(const Profile &profile, const toml::table &table)
{
    QString out = leadingComments(profile.path);
    if (!out.isEmpty())
        out += QLatin1Char('\n');
    auto addString = [&](const char *name, const QString &value) {
        if (!value.isEmpty())
            out += QString::fromLatin1(name) + QStringLiteral(" = ") + tomlString(value) + QLatin1Char('\n');
    };
    // follow_theme = true must stay the only theme line. Writing the resolved
    // theme beside it makes the next load pin those colours and stop following.
    if (table.contains("follow_theme")) {
        std::ostringstream stream;
        stream << table["follow_theme"];
        out += QStringLiteral("follow_theme = ") + QString::fromStdString(stream.str()) + QLatin1Char('\n');
    } else {
        addString("theme", profile.themeName);
    }
    addString("start_page", profile.startPage);
    addString("logo", profile.logo);
    addString("screen", profile.screen);
    if (!profile.settingsPage)
        out += QStringLiteral("settings_page = false\n");
    if (table.contains("auto_switch")) {
        std::ostringstream stream;
        stream << table["auto_switch"];
        out += QStringLiteral("auto_switch = ") + QString::fromStdString(stream.str()) + QLatin1Char('\n');
    }
    if (auto *overrides = table["theme_overrides"].as_table()) {
        std::ostringstream stream;
        stream << *overrides;
        out += QStringLiteral("\n[theme_overrides]\n") + QString::fromStdString(stream.str());
        if (!out.endsWith(QLatin1Char('\n')))
            out += QLatin1Char('\n');
    }
    for (const Page &page : profile.pages) {
        if (page.settings)
            continue;
        QList<KeySpec> keys = page.keys;
        while (!keys.isEmpty() && keys.last().isEmpty())
            keys.removeLast();
        out += QStringLiteral("\n[[page]]\nname = ") + tomlString(page.name) + QStringLiteral("\nkeys = [\n");
        for (const KeySpec &key : keys)
            out += QStringLiteral("  ") + formatKey(key) + QStringLiteral(",\n");
        out += QStringLiteral("]\n");
    }
    return out;
}

const char *valueField(const QString &kind)
{
    if (kind == QLatin1String("key"))
        return "key";
    if (kind == QLatin1String("shortcut"))
        return "keys";
    if (kind == QLatin1String("command"))
        return "exec";
    if (kind == QLatin1String("app"))
        return "app";
    if (kind == QLatin1String("media"))
        return "media";
    if (kind == QLatin1String("page"))
        return "page";
    if (kind == QLatin1String("profile"))
        return "profile";
    if (kind == QLatin1String("sequence"))
        return "sequence";
    if (kind == QLatin1String("repeat"))
        return "repeat";
    return nullptr;
}

// Same order as the GTK configurator's ACTIONS list. A stat key stays "stat"
// even when a press action is stored beside it; that press action is separate.
QString storedKind(const KeySpec &key)
{
    if (!key.stat.isEmpty())
        return QStringLiteral("stat");
    if (!key.key.isEmpty())
        return QStringLiteral("key");
    if (!key.keys.isEmpty())
        return QStringLiteral("shortcut");
    // hold and step_ms belong to a sequence even before the taps are typed.
    if (!key.sequence.isEmpty() || !key.hold.isEmpty() || key.stepMs != 40)
        return QStringLiteral("sequence");
    if (!key.repeat.isEmpty())
        return QStringLiteral("repeat");
    if (!key.app.isEmpty() || !key.args.isEmpty())
        return QStringLiteral("app");
    if (!key.exec.isEmpty())
        return QStringLiteral("command");
    if (!key.media.isEmpty())
        return QStringLiteral("media");
    if (!key.page.isEmpty())
        return QStringLiteral("page");
    if (!key.folder.isEmpty())
        return QStringLiteral("folder");
    if (key.back)
        return QStringLiteral("back");
    if (!key.profile.isEmpty())
        return QStringLiteral("profile");
    return QStringLiteral("none");
}

QString storedPress(const KeySpec &key)
{
    if (!key.key.isEmpty())
        return QStringLiteral("key");
    if (!key.keys.isEmpty())
        return QStringLiteral("shortcut");
    if (!key.sequence.isEmpty() || !key.hold.isEmpty() || key.stepMs != 40)
        return QStringLiteral("sequence");
    if (!key.repeat.isEmpty())
        return QStringLiteral("repeat");
    if (!key.app.isEmpty() || !key.args.isEmpty())
        return QStringLiteral("app");
    if (!key.exec.isEmpty())
        return QStringLiteral("command");
    if (!key.media.isEmpty())
        return QStringLiteral("media");
    if (!key.page.isEmpty())
        return QStringLiteral("page");
    if (!key.profile.isEmpty())
        return QStringLiteral("profile");
    return QStringLiteral("none");
}

QString valueOf(const KeySpec &key, const QString &kind)
{
    if (kind == QLatin1String("key"))
        return key.key;
    if (kind == QLatin1String("shortcut"))
        return key.keys;
    if (kind == QLatin1String("command"))
        return key.exec;
    if (kind == QLatin1String("app"))
        return key.app;
    if (kind == QLatin1String("media"))
        return key.media;
    if (kind == QLatin1String("page"))
        return key.page;
    if (kind == QLatin1String("profile"))
        return key.profile;
    if (kind == QLatin1String("sequence"))
        return key.sequence;
    if (kind == QLatin1String("repeat"))
        return key.repeat;
    return {};
}

void setValueOf(KeySpec &key, const QString &kind, const QString &value)
{
    if (kind == QLatin1String("key"))
        key.key = value;
    else if (kind == QLatin1String("shortcut"))
        key.keys = value;
    else if (kind == QLatin1String("command"))
        key.exec = value;
    else if (kind == QLatin1String("app"))
        key.app = value;
    else if (kind == QLatin1String("media"))
        key.media = value;
    else if (kind == QLatin1String("page"))
        key.page = value;
    else if (kind == QLatin1String("profile"))
        key.profile = value;
    else if (kind == QLatin1String("sequence"))
        key.sequence = value;
    else if (kind == QLatin1String("repeat"))
        key.repeat = value;
}

// Drop every action except `kind`. `keepStat` leaves the sensor in place when
// the press action of a stat key changes.
void keepAction(KeySpec &key, const QString &kind, bool keepStat)
{
    if (kind != QLatin1String("key"))
        key.key.clear();
    if (kind != QLatin1String("shortcut"))
        key.keys.clear();
    if (kind != QLatin1String("command")) {
        key.exec.clear();
        key.confirm = false;
    }
    if (kind != QLatin1String("app")) {
        key.app.clear();
        key.args.clear();
    }
    if (kind != QLatin1String("media"))
        key.media.clear();
    if (kind != QLatin1String("page"))
        key.page.clear();
    if (kind != QLatin1String("profile"))
        key.profile.clear();
    if (kind != QLatin1String("sequence")) {
        key.sequence.clear();
        key.hold.clear();
        key.stepMs = 40;
    }
    if (kind != QLatin1String("repeat")) {
        key.repeat.clear();
        key.repeatMs = 100;
    }
    if (kind != QLatin1String("folder"))
        key.folder.clear();
    if (kind != QLatin1String("back"))
        key.back = false;
    if (!keepStat && kind != QLatin1String("stat"))
        key.stat.clear();
}

void applyKindDefaults(KeySpec &key, const QString &kind, const QString &firstProfile)
{
    if (kind == QLatin1String("media") && key.media.isEmpty())
        key.media = QStringLiteral("play-pause");
    else if (kind == QLatin1String("page") && key.page.isEmpty())
        key.page = QStringLiteral("back");
    else if (kind == QLatin1String("profile") && key.profile.isEmpty() && !firstProfile.isEmpty())
        key.profile = firstProfile;
}

struct StatRow {
    const char *id;
    const char *title;
    const char *label;
};

// Titles and short labels match kdeck_stats.STATS. The degree sign is written
// as UTF-8 bytes so the source does not depend on how the compiler reads the file.
const StatRow kStats[] = {
    {"cpu", "CPU usage", "CPU"},
    {"cpu-temp", "CPU temperature", "CPU \xC2\xB0" "C"},
    {"cpu-freq", "CPU frequency", "CPU GHz"},
    {"ram", "Memory", "RAM"},
    {"swap", "Swap", "SWAP"},
    {"disk", "Disk space", "DISK"},
    {"gpu", "GPU usage", "GPU"},
    {"gpu-temp", "GPU temperature", "GPU \xC2\xB0" "C"},
    {"gpu-power", "GPU power", "POWER"},
    {"vram", "Video memory", "VRAM"},
    {"net", "Network", "NET"},
    {"io", "Disk throughput", "IO"},
    {"zfs", "ZFS pool", "ZFS"},
    {"zfs-health", "ZFS health", "HEALTH"},
    {"zfs-frag", "ZFS fragmentation", "FRAG"},
    {"zfs-io", "ZFS throughput", "ZFS IO"},
    {"zfs-data", "ZFS dataset", "DATA"},
    {"arc", "ARC size", "ARC"},
    {"arc-hit", "ARC hit ratio", "HITS"},
};

QString statLabelFor(const QString &id)
{
    for (const StatRow &row : kStats) {
        if (id == QLatin1String(row.id))
            return QString::fromUtf8(row.label);
    }
    return {};
}

QString backIconGlyph()
{
    const char32_t codepoint = 0xF004D;
    return QString::fromUcs4(&codepoint, 1);
}

void fillNewFolder(KeySpec &key)
{
    key.folder.clear();
    KeySpec back;
    back.back = true;
    back.icon = backIconGlyph();
    back.label = QStringLiteral("Back");
    key.folder.append(back);
    while (key.folder.size() < Keys)
        key.folder.append(KeySpec{});
}

void mirrorKey(toml::table &table, const KeySpec &key)
{
    table.clear();
    auto put = [&](const char *name, const QString &value) {
        if (!value.isEmpty())
            table.insert_or_assign(name, value.toStdString());
    };
    put("label", key.label);
    put("icon", key.icon);
    put("image", key.image);
    put("fg", key.fg);
    put("bg", key.bg);
    put("border", key.border);
    put("key", key.key);
    put("keys", key.keys);
    put("exec", key.exec);
    put("app", key.app);
    put("args", key.args);
    put("media", key.media);
    put("page", key.page);
    put("profile", key.profile);
    put("stat", key.stat);
    put("sequence", key.sequence);
    put("hold", key.hold);
    put("repeat", key.repeat);
    put("set_theme", key.setTheme);
    if (key.confirm)
        table.insert_or_assign("confirm", true);
    if (key.back)
        table.insert_or_assign("back", true);
    if (key.fill)
        table.insert_or_assign("fill", true);
    if (key.wallpaperTheme)
        table.insert_or_assign("wallpaper_theme", true);
    if (key.hasBrightness)
        table.insert_or_assign("brightness", static_cast<std::int64_t>(key.brightness));
    if (key.stepMs != 40)
        table.insert_or_assign("step_ms", static_cast<std::int64_t>(key.stepMs));
    if (key.repeatMs != 100)
        table.insert_or_assign("repeat_ms", static_cast<std::int64_t>(key.repeatMs));
    if (!key.folder.isEmpty()) {
        toml::array folder;
        for (const KeySpec &child : key.folder) {
            toml::table childTable;
            mirrorKey(childTable, child);
            folder.push_back(std::move(childTable));
        }
        table.insert_or_assign("folder", std::move(folder));
    }
}

void assignString(toml::table &table, const char *name, const QString &value)
{
    if (value.isEmpty())
        table.erase(name);
    else
        table.insert_or_assign(name, value.toStdString());
}

const QColor *themeSlot(const Theme &theme, const QString &field)
{
    if (field == QLatin1String("bg"))
        return &theme.bg;
    if (field == QLatin1String("key_bg"))
        return &theme.keyBg;
    if (field == QLatin1String("fg"))
        return &theme.fg;
    if (field == QLatin1String("accent"))
        return &theme.accent;
    if (field == QLatin1String("frame"))
        return &theme.frame;
    if (field == QLatin1String("dim"))
        return &theme.dim;
    if (field == QLatin1String("border"))
        return &theme.border;
    return nullptr;
}

QString normalHex(const QString &text)
{
    const QColor colour(text);
    if (!colour.isValid())
        return {};
    const QString hex = colour.name(QColor::HexRgb);
    return hex.size() == 7 ? hex : QString();
}

void applyOverrides(Theme &theme, const toml::table &table)
{
    auto colourField = [&](const char *key, QColor &slot) {
        if (auto value = table[key].value<std::string>()) {
            const QColor colour(QString::fromStdString(*value));
            if (colour.isValid())
                slot = colour;
        }
    };
    colourField("bg", theme.bg);
    colourField("key_bg", theme.keyBg);
    colourField("fg", theme.fg);
    colourField("accent", theme.accent);
    colourField("dim", theme.dim);
    colourField("border", theme.border);
    colourField("frame", theme.frame);
    colourField("tick", theme.tick);
    if (!table.contains("tick"))
        theme.tick = theme.accent;
    if (auto value = table["font"].value<std::string>())
        theme.font = QString::fromStdString(*value);
    if (auto value = table["clock_font"].value<std::string>())
        theme.clockFont = QString::fromStdString(*value);
    if (auto value = table["icon_font"].value<std::string>())
        theme.iconFont = QString::fromStdString(*value);
    if (auto value = table["key_style"].value<std::string>())
        theme.keyStyle = QString::fromStdString(*value);
}

QString formatNow(const QString &format)
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char buffer[128];
    if (strftime(buffer, sizeof buffer, format.toUtf8().constData(), &local) == 0)
        return {};
    return QString::fromUtf8(buffer);
}

QString addonPython();
bool runKdeckImport(const QString &body, const QStringList &args, QString *output, QString *errorText, int timeoutMs);

} // namespace

struct Controller::Store {
    std::map<std::string, toml::table> tables;
    QStringList dirty;
};

Controller::Controller(QObject *parent)
    : QObject(parent)
    , m_store(std::make_unique<Store>())
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &Controller::save);
    m_followTimer.setInterval(500);
    connect(&m_followTimer, &QTimer::timeout, this, &Controller::followDeck);
    m_watchTimer.setInterval(2000);
    connect(&m_watchTimer, &QTimer::timeout, this, &Controller::watchConfig);
    m_animTimer.setInterval(20);
    connect(&m_animTimer, &QTimer::timeout, this, &Controller::tickAnimations);
    connect(this, &Controller::changed, this, &Controller::syncKeyAnimation);
    reload();
    m_followTimer.start();
    m_watchTimer.start();
    pollService();
    refreshAddons();
    if (QCoreApplication *app = QCoreApplication::instance())
        app->installEventFilter(this);
}

Controller::~Controller()
{
    if (QCoreApplication *app = QCoreApplication::instance())
        app->removeEventFilter(this);
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();
        save();
    }
}

void Controller::reload()
{
    Config::ensureExamples();
    m_config = Config::load();
    m_store->tables.clear();
    m_store->dirty.clear();
    m_dirty = false;
    for (const Profile &profile : m_config.profiles) {
        try {
            m_store->tables.emplace(profile.name.toStdString(), toml::parse_file(profile.path.toStdString()));
        } catch (const toml::parse_error &) {
        }
    }
    if (!m_config.profiles.isEmpty() && !m_config.profile(m_profile))
        m_profile = m_config.profiles.first().name;
    if (const Profile *profile = m_config.profile(m_profile)) {
        if (profile->pages.isEmpty()) {
            m_page = 0;
            m_folderPath.clear();
        } else {
            m_page = std::clamp(m_page, 0, static_cast<int>(profile->pages.size()) - 1);
            if (profile->pages.at(m_page).settings)
                m_folderPath.clear();
            else
                keysAt(profile->pages.at(m_page).keys, m_folderPath, &m_folderPath);
        }
    }
    m_key = std::clamp(m_key, 0, Keys - 1);
    m_seenStamp = configStamp();
    m_status = m_config.error.isEmpty() ? i18n("Profiles from %1", m_config.dir) : m_config.error;
    refreshPreview();
    ++m_revision;
    ++m_previewRevision;
    Q_EMIT changed();
    Q_EMIT previewChanged();
}

QStringList Controller::profiles() const
{
    QStringList names;
    for (const Profile &profile : m_config.profiles)
        names.append(profile.name);
    return names;
}

void Controller::setProfile(const QString &name)
{
    if (name == m_profile || !m_config.profile(name))
        return;
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();
        save();
    }
    m_profile = name;
    m_page = 0;
    m_key = 0;
    m_folderPath.clear();
    ++m_revision;
    ++m_previewRevision;
    m_status = i18n("Profiles from %1", m_config.dir);
    Q_EMIT changed();
    Q_EMIT previewChanged();
}

QStringList Controller::pages() const
{
    QStringList names;
    if (const Profile *profile = m_config.profile(m_profile)) {
        for (const Page &page : profile->pages)
            names.append(page.name);
    }
    return names;
}

bool Controller::pageIsSettings(int index) const
{
    const Profile *profile = m_config.profile(m_profile);
    if (!profile || index < 0 || index >= profile->pages.size())
        return false;
    return profile->pages.at(index).settings;
}

void Controller::setPage(int page)
{
    const int count = pages().size();
    if (count == 0)
        return;
    page = std::clamp(page, 0, count - 1);
    if (page == m_page)
        return;
    m_page = page;
    m_key = 0;
    m_folderPath.clear();
    ++m_revision;
    ++m_previewRevision;
    Q_EMIT changed();
    Q_EMIT previewChanged();
}

QString Controller::pageName() const
{
    const QStringList names = pages();
    if (m_page < 0 || m_page >= names.size())
        return {};
    return names.at(m_page);
}

void Controller::setKeyIndex(int index)
{
    index = std::clamp(index, 0, Keys - 1);
    if (index == m_key)
        return;
    m_key = index;
    Q_EMIT changed();
}

QList<KeySpec> Controller::visibleKeys() const
{
    const Profile *profile = m_config.profile(m_profile);
    if (!profile || m_page < 0 || m_page >= profile->pages.size())
        return {};
    const Page &page = profile->pages.at(m_page);
    if (page.settings)
        return settingsKeys(m_config, m_profile);
    return keysAt(page.keys, m_folderPath);
}

const KeySpec *Controller::currentKey() const
{
    return const_cast<Controller *>(this)->currentKey();
}

KeySpec *Controller::currentKey()
{
    Profile *profile = m_config.profile(m_profile);
    if (!profile || m_page < 0 || m_page >= profile->pages.size())
        return nullptr;
    Page &page = profile->pages[m_page];
    if (page.settings)
        return nullptr;
    QList<KeySpec> *level = &page.keys;
    for (int index : m_folderPath) {
        if (index < 0 || index >= level->size() || (*level).at(index).folder.isEmpty())
            return nullptr;
        level = &(*level)[index].folder;
    }
    if (m_key < 0 || m_key >= level->size())
        return nullptr;
    return &(*level)[m_key];
}

void *Controller::rawKey()
{
    return rawKeyAt(m_key);
}

void *Controller::rawKeyAt(int index)
{
    auto found = m_store->tables.find(m_profile.toStdString());
    if (found == m_store->tables.end())
        return nullptr;
    auto *pages = found->second["page"].as_array();
    if (!pages || m_page < 0 || m_page >= static_cast<int>(pages->size()))
        return nullptr;
    auto *pageTable = (*pages)[static_cast<size_t>(m_page)].as_table();
    if (!pageTable)
        return nullptr;
    auto *keys = (*pageTable)["keys"].as_array();
    if (!keys) {
        pageTable->insert_or_assign("keys", toml::array{});
        keys = (*pageTable)["keys"].as_array();
    }
    for (int slot : m_folderPath) {
        while (static_cast<int>(keys->size()) <= slot)
            keys->push_back(toml::table{});
        if (!(*keys)[static_cast<size_t>(slot)].is_table())
            keys->replace(keys->cbegin() + slot, toml::table{});
        auto *folderKey = (*keys)[static_cast<size_t>(slot)].as_table();
        auto *folder = (*folderKey)["folder"].as_array();
        if (!folder)
            return nullptr;
        keys = folder;
    }
    while (static_cast<int>(keys->size()) <= index)
        keys->push_back(toml::table{});
    if (!(*keys)[static_cast<size_t>(index)].is_table())
        keys->replace(keys->cbegin() + index, toml::table{});
    return (*keys)[static_cast<size_t>(index)].as_table();
}

void Controller::touch()
{
    if (!m_store->dirty.contains(m_profile))
        m_store->dirty.append(m_profile);
    m_dirty = true;
    ++m_revision;
    ++m_previewRevision;
    m_holdStatus = false;
    m_status = i18n("Unsaved changes");
    m_saveTimer.start();
    Q_EMIT changed();
    Q_EMIT previewChanged();
}

QString Controller::label() const
{
    const KeySpec *key = currentKey();
    return key ? key->label : QString();
}

void Controller::setLabel(const QString &label)
{
    KeySpec *key = currentKey();
    if (!key || key->label == label)
        return;
    key->label = label;
    if (auto *table = static_cast<toml::table *>(rawKey()))
        assignString(*table, "label", label);
    touch();
}

QString Controller::icon() const
{
    const KeySpec *key = currentKey();
    return key ? key->icon : QString();
}

void Controller::setIcon(const QString &icon)
{
    KeySpec *key = currentKey();
    if (!key || key->icon == icon)
        return;
    key->icon = icon;
    if (auto *table = static_cast<toml::table *>(rawKey()))
        assignString(*table, "icon", icon);
    touch();
}

QString Controller::image() const
{
    const KeySpec *key = currentKey();
    return key ? key->image : QString();
}

void Controller::setImageUrl(const QUrl &url)
{
    QString path = url.toLocalFile();
    const QString root = m_config.dir + QLatin1Char('/');
    if (path.startsWith(root))
        path = path.mid(root.size());
    KeySpec *key = currentKey();
    if (!key || key->image == path)
        return;
    key->image = path;
    if (auto *table = static_cast<toml::table *>(rawKey()))
        assignString(*table, "image", path);
    touch();
}

void Controller::clearImage()
{
    setImageUrl(QUrl());
}

QStringList Controller::actionKindIds() const
{
    return {QStringLiteral("none"),     QStringLiteral("key"),      QStringLiteral("shortcut"), QStringLiteral("sequence"),
            QStringLiteral("repeat"),   QStringLiteral("command"),  QStringLiteral("app"),      QStringLiteral("media"),
            QStringLiteral("page"),
            QStringLiteral("folder"),   QStringLiteral("back"),     QStringLiteral("profile"),  QStringLiteral("stat")};
}

QStringList Controller::actionKindLabels() const
{
    return {i18n("Nothing"),
            i18n("Press a key"),
            i18n("Shortcut"),
            i18n("Key sequence"),
            i18n("Repeat a key (toggle)"),
            i18n("Run a command"),
            i18n("Open an application"),
            i18n("Media control"),
            i18n("Go to page"),
            i18n("Folder"),
            i18n("Back"),
            i18n("Switch profile"),
            i18n("System stat")};
}

QString Controller::slotId() const
{
    QString id = m_profile + QLatin1Char('|') + QString::number(m_page) + QLatin1Char('|');
    for (int index : m_folderPath)
        id += QString::number(index) + QLatin1Char('.');
    return id + QLatin1Char('|') + QString::number(m_key);
}

QString Controller::editorKind(const KeySpec &key) const
{
    const QString stored = storedKind(key);
    if (stored != QLatin1String("none"))
        return stored;
    if (m_pickedSlot == slotId() && !m_pickedKind.isEmpty())
        return m_pickedKind;
    return QStringLiteral("none");
}

QString Controller::pressKindOf(const KeySpec &key) const
{
    const QString stored = storedPress(key);
    if (stored != QLatin1String("none"))
        return stored;
    if (m_pickedSlot == slotId() && !m_pickedPress.isEmpty())
        return m_pickedPress;
    return QStringLiteral("none");
}

QString Controller::activeValueKind(const KeySpec &key) const
{
    const QString kind = editorKind(key);
    return kind == QLatin1String("stat") ? pressKindOf(key) : kind;
}

void Controller::rememberKind(const QString &kind, bool press)
{
    m_pickedSlot = slotId();
    if (press)
        m_pickedPress = kind;
    else
        m_pickedKind = kind;
}

void Controller::mirrorCurrent(KeySpec &key)
{
    if (auto *table = static_cast<toml::table *>(rawKey()))
        mirrorKey(*table, key);
}

QString Controller::firstProfileName() const
{
    return m_config.profiles.isEmpty() ? QString() : m_config.profiles.first().name;
}

QString Controller::actionKindId() const
{
    const KeySpec *key = currentKey();
    return key ? editorKind(*key) : QStringLiteral("none");
}

int Controller::actionKindIndex() const
{
    const int index = actionKindIds().indexOf(actionKindId());
    return index < 0 ? 0 : index;
}

void Controller::setActionKindIndex(int index)
{
    KeySpec *key = currentKey();
    const QStringList ids = actionKindIds();
    if (!key || index < 0 || index >= ids.size())
        return;
    const QString kind = ids.at(index);
    if (editorKind(*key) == kind)
        return;
    // Switching to a stat keeps the press action. Every other kind replaces it.
    if (kind == QLatin1String("stat")) {
        if (key->stat.isEmpty()) {
            key->stat = QStringLiteral("cpu");
            if (key->label.isEmpty())
                key->label = statLabelFor(QStringLiteral("cpu"));
        }
    } else {
        keepAction(*key, kind, false);
        applyKindDefaults(*key, kind, firstProfileName());
        if (kind == QLatin1String("folder") && key->folder.isEmpty()) {
            fillNewFolder(*key);
            if (key->label.isEmpty())
                key->label = QStringLiteral("Folder");
        }
        if (kind == QLatin1String("back")) {
            key->back = true;
            if (key->label.isEmpty())
                key->label = QStringLiteral("Back");
            if (key->icon.isEmpty() && key->image.isEmpty())
                key->icon = backIconGlyph();
        }
    }
    rememberKind(kind, false);
    // A stat key's press action comes from the fields that were kept, not from
    // whichever passthrough was picked on a previous key.
    m_pickedPress.clear();
    mirrorCurrent(*key);
    touch();
}

QString Controller::actionValue() const
{
    const KeySpec *key = currentKey();
    if (!key)
        return {};
    return valueOf(*key, activeValueKind(*key));
}

void Controller::setActionValue(const QString &value)
{
    KeySpec *key = currentKey();
    if (!key)
        return;
    const bool passthrough = editorKind(*key) == QLatin1String("stat");
    const QString kind = activeValueKind(*key);
    const char *field = valueField(kind);
    if (!field || valueOf(*key, kind) == value)
        return;
    if (value.isEmpty())
        rememberKind(kind, passthrough);
    setValueOf(*key, kind, value);
    if (auto *table = static_cast<toml::table *>(rawKey()))
        assignString(*table, field, value);
    touch();
}

QString Controller::appId() const
{
    const KeySpec *key = currentKey();
    return key ? key->app : QString();
}

void Controller::setAppId(const QString &id)
{
    KeySpec *key = currentKey();
    if (!key || key->app == id)
        return;
    const QString previous = applicationName(key->app);
    key->app = id;
    const QString name = applicationName(id);
    if (!name.isEmpty() && (key->label.isEmpty() || key->label == previous))
        key->label = name;
    mirrorCurrent(*key);
    touch();
}

QString Controller::appArgs() const
{
    const KeySpec *key = currentKey();
    return key ? key->args : QString();
}

void Controller::setAppArgs(const QString &args)
{
    KeySpec *key = currentKey();
    if (!key || key->args == args)
        return;
    key->args = args;
    mirrorCurrent(*key);
    touch();
}

QString Controller::appLabel() const
{
    const KeySpec *key = currentKey();
    if (!key || key->app.isEmpty())
        return {};
    const QString name = applicationName(key->app);
    return name.isEmpty() ? key->app : name;
}

void Controller::refreshApplications()
{
    m_applications.clear();
    const QList<DesktopApp> apps = installedApplications();
    m_applications.reserve(apps.size());
    for (const DesktopApp &app : apps) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), app.id);
        row.insert(QStringLiteral("name"), app.name);
        row.insert(QStringLiteral("icon"), app.icon);
        m_applications.append(row);
    }
    Q_EMIT applicationsChanged();
}

QStringList Controller::pressKindIds() const
{
    return {QStringLiteral("none"),    QStringLiteral("key"),     QStringLiteral("shortcut"), QStringLiteral("sequence"),
            QStringLiteral("repeat"),  QStringLiteral("command"), QStringLiteral("app"),     QStringLiteral("media"),
            QStringLiteral("page"),
            QStringLiteral("profile")};
}

QStringList Controller::pressKindLabels() const
{
    return {i18n("Nothing"),
            i18n("Press a key"),
            i18n("Shortcut"),
            i18n("Key sequence"),
            i18n("Repeat a key (toggle)"),
            i18n("Run a command"),
            i18n("Open an application"),
            i18n("Media control"),
            i18n("Go to page"),
            i18n("Switch profile")};
}

QString Controller::pressKindId() const
{
    const KeySpec *key = currentKey();
    return key ? pressKindOf(*key) : QStringLiteral("none");
}

int Controller::pressKindIndex() const
{
    const int index = pressKindIds().indexOf(pressKindId());
    return index < 0 ? 0 : index;
}

void Controller::setPressKindIndex(int index)
{
    KeySpec *key = currentKey();
    const QStringList ids = pressKindIds();
    if (!key || key->stat.isEmpty() || index < 0 || index >= ids.size())
        return;
    const QString kind = ids.at(index);
    if (pressKindOf(*key) == kind)
        return;
    keepAction(*key, kind, true);
    applyKindDefaults(*key, kind, firstProfileName());
    rememberKind(kind, true);
    mirrorCurrent(*key);
    touch();
}

QStringList Controller::statIds() const
{
    QStringList ids;
    ids.reserve(static_cast<int>(std::size(kStats)) + 1);
    for (const StatRow &row : kStats)
        ids.append(QLatin1String(row.id));
    const KeySpec *key = currentKey();
    if (key && !key->stat.isEmpty() && !ids.contains(key->stat))
        ids.append(key->stat);
    return ids;
}

QStringList Controller::statTitles() const
{
    QStringList titles;
    titles.reserve(static_cast<int>(std::size(kStats)) + 1);
    for (const StatRow &row : kStats)
        titles.append(QString::fromUtf8(row.title));
    const KeySpec *key = currentKey();
    if (key && !key->stat.isEmpty()) {
        bool known = false;
        for (const StatRow &row : kStats) {
            if (key->stat == QLatin1String(row.id)) {
                known = true;
                break;
            }
        }
        if (!known)
            titles.append(key->stat);
    }
    return titles;
}

int Controller::statIndex() const
{
    const KeySpec *key = currentKey();
    const int index = statIds().indexOf(key ? key->stat : QString());
    return index < 0 ? 0 : index;
}

void Controller::setStatIndex(int index)
{
    KeySpec *key = currentKey();
    const QStringList ids = statIds();
    if (!key || index < 0 || index >= ids.size())
        return;
    const QString stat = ids.at(index);
    if (key->stat == stat)
        return;
    const QString previous = statLabelFor(key->stat);
    key->stat = stat;
    if (key->label.isEmpty() || key->label == previous)
        key->label = statLabelFor(stat);
    rememberKind(QStringLiteral("stat"), false);
    mirrorCurrent(*key);
    touch();
}

QStringList Controller::pageChoices() const
{
    QStringList names;
    if (const Profile *profile = m_config.profile(m_profile)) {
        for (const Page &page : profile->pages) {
            if (!page.settings)
                names.append(page.name);
        }
    }
    names << QStringLiteral("next") << QStringLiteral("previous") << QStringLiteral("back");
    return names;
}

QStringList Controller::mediaChoices() const
{
    return {QStringLiteral("play-pause"), QStringLiteral("next"), QStringLiteral("previous"), QStringLiteral("stop")};
}

int Controller::repeatMs() const
{
    const KeySpec *key = currentKey();
    return key ? key->repeatMs : 100;
}

void Controller::setRepeatMs(int value)
{
    KeySpec *key = currentKey();
    // A hidden interval spinner reports its minimum when the page changes.
    // Only a repeat action stores this number.
    if (!key || activeValueKind(*key) != QLatin1String("repeat"))
        return;
    value = std::clamp(value, 20, 10000);
    if (key->repeatMs == value)
        return;
    key->repeatMs = value;
    mirrorCurrent(*key);
    touch();
}

int Controller::stepMs() const
{
    const KeySpec *key = currentKey();
    return key ? key->stepMs : 40;
}

void Controller::setStepMs(int value)
{
    KeySpec *key = currentKey();
    if (!key || activeValueKind(*key) != QLatin1String("sequence"))
        return;
    value = std::clamp(value, 10, 500);
    if (key->stepMs == value)
        return;
    key->stepMs = value;
    mirrorCurrent(*key);
    touch();
}

QString Controller::hold() const
{
    const KeySpec *key = currentKey();
    return key ? key->hold : QString();
}

void Controller::setHold(const QString &hold)
{
    KeySpec *key = currentKey();
    if (!key || key->hold == hold || activeValueKind(*key) != QLatin1String("sequence"))
        return;
    key->hold = hold;
    if (auto *table = static_cast<toml::table *>(rawKey()))
        assignString(*table, "hold", hold);
    touch();
}

bool Controller::keyEditable() const
{
    const Profile *profile = m_config.profile(m_profile);
    if (!profile || m_page < 0 || m_page >= profile->pages.size())
        return false;
    return !profile->pages.at(m_page).settings;
}

void Controller::clearKey()
{
    KeySpec *key = currentKey();
    if (!key)
        return;
    if (key->isEmpty() && editorKind(*key) == QLatin1String("none"))
        return;
    *key = KeySpec{};
    m_pickedSlot = slotId();
    m_pickedKind.clear();
    m_pickedPress.clear();
    mirrorCurrent(*key);
    touch();
}

namespace {

enum class RecordOutcome { Ignore, Cancel, Accept };

struct RecordedKey {
    RecordOutcome outcome = RecordOutcome::Cancel;
    QString text;
};

RecordedKey interpretRecord(const QString &field, const QKeyEvent *event)
{
    const quint32 scan = event->nativeScanCode();
    QString name;
    if (scan >= 8) {
        if (const char *raw = libevdev_event_code_get_name(EV_KEY, static_cast<int>(scan - 8))) {
            name = QString::fromLatin1(raw);
            if (name.startsWith(QLatin1String("KEY_")))
                name = name.mid(4);
        }
    }
    const bool shortcut = field == QLatin1String("keys") || field == QLatin1String("repeat");
    const bool bareEscape = name == QLatin1String("ESC") && event->modifiers() == Qt::NoModifier;
    if (name.isEmpty() || (bareEscape && field == QLatin1String("key")))
        return {RecordOutcome::Cancel, {}};
    if (shortcut
        && (name == QLatin1String("LEFTCTRL") || name == QLatin1String("RIGHTCTRL") || name == QLatin1String("LEFTSHIFT")
            || name == QLatin1String("RIGHTSHIFT") || name == QLatin1String("LEFTALT") || name == QLatin1String("RIGHTALT")
            || name == QLatin1String("LEFTMETA") || name == QLatin1String("RIGHTMETA")))
        return {RecordOutcome::Ignore, {}};
    QStringList parts;
    if (shortcut) {
        const Qt::KeyboardModifiers mods = event->modifiers();
        if (mods.testFlag(Qt::ControlModifier))
            parts.append(QStringLiteral("CTRL"));
        if (mods.testFlag(Qt::ShiftModifier))
            parts.append(QStringLiteral("SHIFT"));
        if (mods.testFlag(Qt::AltModifier))
            parts.append(QStringLiteral("ALT"));
        if (mods.testFlag(Qt::MetaModifier))
            parts.append(QStringLiteral("SUPER"));
    }
    parts.append(name);
    return {RecordOutcome::Accept, parts.join(QLatin1Char('+'))};
}

} // namespace

void Controller::beginRecord(const QString &field)
{
    if (field != QLatin1String("key") && field != QLatin1String("keys") && field != QLatin1String("repeat")
        && field != QLatin1String("hold"))
        return;
    m_recordField = field;
    m_recording = true;
}

void Controller::cancelRecord()
{
    m_recording = false;
}

bool Controller::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    if (event->type() == QEvent::ApplicationFontChange) {
        invalidateTextFonts();
        ++m_revision;
        ++m_previewRevision;
        Q_EMIT changed();
        Q_EMIT previewChanged();
    }
    if (!m_recording || event->type() != QEvent::KeyPress)
        return false;
    const auto *keyEvent = static_cast<const QKeyEvent *>(event);
    if (keyEvent->isAutoRepeat())
        return true;
    const RecordedKey recorded = interpretRecord(m_recordField, keyEvent);
    if (recorded.outcome == RecordOutcome::Ignore)
        return true;
    m_recording = false;
    if (recorded.outcome == RecordOutcome::Accept) {
        if (m_recordField == QLatin1String("hold"))
            setHold(recorded.text);
        else
            setActionValue(recorded.text);
    }
    Q_EMIT recordFinished();
    return true;
}

void Controller::swapKeys(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= Keys || to >= Keys)
        return;
    Profile *profile = m_config.profile(m_profile);
    if (!profile || m_page < 0 || m_page >= profile->pages.size())
        return;
    Page &page = profile->pages[m_page];
    if (page.settings)
        return;
    QList<KeySpec> *level = &page.keys;
    for (int slot : m_folderPath) {
        if (slot < 0 || slot >= level->size() || level->at(slot).folder.isEmpty())
            return;
        level = &(*level)[slot].folder;
    }
    while (level->size() < Keys)
        level->append(KeySpec{});
    if (level->at(from).isEmpty() && level->at(to).isEmpty())
        return;
    std::swap((*level)[from], (*level)[to]);
    if (auto *table = static_cast<toml::table *>(rawKeyAt(from)))
        mirrorKey(*table, level->at(from));
    if (auto *table = static_cast<toml::table *>(rawKeyAt(to)))
        mirrorKey(*table, level->at(to));
    m_key = to;
    touch();
}

void Controller::rebuildIconRows()
{
    const QString query = m_iconFilter.trimmed().toLower().replace(QLatin1Char(' '), QLatin1Char('_'));
    m_iconRows.clear();
    for (int index = 0; index < m_icons.size(); ++index) {
        if (query.isEmpty() || m_icons.at(index).name.toLower().contains(query))
            m_iconRows.append(index);
    }
}

void Controller::setIconFilter(const QString &text)
{
    if (m_iconFilter == text)
        return;
    m_iconFilter = text;
    rebuildIconRows();
    Q_EMIT iconsChanged();
}

void Controller::ensureIcons()
{
    if (m_iconsLoaded)
        return;
    m_iconsLoaded = true;
    QProcess match;
    match.start(QStringLiteral("fc-match"), {QStringLiteral("-f"), QStringLiteral("%{file}"), QStringLiteral("Symbols Nerd Font")});
    if (match.waitForFinished(2000) && match.exitCode() == 0) {
        const QString font = QString::fromUtf8(match.readAllStandardOutput()).trimmed();
        const QString script = QStringLiteral(
            "import sys\n"
            "from fontTools.ttLib import TTFont\n"
            "cmap = TTFont(sys.argv[1], lazy=True).getBestCmap() or {}\n"
            "rows = sorted(((name, cp) for cp, name in cmap.items() if isinstance(cp, int) and cp > 0xE000), key=lambda item: item[0])\n"
            "out = sys.stdout.buffer\n"
            "for name, cp in rows:\n"
            "    out.write(chr(cp).encode('utf-8') + b'\\t' + str(name).encode('utf-8') + b'\\n')\n");
        QProcess python;
        python.start(QStringLiteral("python3"), {QStringLiteral("-c"), script, font});
        if (python.waitForFinished(8000) && python.exitCode() == 0) {
            const QStringList lines = QString::fromUtf8(python.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            m_icons.reserve(lines.size());
            for (const QString &line : lines) {
                const int tab = line.indexOf(QLatin1Char('\t'));
                if (tab <= 0)
                    continue;
                IconEntry entry;
                entry.glyph = line.left(tab);
                entry.name = line.mid(tab + 1);
                const int dash = entry.name.indexOf(QLatin1Char('-'));
                entry.label = dash < 0 ? entry.name : entry.name.mid(dash + 1);
                m_icons.append(entry);
            }
        }
    }
    if (m_icons.isEmpty()) {
        const struct {
            char32_t code;
            const char *name;
        } fallback[] = {
            {0xF120, "fa-terminal"}, {0xF07B, "fa-folder"}, {0xF269, "fa-firefox"}, {0xF1B6, "fa-steam"},
            {0xF066F, "md-discord"}, {0xF040E, "md-play_pause"}, {0xF04AE, "md-skip_previous"},
            {0xF04AD, "md-skip_next"}, {0xF04DB, "md-stop"}, {0xF036D, "md-microphone_off"},
            {0xF0100, "md-camera"}, {0xF02B4, "md-google_controller"}, {0xF004D, "md-arrow_left"},
            {0xF02DC, "md-home"}, {0xF0311, "md-keyboard_return"}, {0xF003B, "md-apps"},
        };
        for (const auto &row : fallback) {
            IconEntry entry;
            entry.glyph = QString::fromUcs4(&row.code, 1);
            entry.name = QString::fromLatin1(row.name);
            const int dash = entry.name.indexOf(QLatin1Char('-'));
            entry.label = dash < 0 ? entry.name : entry.name.mid(dash + 1);
            m_icons.append(entry);
        }
    }
    rebuildIconRows();
    Q_EMIT iconsChanged();
}

QString Controller::iconGlyph(int index) const
{
    if (index < 0 || index >= m_iconRows.size())
        return {};
    return m_icons.at(m_iconRows.at(index)).glyph;
}

QString Controller::iconLabel(int index) const
{
    if (index < 0 || index >= m_iconRows.size())
        return {};
    return m_icons.at(m_iconRows.at(index)).label;
}

QString Controller::iconName(int index) const
{
    if (index < 0 || index >= m_iconRows.size())
        return {};
    return m_icons.at(m_iconRows.at(index)).name;
}

bool Controller::confirm() const
{
    const KeySpec *key = currentKey();
    return key && key->confirm;
}

void Controller::setConfirm(bool confirm)
{
    KeySpec *key = currentKey();
    if (!key || key->confirm == confirm)
        return;
    key->confirm = confirm;
    if (auto *table = static_cast<toml::table *>(rawKey())) {
        if (confirm)
            table->insert_or_assign("confirm", true);
        else
            table->erase("confirm");
    }
    touch();
}

QString Controller::special() const
{
    const KeySpec *key = currentKey();
    if (!key)
        return {};
    if (key->back)
        return i18n("This key steps back out of a folder.");
    if (!key->folder.isEmpty())
        return i18n("This key opens a folder. Its label and icon can change here. The keys inside it stay as they are.");
    if (!key->stat.isEmpty())
        return i18n("This key shows the system stat “%1”. Pressing it still runs the other action on the key.", key->stat);
    return {};
}

void Controller::setColour(const char *field, QString *slot, const QString &value)
{
    if (!slot || *slot == value)
        return;
    *slot = value;
    if (auto *table = static_cast<toml::table *>(rawKey()))
        assignString(*table, field, value);
    touch();
}

QString Controller::fg() const
{
    const KeySpec *key = currentKey();
    return key ? key->fg : QString();
}

void Controller::setFg(const QString &value)
{
    if (KeySpec *key = currentKey())
        setColour("fg", &key->fg, value);
}

QString Controller::bg() const
{
    const KeySpec *key = currentKey();
    return key ? key->bg : QString();
}

void Controller::setBg(const QString &value)
{
    if (KeySpec *key = currentKey())
        setColour("bg", &key->bg, value);
}

QString Controller::border() const
{
    const KeySpec *key = currentKey();
    return key ? key->border : QString();
}

void Controller::setBorder(const QString &value)
{
    if (KeySpec *key = currentKey())
        setColour("border", &key->border, value);
}

bool Controller::canOpenFolder() const
{
    const KeySpec *key = currentKey();
    return key && !key->folder.isEmpty();
}

QStringList Controller::folderCrumbs() const
{
    const Profile *profile = m_config.profile(m_profile);
    if (!profile || m_page < 0 || m_page >= profile->pages.size())
        return {};
    const Page &page = profile->pages.at(m_page);
    QStringList names{page.name};
    QList<KeySpec> keys = page.keys;
    for (int index : m_folderPath) {
        if (index < 0 || index >= keys.size() || keys.at(index).folder.isEmpty())
            break;
        const QString label = keys.at(index).label;
        names.append(label.isEmpty() ? QStringLiteral("Folder") : label);
        keys = keys.at(index).folder;
    }
    return names;
}

QString Controller::place() const
{
    const Profile *profile = m_config.profile(m_profile);
    if (!profile || m_page < 0 || m_page >= profile->pages.size())
        return m_profile;
    const Page &page = profile->pages.at(m_page);
    return m_profile + QStringLiteral("  /  ") + placeLabel(page.name, page.keys, m_folderPath);
}

void Controller::openFolder(int index)
{
    const QList<KeySpec> keys = visibleKeys();
    if (index < 0 || index >= keys.size() || keys.at(index).folder.isEmpty())
        return;
    m_folderPath.append(index);
    m_key = 0;
    ++m_revision;
    ++m_previewRevision;
    Q_EMIT changed();
    Q_EMIT previewChanged();
}

void Controller::closeFolder()
{
    if (m_folderPath.isEmpty())
        return;
    showFolderDepth(m_folderPath.size() - 1);
}

void Controller::showFolderDepth(int depth)
{
    // Same as the GTK folder bar: depth 0 is the page, and the key that led
    // into the level you left stays selected.
    if (depth < 0 || depth >= m_folderPath.size())
        return;
    const int select = m_folderPath.at(depth);
    m_folderPath = m_folderPath.mid(0, depth);
    m_key = select;
    ++m_revision;
    ++m_previewRevision;
    Q_EMIT changed();
    Q_EMIT previewChanged();
}

QStringList Controller::themes() const
{
    return themeNames(m_config);
}

QString Controller::shownTheme(const Profile &profile) const
{
    if (!profile.followTheme)
        return profile.themeName;
    if (!profile.theme.name.isEmpty())
        return profile.theme.name;
    return QStringLiteral("amber");
}

QString Controller::theme() const
{
    const Profile *profile = m_config.profile(m_profile);
    return profile ? shownTheme(*profile) : QString();
}

bool Controller::followTheme() const
{
    const Profile *profile = m_config.profile(m_profile);
    return profile && profile->followTheme;
}

void Controller::setTheme(const QString &name)
{
    Profile *profile = m_config.profile(m_profile);
    auto found = m_store->tables.find(m_profile.toStdString());
    if (!profile || found == m_store->tables.end() || !m_config.themes.contains(name))
        return;
    if (!profile->followTheme && profile->themeName == name)
        return;

    // Picking the theme already on screen keeps its colour tweaks, so the keys
    // don't jump back to that theme's stock palette.
    if (profile->followTheme && name == shownTheme(*profile)) {
        const Theme stock = m_config.themes.value(name);
        toml::table pinned;
        if (const toml::table *existing = found->second["theme_overrides"].as_table())
            pinned = *existing;
        auto pin = [&](const char *field, const QColor &current, const QColor &base) {
            if (pinned.contains(field))
                return;
            if (current.name(QColor::HexRgb) == base.name(QColor::HexRgb))
                return;
            pinned.insert(field, current.name(QColor::HexRgb).toStdString());
        };
        pin("bg", profile->theme.bg, stock.bg);
        pin("key_bg", profile->theme.keyBg, stock.keyBg);
        pin("fg", profile->theme.fg, stock.fg);
        pin("accent", profile->theme.accent, stock.accent);
        pin("frame", profile->theme.frame, stock.frame);
        pin("dim", profile->theme.dim, stock.dim);
        pin("border", profile->theme.border, stock.border);
        if (!pinned.empty())
            found->second.insert_or_assign("theme_overrides", std::move(pinned));
    }

    profile->themeName = name;
    profile->followTheme = false;
    profile->followTarget.clear();
    found->second.insert_or_assign("theme", name.toStdString());
    found->second.erase("follow_theme");
    rebindThemeColours(*profile);
    touch();
}

void Controller::rebindThemeColours(Profile &profile)
{
    auto found = m_store->tables.find(profile.name.toStdString());
    Theme base;
    if (profile.followTheme) {
        const QString sourceName = profile.followTarget.isEmpty() ? m_config.startProfile : profile.followTarget;
        const Profile *source = sourceName == profile.name ? nullptr : m_config.profile(sourceName);
        base = source ? source->theme : m_config.themes.value(QStringLiteral("amber"));
    } else if (m_config.themes.contains(profile.themeName)) {
        base = m_config.themes.value(profile.themeName);
    } else {
        base = m_config.themes.value(QStringLiteral("amber"));
    }
    profile.theme = base;
    if (found != m_store->tables.end()) {
        if (const toml::table *overrides = found->second["theme_overrides"].as_table())
            applyOverrides(profile.theme, *overrides);
    }
}

QString Controller::logo() const
{
    const Profile *profile = m_config.profile(m_profile);
    return profile ? profile->logo : QString();
}

void Controller::setLogo(const QString &path)
{
    Profile *profile = m_config.profile(m_profile);
    auto found = m_store->tables.find(m_profile.toStdString());
    if (!profile || found == m_store->tables.end() || profile->logo == path)
        return;
    profile->logo = path;
    assignString(found->second, "logo", path);
    touch();
}

void Controller::setLogoUrl(const QUrl &url)
{
    const QString path = url.toLocalFile();
    if (!path.isEmpty())
        setLogo(path);
}

QString Controller::followSource() const
{
    QString name = m_profile;
    QStringList seen;
    while (!seen.contains(name)) {
        const Profile *profile = m_config.profile(name);
        if (!profile)
            return {};
        if (!profile->followTheme)
            return name == m_profile ? QString() : name;
        seen.append(name);
        const QString next = profile->followTarget.isEmpty() ? m_config.startProfile : profile->followTarget;
        if (next.isEmpty() || next == name)
            return {};
        name = next;
    }
    return {};
}

QString Controller::themeColour(const QString &field) const
{
    const Profile *profile = m_config.profile(m_profile);
    const QColor *slot = profile ? themeSlot(profile->theme, field) : nullptr;
    return slot ? slot->name(QColor::HexRgb) : QStringLiteral("#888888");
}

bool Controller::themeTweaked(const QString &field) const
{
    if (!themeSlot(Theme{}, field))
        return false;
    auto found = m_store->tables.find(m_profile.toStdString());
    if (found == m_store->tables.end())
        return false;
    const toml::table *overrides = found->second["theme_overrides"].as_table();
    return overrides && overrides->contains(field.toStdString());
}

void Controller::setThemeColour(const QString &field, const QString &hexText)
{
    if (!themeSlot(Theme{}, field))
        return;
    const QString hex = normalHex(hexText);
    if (hex.isEmpty())
        return;
    Profile *profile = m_config.profile(m_profile);
    auto found = m_store->tables.find(m_profile.toStdString());
    if (!profile || found == m_store->tables.end())
        return;
    const QColor *slot = themeSlot(profile->theme, field);
    if (!slot)
        return;
    QString stored;
    if (const toml::table *existing = found->second["theme_overrides"].as_table()) {
        if (auto value = (*existing)[field.toStdString()].value<std::string>())
            stored = QString::fromStdString(*value);
    }
    if (hex == stored || (stored.isEmpty() && hex == slot->name(QColor::HexRgb)))
        return;
    toml::table overrides;
    if (const toml::table *existing = found->second["theme_overrides"].as_table())
        overrides = *existing;
    overrides.insert_or_assign(field.toStdString(), hex.toStdString());
    found->second.insert_or_assign("theme_overrides", std::move(overrides));
    rebindThemeColours(*profile);
    touch();
}

void Controller::resetThemeColour(const QString &field)
{
    if (!themeSlot(Theme{}, field))
        return;
    Profile *profile = m_config.profile(m_profile);
    auto found = m_store->tables.find(m_profile.toStdString());
    if (!profile || found == m_store->tables.end())
        return;
    toml::table *overrides = found->second["theme_overrides"].as_table();
    if (!overrides || !overrides->contains(field.toStdString()))
        return;
    overrides->erase(field.toStdString());
    if (overrides->empty())
        found->second.erase("theme_overrides");
    rebindThemeColours(*profile);
    touch();
}

bool Controller::writeSectionKey(const QString &table, const QString &key, const QString &literal, bool remove)
{
    const bool ok = remove ? removeTableKey(m_config.path, table, key)
                           : (replaceTableValue(m_config.path, table, key, literal)
                              || assignTableKey(m_config.path, table, key, literal));
    if (!ok) {
        m_status = i18n("Could not save config.toml");
        Q_EMIT changed();
        return false;
    }
    m_seenStamp = configStamp();
    return true;
}

void Controller::setWallpaperAuto(bool on)
{
    if (on == m_config.wallpaperAuto)
        return;
    if (!writeSectionKey(QStringLiteral("wallpaper_theme"), QStringLiteral("auto"), on ? QStringLiteral("true") : QStringLiteral("false"), false))
        return;
    m_config.wallpaperAuto = on;
    m_status = on ? i18n("Wallpaper theme will follow wallpaper changes") : i18n("Wallpaper theme will not follow changes");
    Q_EMIT changed();
}

void Controller::setWallpaperCommand(const QString &command)
{
    if (command == m_config.wallpaperCommand)
        return;
    const bool remove = command.trimmed().isEmpty();
    if (!writeSectionKey(QStringLiteral("wallpaper_theme"), QStringLiteral("command"), remove ? QString() : tomlString(command), remove))
        return;
    m_config.wallpaperCommand = remove ? QString() : command;
    m_status = i18n("Wallpaper command saved");
    Q_EMIT changed();
}

QString Controller::textFontFamily() const
{
    return textFont(QString(), 10).family();
}

QString Controller::fixedFontFamily() const
{
    return fixedTextFont(QString(), 10).family();
}

void Controller::setWallpaperFont(const QString &font)
{
    if (font == m_config.wallpaperFont)
        return;
    if (!writeSectionKey(QStringLiteral("wallpaper_theme"), QStringLiteral("font"), tomlString(font), false))
        return;
    m_config.wallpaperFont = font;
    m_status = i18n("Wallpaper font saved");
    Q_EMIT changed();
}

void Controller::setWallpaperClockFont(const QString &font)
{
    if (font == m_config.wallpaperClockFont)
        return;
    if (!writeSectionKey(QStringLiteral("wallpaper_theme"), QStringLiteral("clock_font"), tomlString(font), false))
        return;
    m_config.wallpaperClockFont = font;
    m_status = i18n("Wallpaper clock font saved");
    Q_EMIT changed();
}

namespace {

bool near(double a, double b)
{
    return std::abs(a - b) < 0.001;
}

QString decimal(double value, int places)
{
    return QString::number(value, 'f', places);
}

} // namespace

void Controller::setTransitionEnabled(bool on)
{
    if (on == m_config.transitionEnabled)
        return;
    if (!writeSectionKey(QStringLiteral("transition"), QStringLiteral("enabled"), on ? QStringLiteral("true") : QStringLiteral("false"), false))
        return;
    m_config.transitionEnabled = on;
    m_status = i18n("Profile switch saved");
    Q_EMIT changed();
}

void Controller::setTransitionSpeed(double value)
{
    value = std::clamp(value, 0.25, 4.0);
    if (near(value, m_config.transitionSpeed))
        return;
    if (!writeSectionKey(QStringLiteral("transition"), QStringLiteral("speed"), decimal(value, 2), false))
        return;
    m_config.transitionSpeed = value;
    m_status = i18n("Profile switch saved");
    Q_EMIT changed();
}

void Controller::setTransitionHold(double value)
{
    value = std::clamp(value, 0.0, 3.0);
    if (near(value, m_config.transitionHold))
        return;
    if (!writeSectionKey(QStringLiteral("transition"), QStringLiteral("hold"), decimal(value, 1), false))
        return;
    m_config.transitionHold = value;
    m_status = i18n("Profile switch saved");
    Q_EMIT changed();
}

void Controller::setBootEnabled(bool on)
{
    if (on == m_config.bootEnabled)
        return;
    if (!writeSectionKey(QStringLiteral("boot"), QStringLiteral("enabled"), on ? QStringLiteral("true") : QStringLiteral("false"), false))
        return;
    m_config.bootEnabled = on;
    m_status = i18n("Boot animation saved");
    Q_EMIT changed();
}

void Controller::setBootSpeed(double value)
{
    value = std::clamp(value, 0.25, 2.0);
    if (near(value, m_config.bootSpeed))
        return;
    if (!writeSectionKey(QStringLiteral("boot"), QStringLiteral("speed"), decimal(value, 2), false))
        return;
    m_config.bootSpeed = value;
    m_status = i18n("Boot animation saved");
    Q_EMIT changed();
}

void Controller::setBootTitle(const QString &title)
{
    if (title == m_config.bootTitle)
        return;
    if (!writeSectionKey(QStringLiteral("boot"), QStringLiteral("title"), tomlString(title), false))
        return;
    m_config.bootTitle = title;
    m_status = i18n("Boot animation saved");
    Q_EMIT changed();
}

QString Controller::bootLinesText() const
{
    return m_config.bootLines.join(QStringLiteral("; "));
}

void Controller::setBootLinesText(const QString &text)
{
    QStringList lines;
    for (const QString &part : text.split(QLatin1Char(';'))) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            lines.append(trimmed);
    }
    if (lines.size() > 6)
        lines.resize(6);
    if (lines == m_config.bootLines)
        return;
    QStringList quoted;
    for (const QString &line : lines)
        quoted.append(tomlString(line));
    const QString literal = QStringLiteral("[\n  ") + quoted.join(QStringLiteral(",\n  ")) + QStringLiteral(",\n]");
    if (!writeSectionKey(QStringLiteral("boot"), QStringLiteral("lines"), literal, false))
        return;
    m_config.bootLines = lines;
    m_status = i18n("Boot animation saved");
    Q_EMIT changed();
}

void Controller::setBootLogo(const QString &path)
{
    if (path == m_config.bootLogo)
        return;
    const bool remove = path.isEmpty();
    if (!writeSectionKey(QStringLiteral("boot"), QStringLiteral("logo"), remove ? QString() : tomlString(path), remove))
        return;
    m_config.bootLogo = path;
    m_status = i18n("Boot animation saved");
    Q_EMIT changed();
}

void Controller::setBootGif(const QString &path)
{
    if (path == m_config.bootGif)
        return;
    const bool remove = path.isEmpty();
    if (!writeSectionKey(QStringLiteral("boot"), QStringLiteral("gif"), remove ? QString() : tomlString(path), remove))
        return;
    m_config.bootGif = path;
    m_status = i18n("Boot animation saved");
    Q_EMIT changed();
}

void Controller::setBootLogoUrl(const QUrl &url)
{
    const QString path = url.toLocalFile();
    if (!path.isEmpty())
        setBootLogo(path);
}

void Controller::setBootGifUrl(const QUrl &url)
{
    const QString path = url.toLocalFile();
    if (!path.isEmpty())
        setBootGif(path);
}

QVariantList Controller::switchRules() const
{
    QVariantList list;
    for (const SwitchRule &rule : m_config.autoRules) {
        QVariantMap row;
        row.insert(QStringLiteral("pattern"), rule.classPattern);
        row.insert(QStringLiteral("profile"), rule.profile);
        row.insert(QStringLiteral("title"), rule.titlePattern);
        list.append(row);
    }
    return list;
}

QVariantList Controller::profileSwitchRules() const
{
    QVariantList list;
    for (const Profile &profile : m_config.profiles) {
        for (const SwitchRule &rule : profile.autoSwitch) {
            QStringList parts;
            if (!rule.classPattern.isEmpty())
                parts.append(QStringLiteral("class ") + rule.classPattern);
            if (!rule.titlePattern.isEmpty())
                parts.append(QStringLiteral("title ") + rule.titlePattern);
            const QString what = parts.isEmpty() ? i18n("any window") : parts.join(QStringLiteral(" + "));
            QVariantMap row;
            row.insert(QStringLiteral("text"), QStringLiteral("%1 → %2").arg(what, profile.name));
            row.insert(QStringLiteral("subtitle"), i18n("From profiles/%1.toml; checked before the rules below", profile.name));
            list.append(row);
        }
    }
    return list;
}

QString Controller::focusHint() const
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString source;
    if (!env.value(QStringLiteral("HYPRLAND_INSTANCE_SIGNATURE")).isEmpty())
        source = i18n("Hyprland");
    else if (!env.value(QStringLiteral("SWAYSOCK")).isEmpty())
        source = i18n("Sway");
    else if (!env.value(QStringLiteral("I3SOCK")).isEmpty())
        source = i18n("i3");
    else if (!env.value(QStringLiteral("NIRI_SOCKET")).isEmpty())
        source = i18n("niri");
    else if (env.value(QStringLiteral("XDG_SESSION_TYPE")) == QLatin1String("x11")
             || (!env.value(QStringLiteral("DISPLAY")).isEmpty() && env.value(QStringLiteral("WAYLAND_DISPLAY")).isEmpty()))
        source = i18n("X11");
    if (!source.isEmpty()) {
        return i18n("Switch profile by the focused window's class (a regular expression). Following window focus on %1.", source);
    }
    return i18n("Switch profile by the focused window's class (a regular expression). Not available on this desktop yet: GNOME and KDE Plasma under Wayland aren't supported. Works on Hyprland, Sway, i3, niri and X11 desktops.");
}

bool Controller::writeAutoRules(const QList<SwitchRule> &rules)
{
    return writeSectionKey(QStringLiteral("auto_switch"), QStringLiteral("rules"), switchRulesLiteral(rules), false);
}

void Controller::setAutoFallback(const QString &name)
{
    if (name == m_config.autoFallback)
        return;
    if (!name.isEmpty() && !m_config.profile(name))
        return;
    const bool remove = name.isEmpty();
    if (!writeSectionKey(QStringLiteral("auto_switch"), QStringLiteral("fallback"), remove ? QString() : tomlString(name), remove))
        return;
    m_config.autoFallback = name;
    m_status = i18n("Auto-switch saved");
    Q_EMIT changed();
}

void Controller::addSwitchRule()
{
    if (m_config.profiles.isEmpty())
        return;
    SwitchRule rule;
    rule.classPattern = QStringLiteral("^steam_app_");
    rule.profile = m_config.profiles.first().name;
    QList<SwitchRule> rules = m_config.autoRules;
    rules.append(rule);
    if (!writeAutoRules(rules))
        return;
    m_config.autoRules = rules;
    m_status = i18n("Auto-switch saved");
    Q_EMIT changed();
}

void Controller::removeSwitchRule(int index)
{
    if (index < 0 || index >= m_config.autoRules.size())
        return;
    QList<SwitchRule> rules = m_config.autoRules;
    rules.removeAt(index);
    if (!writeAutoRules(rules))
        return;
    m_config.autoRules = rules;
    m_status = i18n("Auto-switch saved");
    Q_EMIT changed();
}

void Controller::setSwitchRuleClass(int index, const QString &pattern)
{
    if (index < 0 || index >= m_config.autoRules.size())
        return;
    if (m_config.autoRules.at(index).classPattern == pattern)
        return;
    QList<SwitchRule> rules = m_config.autoRules;
    rules[index].classPattern = pattern;
    if (!writeAutoRules(rules))
        return;
    m_config.autoRules = rules;
    m_status = i18n("Auto-switch saved");
    Q_EMIT changed();
}

void Controller::setSwitchRuleProfile(int index, const QString &profile)
{
    if (index < 0 || index >= m_config.autoRules.size())
        return;
    if (m_config.autoRules.at(index).profile == profile)
        return;
    if (!m_config.profile(profile))
        return;
    QList<SwitchRule> rules = m_config.autoRules;
    rules[index].profile = profile;
    if (!writeAutoRules(rules))
        return;
    m_config.autoRules = rules;
    m_status = i18n("Auto-switch saved");
    Q_EMIT changed();
}

void Controller::replayBoot()
{
    restartBackend(true);
}

void Controller::restartService()
{
    restartBackend(false);
}

void Controller::restartBackend(bool replay)
{
    flushEdits();
    QProcess unit;
    unit.start(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("cat"), QStringLiteral("kdeck-control.service")});
    const bool installed = unit.waitForFinished(1500) && unit.exitCode() == 0;
    bool ok = false;
    QString err;
    if (installed) {
        QProcess process;
        process.start(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("restart"), QStringLiteral("kdeck-control.service")});
        ok = process.waitForFinished(8000) && process.exitCode() == 0;
        if (!ok)
            err = QString::fromUtf8(process.readAllStandardError()).trimmed();
    } else {
        QFile pidFile(runtimePath(QStringLiteral("kdeck-control.pid")));
        if (pidFile.open(QIODevice::ReadOnly)) {
            bool parsed = false;
            const qint64 pid = QString::fromUtf8(pidFile.readAll()).trimmed().toLongLong(&parsed);
            if (parsed && pid > 0)
                ::kill(static_cast<pid_t>(pid), SIGTERM);
        }
        QThread::sleep(1);
        const QString exe = QStandardPaths::findExecutable(QStringLiteral("kdeck-control"));
        if (exe.isEmpty())
            err = i18n("kdeck-control is not on PATH");
        else
            ok = QProcess::startDetached(exe);
    }
    if (!ok)
        m_status = err.isEmpty() ? i18n("Restart failed") : i18n("Restart failed: %1", err);
    else
        m_status = replay ? i18n("Restarting the service — watch the deck") : i18n("Service restarted");
    m_holdStatus = true;
    Q_EMIT changed();
}

void Controller::openConfigFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_config.dir));
}

void Controller::pollService()
{
    QProcess process;
    process.start(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("is-active"), QStringLiteral("--quiet"), QStringLiteral("kdeck-control.service")});
    bool running = process.waitForFinished(1500) && process.exitCode() == 0;
    if (!running) {
        QFile pidFile(runtimePath(QStringLiteral("kdeck-control.pid")));
        if (pidFile.open(QIODevice::ReadOnly)) {
            bool parsed = false;
            const qint64 pid = QString::fromUtf8(pidFile.readAll()).trimmed().toLongLong(&parsed);
            if (parsed && pid > 0 && (::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM))
                running = true;
        }
    }
    if (running == m_serviceRunning)
        return;
    m_serviceRunning = running;
    Q_EMIT serviceRunningChanged();
}

bool Controller::runPythonDeck(const QString &body, QString *output, int timeoutMs)
{
    const QString scriptPath = addonPython();
    if (scriptPath.isEmpty())
        return false;
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONPATH"), QFileInfo(scriptPath).absolutePath());
    process.setProcessEnvironment(env);
    const QString script = QStringLiteral("import kdeck_control as g\n"
                                          "cfg = {}\n"
                                          "try:\n"
                                          "    import tomllib\n"
                                          "    with open(g.CONFIG_PATH, 'rb') as f:\n"
                                          "        cfg = tomllib.load(f).get('wallpaper_theme', {}) or {}\n"
                                          "except Exception:\n"
                                          "    pass\n")
        + body;
    process.start(QStringLiteral("python3"), {QStringLiteral("-c"), script});
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    if (output)
        *output = QString::fromUtf8(process.readAllStandardOutput());
    return process.exitCode() == 0;
}

void Controller::refreshWallpaperHint()
{
    const QString missing = i18n("No wallpaper found — set the wallpaper command in Settings");
    QString output;
    if (!runPythonDeck(QStringLiteral("print(g.wallpaper_path(cfg) or '')\n"), &output, 4000))
        m_wallpaperHint = missing;
    else {
        const QString path = output.trimmed();
        m_wallpaperHint = path.isEmpty() ? missing : path;
    }
    Q_EMIT changed();
}

void Controller::generateWallpaperTheme()
{
    const QString missing = i18n("No wallpaper found — set the wallpaper command in Settings");
    if (!runPythonDeck(QStringLiteral("raise SystemExit(0 if g.write_wallpaper_theme(cfg) else 1)\n"), nullptr, 8000)) {
        m_holdStatus = true;
        m_status = missing;
        refreshWallpaperHint();
        return;
    }
    const Config fresh = Config::load();
    if (!fresh.themes.contains(QStringLiteral("wallpaper"))) {
        m_holdStatus = true;
        m_status = missing;
        Q_EMIT changed();
        return;
    }
    m_config.themes = fresh.themes;
    Profile *profile = m_config.profile(m_profile);
    if (profile && !profile->followTheme && profile->themeName == QLatin1String("wallpaper")) {
        rebindThemeColours(*profile);
        ++m_revision;
        ++m_previewRevision;
        m_holdStatus = true;
        m_status = i18n("Theme generated from your wallpaper");
        Q_EMIT changed();
        Q_EMIT previewChanged();
        refreshWallpaperHint();
        return;
    }
    setTheme(QStringLiteral("wallpaper"));
    m_holdStatus = true;
    m_status = i18n("Theme generated from your wallpaper");
    refreshWallpaperHint();
}

QString Controller::startPage() const
{
    const Profile *profile = m_config.profile(m_profile);
    return profile ? profile->startPage : QString();
}

void Controller::setStartPage(const QString &name)
{
    Profile *profile = m_config.profile(m_profile);
    auto found = m_store->tables.find(m_profile.toStdString());
    if (!profile || found == m_store->tables.end() || profile->startPage == name || !pages().contains(name))
        return;
    profile->startPage = name;
    found->second.insert_or_assign("start_page", name.toStdString());
    touch();
}

bool Controller::settingsPage() const
{
    const Profile *profile = m_config.profile(m_profile);
    return profile && profile->settingsPage;
}

void Controller::setSettingsPage(bool on)
{
    Profile *profile = m_config.profile(m_profile);
    auto found = m_store->tables.find(m_profile.toStdString());
    if (!profile || found == m_store->tables.end() || profile->settingsPage == on)
        return;
    profile->settingsPage = on;
    if (on) {
        if (profile->pages.isEmpty() || !profile->pages.last().settings) {
            Page settings;
            settings.name = QStringLiteral("settings");
            settings.settings = true;
            settings.keys = QList<KeySpec>(Keys);
            profile->pages.append(settings);
        }
        found->second.erase("settings_page");
    } else if (!profile->pages.isEmpty() && profile->pages.last().settings) {
        profile->pages.removeLast();
        found->second.insert_or_assign("settings_page", false);
    }
    touch();
}

bool Controller::writeConfig(const QString &key, const QString &literal)
{
    if (assignTopLevel(m_config.path, key, literal))
        return true;
    m_status = i18n("Could not save config.toml");
    Q_EMIT changed();
    return false;
}

void Controller::setBrightness(int value)
{
    value = std::clamp(value, 10, 100);
    if (value == m_config.brightness)
        return;
    if (!writeConfig(QStringLiteral("brightness"), QString::number(value)))
        return;
    m_config.brightness = value;
    m_status = i18n("Brightness saved. The deck picks it up within a second.");
    Q_EMIT changed();
}

void Controller::setVolumeStep(int value)
{
    value = std::clamp(value, 1, 10);
    if (value == m_config.volumeStep)
        return;
    if (!writeConfig(QStringLiteral("volume_step"), QString::number(value)))
        return;
    m_config.volumeStep = value;
    m_status = i18n("Volume step saved");
    Q_EMIT changed();
}

void Controller::setClockFormat(const QString &format)
{
    const QString value = format.isEmpty() ? QStringLiteral("%H:%M") : format;
    if (value == m_config.clockFormat)
        return;
    if (!writeConfig(QStringLiteral("clock_format"), tomlString(value)))
        return;
    m_config.clockFormat = value;
    m_status = i18n("Clock format saved");
    ++m_previewRevision;
    Q_EMIT changed();
    Q_EMIT previewChanged();
}

void Controller::setStartProfile(const QString &name)
{
    if (name == m_config.startProfile || !m_config.profile(name))
        return;
    if (!writeConfig(QStringLiteral("start_profile"), tomlString(name)))
        return;
    m_config.startProfile = name;
    m_status = i18n("Start profile saved");
    Q_EMIT changed();
}

void Controller::refreshPreview()
{
    m_locks = keyboardStatus();
    fillMedia(m_locks, m_config.mediaPlayers);
    readVolume(m_locks.volume, m_locks.muted);
    const QStringList found = listMediaPlayers();
    if (found != m_discoveredPlayers)
        m_discoveredPlayers = found;
    publishPlayerChoices();
}

void Controller::publishPlayerChoices()
{
    QStringList names = m_discoveredPlayers;
    for (const QString &saved : m_config.mediaPlayers) {
        if (!names.contains(saved))
            names.append(saved);
    }
    names.sort(Qt::CaseInsensitive);
    if (names == m_playerChoices)
        return;
    m_playerChoices = names;
    Q_EMIT playersChanged();
}

bool Controller::writeMediaPlayers(const QStringList &players)
{
    if (players.isEmpty()) {
        if (removeTopLevel(m_config.path, QStringLiteral("media_players")))
            return true;
        m_status = i18n("Could not save config.toml");
        Q_EMIT changed();
        return false;
    }
    QStringList quoted;
    for (const QString &name : players)
        quoted.append(tomlString(name));
    return writeConfig(QStringLiteral("media_players"), QStringLiteral("[") + quoted.join(QStringLiteral(", ")) + QStringLiteral("]"));
}

bool Controller::setMediaPlayerEnabled(const QString &raw, bool on)
{
    if (raw.trimmed().isEmpty())
        return false;
    const QString name = normalizeMediaPlayer(raw);
    if (name.isEmpty()) {
        m_status = i18n("Player names cannot contain a comma or a line break");
        Q_EMIT changed();
        return false;
    }
    QStringList next = m_config.mediaPlayers;
    const int index = next.indexOf(name);
    if (on) {
        if (index >= 0)
            return true;
        next.append(name);
    } else {
        if (index < 0)
            return true;
        next.removeAt(index);
    }
    if (!writeMediaPlayers(next))
        return false;
    m_config.mediaPlayers = next;
    m_status = next.isEmpty() ? i18n("Now playing follows whichever player is active")
                              : i18n("Now playing locked to %1", next.join(QStringLiteral(", ")));
    refreshPreview();
    ++m_previewRevision;
    Q_EMIT changed();
    Q_EMIT previewChanged();
    return true;
}

void Controller::bumpPreview()
{
    refreshPreview();
    ++m_previewRevision;
    Q_EMIT previewChanged();
}

QString Controller::colourHex(const QString &name) const
{
    const Profile *profile = m_config.profile(m_profile);
    if (!profile)
        return QStringLiteral("#888888");
    return profile->theme.colour(name).name(QColor::HexRgb);
}

void Controller::save()
{
    for (const QString &name : m_store->dirty) {
        const Profile *profile = m_config.profile(name);
        auto found = m_store->tables.find(name.toStdString());
        if (!profile || found == m_store->tables.end())
            continue;
        const QString text = formatProfile(*profile, found->second);
        QSaveFile file(profile->path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            m_status = i18n("Could not save %1", profile->path);
            Q_EMIT changed();
            return;
        }
        const QByteArray bytes = text.toUtf8();
        file.write(bytes);
        if (!file.commit()) {
            m_status = i18n("Could not save %1", profile->path);
            Q_EMIT changed();
            return;
        }
    }
    m_store->dirty.clear();
    m_dirty = false;
    m_seenStamp = configStamp();
    if (!m_holdStatus && m_config.error.isEmpty())
        m_status = i18n("Profiles from %1", m_config.dir);
    m_holdStatus = false;
    Q_EMIT changed();
}

bool Controller::statsVisible() const
{
    for (const KeySpec &key : visibleKeys()) {
        if (!key.stat.isEmpty())
            return true;
    }
    return false;
}

void Controller::watchConfig()
{
    pollService();
    if (m_dirty || m_saveTimer.isActive())
        return;
    if (configStamp() != m_seenStamp) {
        reload();
        return;
    }
    // GTK's watch() redraws the deck every 2s while a stat key is on screen.
    // A separate counter keeps that off changed(), so the form is not reset.
    if (statsVisible()) {
        ++m_statRevision;
        Q_EMIT statTicked();
    }
}

void Controller::followDeck()
{
    const QByteArray runtime = qgetenv("XDG_RUNTIME_DIR");
    const QString path = (runtime.isEmpty() ? QStringLiteral("/tmp") : QString::fromUtf8(runtime)) + QStringLiteral("/kdeck-control.state");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray bytes = file.readAll();
    if (bytes == m_followed)
        return;
    m_followed = bytes;
    if (m_dirty || m_saveTimer.isActive())
        return;
    const QJsonObject state = QJsonDocument::fromJson(bytes).object();
    const QString profileName = state.value(QStringLiteral("profile")).toString();
    if (profileName.isEmpty() || !m_config.profile(profileName))
        return;
    const QString pageName = state.value(QStringLiteral("page")).toString();
    QList<int> folder;
    for (const QJsonValue &value : state.value(QStringLiteral("folder")).toArray())
        folder.append(value.toInt());

    const bool profileChanged = profileName != m_profile;
    if (profileChanged) {
        m_profile = profileName;
        m_key = 0;
        m_page = 0;
        m_folderPath.clear();
    }
    const Profile *profile = m_config.profile(m_profile);
    if (!profile)
        return;
    int pageIndex = -1;
    for (int i = 0; i < profile->pages.size(); ++i) {
        if (profile->pages.at(i).name == pageName) {
            pageIndex = i;
            break;
        }
    }
    if (pageIndex < 0)
        pageIndex = profileChanged ? 0 : m_page;
    bool moved = profileChanged;
    if (m_page != pageIndex) {
        m_page = pageIndex;
        m_key = 0;
        m_folderPath.clear();
        moved = true;
    }
    if (m_page >= 0 && m_page < profile->pages.size() && profile->pages.at(m_page).settings)
        folder.clear();
    else if (m_page >= 0 && m_page < profile->pages.size())
        keysAt(profile->pages.at(m_page).keys, folder, &folder);
    if (folder != m_folderPath) {
        m_folderPath = folder;
        moved = true;
    }
    if (!moved)
        return;
    ++m_revision;
    ++m_previewRevision;
    m_status = i18n("Following the deck: %1", m_profile);
    Q_EMIT changed();
    Q_EMIT previewChanged();
}

namespace {

QString addonPython()
{
    const QString installed = QString::fromUtf8(KDECK_ADDON_SCRIPT);
    if (QFileInfo::exists(installed))
        return installed;
#ifdef KDECK_ADDON_SRC
    const QString source = QString::fromUtf8(KDECK_ADDON_SRC);
    if (QFileInfo::exists(source))
        return source;
#endif
    return {};
}

bool runKdeckImport(const QString &body, const QStringList &args, QString *output, QString *errorText, int timeoutMs)
{
    const QString scriptPath = addonPython();
    if (scriptPath.isEmpty()) {
        if (errorText)
            *errorText = QStringLiteral("kdeck_import.py was not found");
        return false;
    }
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONPATH"), QFileInfo(scriptPath).absolutePath());
    process.setProcessEnvironment(env);
    process.start(QStringLiteral("python3"), QStringList{QStringLiteral("-c"), body} + args);
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        if (errorText)
            *errorText = QStringLiteral("import timed out");
        return false;
    }
    if (output)
        *output = QString::fromUtf8(process.readAllStandardOutput());
    if (process.exitCode() != 0) {
        const QString err = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (errorText)
            *errorText = err.isEmpty() ? QString::fromUtf8(process.readAllStandardOutput()).trimmed() : err;
        return false;
    }
    return true;
}

} // namespace

bool Controller::runAddonTool(const QStringList &args, QString *output, QString *errorText)
{
    QString program;
    QStringList full = args;
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("kdeck-addon"));
    if (!onPath.isEmpty()) {
        program = onPath;
    } else {
        program = QStandardPaths::findExecutable(QStringLiteral("python3"));
        const QString script = addonPython();
        if (program.isEmpty() || script.isEmpty()) {
            if (errorText)
                *errorText = i18n("kdeck-addon is not installed");
            return false;
        }
        full.prepend(script);
    }
    QProcess process;
    process.start(program, full);
    if (!process.waitForFinished(20000)) {
        process.kill();
        if (errorText)
            *errorText = i18n("kdeck-addon timed out");
        return false;
    }
    if (output)
        *output = QString::fromUtf8(process.readAllStandardOutput());
    if (process.exitCode() != 0) {
        const QString err = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (errorText)
            *errorText = err.isEmpty() ? QString::fromUtf8(process.readAllStandardOutput()).trimmed() : err;
        return false;
    }
    return true;
}

void Controller::refreshAddons()
{
    QString output;
    QString errorText;
    if (!runAddonTool({QStringLiteral("list"), QStringLiteral("--json")}, &output, &errorText)) {
        m_addons.clear();
        m_addonStatus = errorText;
        Q_EMIT addonsChanged();
        return;
    }
    const QJsonArray list = QJsonDocument::fromJson(output.toUtf8()).array();
    QVariantList addons;
    for (const QJsonValue &value : list) {
        const QJsonObject object = value.toObject();
        QVariantMap addon;
        addon.insert(QStringLiteral("name"), object.value(QStringLiteral("name")).toString());
        addon.insert(QStringLiteral("title"), object.value(QStringLiteral("title")).toString());
        addon.insert(QStringLiteral("version"), object.value(QStringLiteral("version")).toString());
        addon.insert(QStringLiteral("description"), object.value(QStringLiteral("description")).toString());
        addon.insert(QStringLiteral("installed"), object.value(QStringLiteral("installed")).toString());
        addon.insert(QStringLiteral("upgrade"), object.value(QStringLiteral("upgrade")).toBool());
        addon.insert(QStringLiteral("compatible"), object.value(QStringLiteral("compatible")).toBool());
        QStringList commands;
        for (const QJsonValue &command : object.value(QStringLiteral("commands")).toArray())
            commands.append(command.toString());
        addon.insert(QStringLiteral("commands"), commands);
        QStringList profiles;
        for (const QJsonValue &profileName : object.value(QStringLiteral("profiles")).toArray())
            profiles.append(profileName.toString());
        addon.insert(QStringLiteral("profiles"), profiles);
        addon.insert(QStringLiteral("requires"), object.value(QStringLiteral("requires")).toString());
        addons.append(addon);
    }
    m_addons = addons;
    m_addonStatus = addons.isEmpty() ? i18n("No add-ons yet. Packages from the previous program are not compatible yet.") : QString();
    Q_EMIT addonsChanged();
}

void Controller::installAddon(const QString &name)
{
    QString errorText;
    const bool ok = runAddonTool({QStringLiteral("install"), name}, nullptr, &errorText);
    refreshAddons();
    m_addonStatus = ok ? i18n("Installed %1", name) : errorText;
    if (ok && !m_dirty && !m_saveTimer.isActive())
        reload();
    Q_EMIT addonsChanged();
}

void Controller::removeAddon(const QString &name)
{
    QString errorText;
    const bool ok = runAddonTool({QStringLiteral("remove"), name}, nullptr, &errorText);
    refreshAddons();
    m_addonStatus = ok ? i18n("Removed %1", name) : errorText;
    if (ok && !m_dirty && !m_saveTimer.isActive())
        reload();
    Q_EMIT addonsChanged();
}

void Controller::installAddonFile(const QUrl &url)
{
    const QString path = url.toLocalFile();
    if (path.isEmpty())
        return;
    QString errorText;
    const bool ok = runAddonTool({QStringLiteral("install"), path}, nullptr, &errorText);
    refreshAddons();
    m_addonStatus = ok ? i18n("Installed %1", QFileInfo(path).fileName()) : errorText;
    if (ok && !m_dirty && !m_saveTimer.isActive())
        reload();
    Q_EMIT addonsChanged();
}

void Controller::setAddonDir(const QString &path)
{
    QString local = path;
    const QUrl url(path);
    if (url.isLocalFile())
        local = url.toLocalFile();
    if (local.startsWith(QLatin1Char('~')))
        local = QDir::homePath() + local.mid(1);
    local = QFileInfo(local).absoluteFilePath();
    bool found = false;
    const QDir dir(local);
    if (dir.exists()) {
        const auto entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &info : entries) {
            if (QFileInfo::exists(info.filePath() + QStringLiteral("/addon.toml"))) {
                found = true;
                break;
            }
        }
    }
    if (!found) {
        m_addonStatus = i18n("No add-ons in that folder (each is a subfolder with an addon.toml)");
        Q_EMIT addonsChanged();
        return;
    }
    if (!assignTableKey(m_config.path, QStringLiteral("addons"), QStringLiteral("dir"), tomlString(local))) {
        m_addonStatus = i18n("Could not save config.toml");
        Q_EMIT addonsChanged();
        return;
    }
    m_config.addonDir = local;
    m_seenStamp = configStamp();
    refreshAddons();
}

namespace {

QString profileSlug(QString name)
{
    name = name.trimmed().toLower();
    name.replace(QLatin1Char(' '), QLatin1Char('-'));
    return name;
}

bool profileNameOk(const QString &name)
{
    return !name.isEmpty() && name != QLatin1String(".") && name != QLatin1String("..")
        && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\'))
        && QFileInfo(name).fileName() == name;
}

int realPageCount(const Profile &profile)
{
    if (!profile.pages.isEmpty() && profile.pages.last().settings)
        return profile.pages.size() - 1;
    return profile.pages.size();
}

void renamePageRefs(QList<KeySpec> &keys, const QString &oldName, const QString &newName)
{
    for (KeySpec &key : keys) {
        if (key.page == oldName)
            key.page = newName;
        renamePageRefs(key.folder, oldName, newName);
    }
}

bool retargetProfileKeys(QList<KeySpec> &keys, const QString &oldName, const QString &newName)
{
    bool touched = false;
    for (KeySpec &key : keys) {
        if (key.profile == oldName) {
            key.profile = newName;
            touched = true;
        }
        if (retargetProfileKeys(key.folder, oldName, newName))
            touched = true;
    }
    return touched;
}

QString rulesLiteral(const toml::array &rules, const QString &oldName, const QString &newName, bool *changed)
{
    QStringList lines;
    *changed = false;
    for (auto &&node : rules) {
        const toml::table *table = node.as_table();
        if (!table)
            continue;
        toml::table copy = *table;
        QString profileName;
        if (auto value = copy["profile"].value<std::string>())
            profileName = QString::fromStdString(*value);
        if (profileName == oldName) {
            *changed = true;
            if (newName.isEmpty())
                continue;
            copy.insert_or_assign("profile", newName.toStdString());
        }
        QStringList fields;
        for (auto it = copy.begin(); it != copy.end(); ++it) {
            std::ostringstream stream;
            stream << copy[it->first];
            fields.append(QString::fromStdString(std::string(it->first.str())) + QStringLiteral(" = ")
                          + QString::fromStdString(stream.str()));
        }
        lines.append(QStringLiteral("  { ") + fields.join(QStringLiteral(", ")) + QStringLiteral(" },"));
    }
    if (lines.isEmpty())
        return QStringLiteral("[]");
    return QStringLiteral("[\n") + lines.join(QLatin1Char('\n')) + QStringLiteral("\n]");
}

bool retargetAutoSwitch(const QString &path, const QString &oldName, const QString &newName)
{
    toml::table root;
    try {
        root = toml::parse_file(path.toStdString());
    } catch (const toml::parse_error &) {
        return true;
    }
    const toml::table *section = root["auto_switch"].as_table();
    if (!section)
        return true;
    if (auto value = (*section)["fallback"].value<std::string>()) {
        if (QString::fromStdString(*value) == oldName) {
            if (newName.isEmpty()) {
                if (!removeTableKey(path, QStringLiteral("auto_switch"), QStringLiteral("fallback")))
                    return false;
            } else if (!replaceTableValue(path, QStringLiteral("auto_switch"), QStringLiteral("fallback"), tomlString(newName))) {
                return false;
            }
        }
    }
    const toml::array *rules = (*section)["rules"].as_array();
    if (!rules)
        return true;
    bool changed = false;
    const QString literal = rulesLiteral(*rules, oldName, newName, &changed);
    if (!changed)
        return true;
    return replaceTableValue(path, QStringLiteral("auto_switch"), QStringLiteral("rules"), literal);
}

bool writeNewFile(const QString &path, const QString &text)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(text.toUtf8());
    return file.commit();
}

} // namespace

void Controller::clearAddonDir()
{
    if (m_config.addonDir.isEmpty())
        return;
    if (!removeTableKey(m_config.path, QStringLiteral("addons"), QStringLiteral("dir"))) {
        m_addonStatus = i18n("Could not save config.toml");
        Q_EMIT addonsChanged();
        return;
    }
    m_config.addonDir.clear();
    m_seenStamp = configStamp();
    refreshAddons();
}

QString Controller::fail(const QString &text)
{
    m_status = text;
    m_holdStatus = true;
    Q_EMIT changed();
    return text;
}

void Controller::flushEdits()
{
    if (m_saveTimer.isActive())
        m_saveTimer.stop();
    if (m_dirty)
        save();
}

void Controller::replaceProfileRefs(const QString &oldName, const QString &newName)
{
    if (m_config.startProfile == oldName) {
        if (newName.isEmpty()) {
            if (!removeTopLevel(m_config.path, QStringLiteral("start_profile")))
                m_status = i18n("Could not save config.toml");
            else
                m_config.startProfile.clear();
        } else if (writeConfig(QStringLiteral("start_profile"), tomlString(newName))) {
            m_config.startProfile = newName;
        }
    }
    if (!retargetAutoSwitch(m_config.path, oldName, newName))
        m_status = i18n("Could not save config.toml");

    for (Profile &profile : m_config.profiles) {
        if (profile.name == oldName)
            continue;
        bool touched = false;
        for (Page &page : profile.pages) {
            if (page.settings)
                continue;
            if (retargetProfileKeys(page.keys, oldName, newName))
                touched = true;
        }
        if (!touched)
            continue;
        if (!m_store->dirty.contains(profile.name))
            m_store->dirty.append(profile.name);
        m_dirty = true;
    }
    if (m_dirty)
        save();
}

QString Controller::addPage(const QString &requested)
{
    const QString name = requested.trimmed();
    if (name.isEmpty())
        return {};
    Profile *profile = m_config.profile(m_profile);
    if (!profile)
        return {};
    if (name == QLatin1String("settings"))
        return fail(i18n("There is already a page called “%1”", name));
    const int real = realPageCount(*profile);
    for (int i = 0; i < real; ++i) {
        if (profile->pages.at(i).name == name)
            return fail(i18n("There is already a page called “%1”", name));
    }
    Page page;
    page.name = name;
    page.keys = QList<KeySpec>(Keys);
    profile->pages.insert(real, page);
    m_page = real;
    m_key = 0;
    m_folderPath.clear();
    touch();
    return {};
}

QString Controller::renamePage(const QString &requested)
{
    const QString name = requested.trimmed();
    if (name.isEmpty())
        return {};
    Profile *profile = m_config.profile(m_profile);
    if (!profile || m_page < 0 || m_page >= profile->pages.size())
        return {};
    if (profile->pages.at(m_page).settings)
        return fail(i18n("The settings page is generated and can't be renamed"));
    const int real = realPageCount(*profile);
    if (name == QLatin1String("settings"))
        return fail(i18n("There is already a page called “%1”", name));
    for (int i = 0; i < real; ++i) {
        if (profile->pages.at(i).name == name)
            return fail(i18n("There is already a page called “%1”", name));
    }
    const QString old = profile->pages.at(m_page).name;
    for (int i = 0; i < real; ++i)
        renamePageRefs(profile->pages[i].keys, old, name);
    if (profile->startPage == old)
        profile->startPage = name;
    profile->pages[m_page].name = name;
    touch();
    return {};
}

QString Controller::deletePage()
{
    Profile *profile = m_config.profile(m_profile);
    if (!profile || m_page < 0 || m_page >= profile->pages.size())
        return {};
    if (profile->pages.at(m_page).settings)
        return fail(i18n("Turn the settings page off under Look instead"));
    if (realPageCount(*profile) <= 1)
        return fail(i18n("A profile needs at least one page"));
    const QString name = profile->pages.at(m_page).name;
    profile->pages.removeAt(m_page);
    if (profile->startPage == name)
        profile->startPage.clear();
    m_page = std::max(0, m_page - 1);
    m_key = 0;
    m_folderPath.clear();
    touch();
    return {};
}

void Controller::movePage(int direction)
{
    Profile *profile = m_config.profile(m_profile);
    if (!profile)
        return;
    const int real = realPageCount(*profile);
    const int next = m_page + direction;
    if (m_page < 0 || m_page >= real || next < 0 || next >= real)
        return;
    profile->pages.swapItemsAt(m_page, next);
    m_page = next;
    touch();
}

void Controller::movePageTo(int from, int to)
{
    Profile *profile = m_config.profile(m_profile);
    if (!profile)
        return;
    const int real = realPageCount(*profile);
    if (from < 0 || to < 0 || from >= real || to >= real || from == to)
        return;
    if (profile->pages.at(from).settings || profile->pages.at(to).settings)
        return;
    profile->pages.insert(to, profile->pages.takeAt(from));
    if (m_page == from)
        m_page = to;
    else if (from < m_page && to >= m_page)
        --m_page;
    else if (from > m_page && to <= m_page)
        ++m_page;
    touch();
}

QString Controller::makeStartPage()
{
    const Profile *profile = m_config.profile(m_profile);
    if (!profile || m_page < 0 || m_page >= profile->pages.size() || profile->pages.at(m_page).settings)
        return {};
    const QString name = profile->pages.at(m_page).name;
    setStartPage(name);
    m_holdStatus = true;
    m_status = i18n("“%1” is now the start page", name);
    Q_EMIT changed();
    return {};
}

QString Controller::createProfile(const QString &requested, const QString &source)
{
    const QString name = profileSlug(requested);
    if (name.isEmpty())
        return {};
    if (!profileNameOk(name))
        return fail(i18n("That name can't be used for a profile"));
    if (m_config.profile(name))
        return fail(i18n("There is already a profile called “%1”", name));
    if (!source.isEmpty() && !m_config.profile(source))
        return fail(i18n("There is already a profile called “%1”", source));
    flushEdits();
    const QString path = QDir(m_config.dir).filePath(QStringLiteral("profiles/%1.toml").arg(name));
    if (QFileInfo::exists(path))
        return fail(i18n("There is already a profile called “%1”", name));
    if (source.isEmpty()) {
        const QString text = QStringLiteral(
            "# kdeck-control profile. Edit here or in the configurator (kdeck-control-config).\n"
            "\n"
            "theme = \"amber\"\n"
            "\n"
            "[[page]]\n"
            "name = \"main\"\n"
            "keys = [\n"
            "]\n");
        if (!writeNewFile(path, text))
            return fail(i18n("Could not save %1", path));
    } else {
        const Profile *from = m_config.profile(source);
        if (!from || !QFile::copy(from->path, path))
            return fail(i18n("Could not save %1", path));
    }
    m_profile = name;
    m_page = 0;
    m_key = 0;
    m_folderPath.clear();
    reload();
    return {};
}

QString Controller::newProfile(const QString &name)
{
    return createProfile(name, {});
}

QString Controller::duplicateProfile(const QString &name)
{
    return createProfile(name, m_profile);
}

QString Controller::renameProfile(const QString &requested)
{
    const QString name = profileSlug(requested);
    if (name.isEmpty())
        return {};
    if (!profileNameOk(name))
        return fail(i18n("That name can't be used for a profile"));
    if (m_config.profile(name))
        return fail(i18n("There is already a profile called “%1”", name));
    const Profile *profile = m_config.profile(m_profile);
    if (!profile)
        return {};
    flushEdits();
    const QString old = m_profile;
    const QString dest = QDir(m_config.dir).filePath(QStringLiteral("profiles/%1.toml").arg(name));
    if (QFileInfo::exists(dest) || !QFile::rename(profile->path, dest))
        return fail(i18n("Could not save %1", dest));
    replaceProfileRefs(old, name);
    m_profile = name;
    reload();
    return {};
}

QString Controller::deleteProfile()
{
    if (m_config.profiles.size() <= 1)
        return fail(i18n("You need at least one profile"));
    const Profile *profile = m_config.profile(m_profile);
    if (!profile)
        return {};
    const QString name = m_profile;
    flushEdits();
    if (!QFile::remove(profile->path))
        return fail(i18n("Could not save %1", profile->path));
    replaceProfileRefs(name, {});
    m_profile.clear();
    m_page = 0;
    m_key = 0;
    m_folderPath.clear();
    reload();
    return {};
}

QString Controller::makeStartProfile()
{
    if (!m_config.profile(m_profile))
        return {};
    const QString name = m_profile;
    setStartProfile(name);
    if (m_config.startProfile != name)
        return m_status;
    m_holdStatus = true;
    m_status = i18n("The deck starts with “%1”", name);
    Q_EMIT changed();
    return {};
}

QString Controller::peekStreamDeck(const QUrl &url)
{
    const QString path = url.toLocalFile();
    if (path.isEmpty())
        return fail(i18n("Could not read that profile"));
    QString output;
    QString errorText;
    const QString body = QStringLiteral(
        "import sys\n"
        "import kdeck_import\n"
        "try:\n"
        "    print(kdeck_import.peek_name(sys.argv[1]))\n"
        "except (kdeck_import.ProfileImportError, OSError) as e:\n"
        "    print(e, file=sys.stderr)\n"
        "    raise SystemExit(1)\n");
    if (!runKdeckImport(body, {path}, &output, &errorText, 30000)) {
        fail(errorText.isEmpty() ? i18n("Could not read that profile") : errorText);
        return {};
    }
    const QString name = output.trimmed();
    if (name.isEmpty()) {
        fail(i18n("Could not read that profile"));
        return {};
    }
    return name;
}

QString Controller::importStreamDeck(const QUrl &url, const QString &requested)
{
    const QString path = url.toLocalFile();
    if (path.isEmpty())
        return fail(i18n("Could not import that profile"));
    flushEdits();
    QString output;
    QString errorText;
    const QString body = QStringLiteral(
        "import sys\n"
        "import kdeck_import\n"
        "try:\n"
        "    result = kdeck_import.import_profile(sys.argv[1], sys.argv[2], name=(sys.argv[3] or None))\n"
        "except (kdeck_import.ProfileImportError, OSError) as e:\n"
        "    print(e, file=sys.stderr)\n"
        "    raise SystemExit(1)\n"
        "print(result.name)\n"
        "print(result.kept)\n");
    if (!runKdeckImport(body, {path, m_config.dir, requested}, &output, &errorText, 60000))
        return fail(errorText.isEmpty() ? i18n("Could not import that profile") : errorText);
    const QStringList lines = output.trimmed().split(QLatin1Char('\n'));
    if (lines.isEmpty() || lines.at(0).trimmed().isEmpty())
        return fail(i18n("Could not import that profile"));
    const QString name = lines.at(0).trimmed();
    bool keptOk = false;
    const int kept = lines.size() > 1 ? lines.at(1).trimmed().toInt(&keptOk) : 0;
    m_profile = name;
    m_page = 0;
    m_key = 0;
    m_folderPath.clear();
    reload();
    if (!m_config.profile(name))
        return fail(i18n("Could not import that profile"));
    m_holdStatus = true;
    if (keptOk && kept > 0)
        m_status = i18n("Imported “%1”. %2 plugin keys kept their title and picture", name, kept);
    else
        m_status = i18n("Imported “%1”", name);
    Q_EMIT changed();
    return {};
}

void Controller::runAddon(const QString &name, const QString &command)
{
    QString output;
    QString errorText;
    if (!runAddonTool({QStringLiteral("run"), name, command}, &output, &errorText))
        m_addonStatus = errorText;
    else
        m_addonStatus = output.trimmed().isEmpty() ? i18n("Ran %1", command) : output.trimmed();
    Q_EMIT addonsChanged();
}

QImage Controller::keyImage(int index) const
{
    const Profile *profile = m_config.profile(m_profile);
    const QList<KeySpec> keys = visibleKeys();
    if (!profile || index < 0 || index >= keys.size())
        return {};
    const QString active = profile->followTheme ? profile->theme.name : profile->themeName;
    return renderDeckKey(keys.at(index), profile->theme, m_config.themes, active, false, unixSeconds());
}

void Controller::syncKeyAnimation()
{
    bool animated = false;
    const double now = unixSeconds();
    const QList<KeySpec> keys = visibleKeys();
    for (const KeySpec &key : keys) {
        if (!key.stat.isEmpty() || key.image.isEmpty())
            continue;
        if (imageFrameIndex(configPath(m_config.dir, key.image), now) >= 0) {
            animated = true;
            break;
        }
    }
    if (animated)
        m_animTimer.start();
    else
        m_animTimer.stop();
}

void Controller::tickAnimations()
{
    const double now = unixSeconds();
    const QList<KeySpec> keys = visibleKeys();
    bool moved = false;
    QHash<int, int> next;
    for (int i = 0; i < keys.size(); ++i) {
        const KeySpec &key = keys.at(i);
        if (!key.stat.isEmpty() || key.image.isEmpty())
            continue;
        const int frame = imageFrameIndex(configPath(m_config.dir, key.image), now);
        if (frame < 0)
            continue;
        next.insert(i, frame);
        if (m_animFrame.value(i, -1) != frame)
            moved = true;
    }
    m_animFrame = next;
    if (moved) {
        ++m_animRevision;
        Q_EMIT animTicked();
    }
    if (next.isEmpty())
        m_animTimer.stop();
}

QImage Controller::lcdImage() const
{
    const Profile *profile = m_config.profile(m_profile);
    LcdState state = m_locks;
    state.profile = m_profile;
    if (profile && m_page >= 0 && m_page < profile->pages.size()) {
        const Page &page = profile->pages.at(m_page);
        state.page = placeLabel(page.name, page.keys, m_folderPath);
        state.screen = profile->screen;
    }
    state.pageIndex = m_page;
    state.pageCount = profile ? std::max(1, static_cast<int>(profile->pages.size())) : 1;
    state.clock = formatNow(m_config.clockFormat);
    state.date = formatNow(QStringLiteral("%A %d %B")).toUpper();
    return renderLcd(state, profile ? profile->theme : Theme{});
}

QImage Controller::themeImage(const QString &name) const
{
    if (!m_config.themes.contains(name))
        return {};
    const Profile *profile = m_config.profile(m_profile);
    return renderThemeKey(name, m_config.themes.value(name), profile && profile->themeName == name);
}

KeyImageProvider::KeyImageProvider(Controller *controller)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_controller(controller)
{
}

QImage KeyImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QString kind = id.section(QLatin1Char('/'), 0, 0);
    QImage image;
    if (kind == QLatin1String("lcd"))
        image = m_controller->lcdImage();
    else if (kind == QLatin1String("theme"))
        image = m_controller->themeImage(id.section(QLatin1Char('/'), 1, 1));
    else
        image = m_controller->keyImage(kind.toInt());
    if (image.isNull()) {
        image = QImage(KeyPx, KeyPx, QImage::Format_RGB32);
        image.fill(Qt::black);
    }
    if (size)
        *size = image.size();
    if (requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0)
        return image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}

} // namespace kdeck
