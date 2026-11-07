#include "Controller.hpp"

#include "Render.hpp"

#include <toml++/toml.hpp>

#include <KLocalizedString>

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <map>
#include <sstream>

namespace galleon {
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
    if (!key.sequence.isEmpty() && key.stepMs != 40)
        parts.append(QStringLiteral("step_ms = ") + QString::number(key.stepMs));
    if (!key.repeat.isEmpty() && key.repeatMs != 100)
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
        return QStringLiteral("# galleon-deck profile.\n");
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
    addString("theme", profile.themeName);
    addString("start_page", profile.startPage);
    addString("logo", profile.logo);
    addString("screen", profile.screen);
    if (!profile.settingsPage)
        out += QStringLiteral("settings_page = false\n");
    if (table.contains("follow_theme")) {
        std::ostringstream stream;
        stream << table["follow_theme"];
        out += QStringLiteral("follow_theme = ") + QString::fromStdString(stream.str()) + QLatin1Char('\n');
    }
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

QString kindId(const KeySpec &key)
{
    const QString kind = key.actionKind();
    if (kind == QLatin1String("back") || kind == QLatin1String("folder") || kind == QLatin1String("stat"))
        return QStringLiteral("none");
    return kind;
}

const char *valueField(const QString &kind)
{
    if (kind == QLatin1String("key"))
        return "key";
    if (kind == QLatin1String("shortcut"))
        return "keys";
    if (kind == QLatin1String("command"))
        return "exec";
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

const char *actionFields[] = {"key", "keys", "exec", "confirm", "media", "page", "profile", "sequence", "hold", "step_ms", "repeat", "repeat_ms"};

void assignString(toml::table &table, const char *name, const QString &value)
{
    if (value.isEmpty())
        table.erase(name);
    else
        table.insert_or_assign(name, value.toStdString());
}

} // namespace

struct Controller::Store {
    std::map<std::string, toml::table> tables;
    QStringList dirty;
};

Controller::Controller(QObject *parent)
    : QObject(parent)
    , m_store(std::make_unique<Store>())
{
    reload();
}

Controller::~Controller() = default;

void Controller::reload()
{
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
        int count = 0;
        for (const Page &page : profile->pages)
            count += page.settings ? 0 : 1;
        if (count == 0)
            m_page = 0;
        else
            m_page = std::clamp(m_page, 0, count - 1);
    }
    m_key = std::clamp(m_key, 0, Keys - 1);
    m_status = m_config.error.isEmpty() ? i18n("Profiles from %1", m_config.dir) : m_config.error;
    ++m_revision;
    Q_EMIT changed();
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
    m_profile = name;
    m_page = 0;
    m_key = 0;
    ++m_revision;
    Q_EMIT changed();
}

QStringList Controller::pages() const
{
    QStringList names;
    if (const Profile *profile = m_config.profile(m_profile)) {
        for (const Page &page : profile->pages) {
            if (!page.settings)
                names.append(page.name);
        }
    }
    return names;
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
    ++m_revision;
    Q_EMIT changed();
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

const KeySpec *Controller::currentKey() const
{
    const Profile *profile = m_config.profile(m_profile);
    if (!profile || m_page < 0 || m_page >= profile->pages.size())
        return nullptr;
    const Page &page = profile->pages.at(m_page);
    if (page.settings || m_key < 0 || m_key >= page.keys.size())
        return nullptr;
    return &page.keys.at(m_key);
}

KeySpec *Controller::currentKey()
{
    return const_cast<KeySpec *>(std::as_const(*this).currentKey());
}

void *Controller::rawKey()
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
    while (static_cast<int>(keys->size()) <= m_key)
        keys->push_back(toml::table{});
    if (!(*keys)[static_cast<size_t>(m_key)].is_table())
        keys->replace(keys->cbegin() + m_key, toml::table{});
    return (*keys)[static_cast<size_t>(m_key)].as_table();
}

void Controller::touch()
{
    if (!m_store->dirty.contains(m_profile))
        m_store->dirty.append(m_profile);
    m_dirty = true;
    ++m_revision;
    m_status = i18n("Unsaved changes");
    Q_EMIT changed();
}

QString Controller::label() const
{
    const KeySpec *key = currentKey();
    return key ? key->label : QString();
}

void Controller::setLabel(const QString &label)
{
    KeySpec *key = currentKey();
    auto *table = static_cast<toml::table *>(rawKey());
    if (!key || !table || key->label == label)
        return;
    key->label = label;
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
    auto *table = static_cast<toml::table *>(rawKey());
    if (!key || !table || key->icon == icon)
        return;
    key->icon = icon;
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
    auto *table = static_cast<toml::table *>(rawKey());
    if (!key || !table || key->image == path)
        return;
    key->image = path;
    assignString(*table, "image", path);
    touch();
}

void Controller::clearImage()
{
    setImageUrl(QUrl());
}

QStringList Controller::actionKindIds() const
{
    return {QStringLiteral("none"), QStringLiteral("key"), QStringLiteral("shortcut"), QStringLiteral("command"),
            QStringLiteral("media"), QStringLiteral("page"), QStringLiteral("profile"), QStringLiteral("sequence"),
            QStringLiteral("repeat")};
}

QStringList Controller::actionKindLabels() const
{
    return {i18n("None"), i18n("Key"), i18n("Shortcut"), i18n("Command"), i18n("Media"), i18n("Page"),
            i18n("Profile"), i18n("Sequence"), i18n("Repeat")};
}

int Controller::actionKindIndex() const
{
    const KeySpec *key = currentKey();
    const int index = actionKindIds().indexOf(key ? kindId(*key) : QStringLiteral("none"));
    return index < 0 ? 0 : index;
}

void Controller::setActionKindIndex(int index)
{
    KeySpec *key = currentKey();
    auto *table = static_cast<toml::table *>(rawKey());
    const QStringList ids = actionKindIds();
    if (!key || !table || index < 0 || index >= ids.size())
        return;
    if (!key->folder.isEmpty() || key->back || !key->stat.isEmpty())
        return;
    const QString kind = ids.at(index);
    if (kindId(*key) == kind)
        return;
    const QString previous = actionValue();
    for (const char *field : actionFields)
        table->erase(field);
    key->key.clear();
    key->keys.clear();
    key->exec.clear();
    key->media.clear();
    key->page.clear();
    key->profile.clear();
    key->sequence.clear();
    key->repeat.clear();
    key->confirm = false;
    if (const char *field = valueField(kind)) {
        assignString(*table, field, previous);
        if (kind == QLatin1String("key"))
            key->key = previous;
        else if (kind == QLatin1String("shortcut"))
            key->keys = previous;
        else if (kind == QLatin1String("command"))
            key->exec = previous;
        else if (kind == QLatin1String("media"))
            key->media = previous;
        else if (kind == QLatin1String("page"))
            key->page = previous;
        else if (kind == QLatin1String("profile"))
            key->profile = previous;
        else if (kind == QLatin1String("sequence"))
            key->sequence = previous;
        else if (kind == QLatin1String("repeat"))
            key->repeat = previous;
    }
    touch();
}

QString Controller::actionValue() const
{
    const KeySpec *key = currentKey();
    if (!key)
        return {};
    const QString kind = kindId(*key);
    if (kind == QLatin1String("key"))
        return key->key;
    if (kind == QLatin1String("shortcut"))
        return key->keys;
    if (kind == QLatin1String("command"))
        return key->exec;
    if (kind == QLatin1String("media"))
        return key->media;
    if (kind == QLatin1String("page"))
        return key->page;
    if (kind == QLatin1String("profile"))
        return key->profile;
    if (kind == QLatin1String("sequence"))
        return key->sequence;
    if (kind == QLatin1String("repeat"))
        return key->repeat;
    return {};
}

void Controller::setActionValue(const QString &value)
{
    KeySpec *key = currentKey();
    auto *table = static_cast<toml::table *>(rawKey());
    if (!key || !table)
        return;
    const QString kind = kindId(*key);
    const char *field = valueField(kind);
    if (!field || actionValue() == value)
        return;
    assignString(*table, field, value);
    if (kind == QLatin1String("key"))
        key->key = value;
    else if (kind == QLatin1String("shortcut"))
        key->keys = value;
    else if (kind == QLatin1String("command"))
        key->exec = value;
    else if (kind == QLatin1String("media"))
        key->media = value;
    else if (kind == QLatin1String("page"))
        key->page = value;
    else if (kind == QLatin1String("profile"))
        key->profile = value;
    else if (kind == QLatin1String("sequence"))
        key->sequence = value;
    else if (kind == QLatin1String("repeat"))
        key->repeat = value;
    touch();
}

bool Controller::confirm() const
{
    const KeySpec *key = currentKey();
    return key && key->confirm;
}

void Controller::setConfirm(bool confirm)
{
    KeySpec *key = currentKey();
    auto *table = static_cast<toml::table *>(rawKey());
    if (!key || !table || key->confirm == confirm)
        return;
    key->confirm = confirm;
    if (confirm)
        table->insert_or_assign("confirm", true);
    else
        table->erase("confirm");
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
    m_status = i18n("Saved");
    Q_EMIT changed();
}

QImage Controller::keyImage(int index) const
{
    const Profile *profile = m_config.profile(m_profile);
    if (!profile || m_page < 0 || m_page >= profile->pages.size())
        return {};
    const Page &page = profile->pages.at(m_page);
    if (index < 0 || index >= page.keys.size())
        return {};
    return renderKey(page.keys.at(index), profile->theme, false);
}

KeyImageProvider::KeyImageProvider(Controller *controller)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_controller(controller)
{
}

QImage KeyImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const int index = id.section(QLatin1Char('/'), 0, 0).toInt();
    QImage image = m_controller->keyImage(index);
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

} // namespace galleon
