#include "Service.hpp"

#include "Deck.hpp"
#include "Keyboard.hpp"
#include "Render.hpp"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>

#include <algorithm>
#include <cstdlib>

namespace galleon {
namespace {

qint64 configStamp(const QString &dir)
{
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

QString strftimeNow(const QString &format)
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char buffer[128];
    if (strftime(buffer, sizeof buffer, format.toUtf8().constData(), &local) == 0)
        return {};
    return QString::fromUtf8(buffer);
}

void readVolume(int &percent, bool &muted)
{
    percent = -1;
    muted = false;
    auto run = [](const QString &program, const QStringList &args) {
        QProcess process;
        process.start(program, args);
        if (!process.waitForFinished(800))
            return QString();
        return QString::fromUtf8(process.readAllStandardOutput());
    };
    if (!QStandardPaths::findExecutable(QStringLiteral("wpctl")).isEmpty()) {
        const QString out = run(QStringLiteral("wpctl"), {QStringLiteral("get-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@")});
        const auto parts = out.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            bool ok = false;
            const double value = parts.at(1).toDouble(&ok);
            if (ok)
                percent = qRound(value * 100);
            muted = out.contains(QStringLiteral("MUTED"));
        }
        return;
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty()) {
        const QString volume = run(QStringLiteral("pactl"), {QStringLiteral("get-sink-volume"), QStringLiteral("@DEFAULT_SINK@")});
        const QString mute = run(QStringLiteral("pactl"), {QStringLiteral("get-sink-mute"), QStringLiteral("@DEFAULT_SINK@")});
        const QRegularExpression match(QStringLiteral("(\\d+)%"));
        const auto found = match.match(volume);
        if (found.hasMatch())
            percent = found.captured(1).toInt();
        muted = mute.trimmed().endsWith(QStringLiteral("yes"));
    }
}

void applyVolume(int percent)
{
    if (!QStandardPaths::findExecutable(QStringLiteral("wpctl")).isEmpty()) {
        QStringList args{QStringLiteral("set-volume")};
        if (percent > 0)
            args << QStringLiteral("-l") << QStringLiteral("1.0");
        args << QStringLiteral("@DEFAULT_AUDIO_SINK@")
             << QString::number(std::abs(percent)) + QLatin1Char('%') + (percent > 0 ? QLatin1Char('+') : QLatin1Char('-'));
        QProcess::startDetached(QStringLiteral("wpctl"), args);
        return;
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty()) {
        QProcess read;
        read.start(QStringLiteral("pactl"), {QStringLiteral("get-sink-volume"), QStringLiteral("@DEFAULT_SINK@")});
        read.waitForFinished(800);
        const QRegularExpression match(QStringLiteral("(\\d+)%"));
        const auto found = match.match(QString::fromUtf8(read.readAllStandardOutput()));
        const int current = found.hasMatch() ? found.captured(1).toInt() : 0;
        const int target = std::clamp(current + percent, 0, 100);
        QProcess::startDetached(QStringLiteral("pactl"),
                                {QStringLiteral("set-sink-volume"), QStringLiteral("@DEFAULT_SINK@"), QString::number(target) + QLatin1Char('%')});
    }
}

void toggleMute()
{
    if (!QStandardPaths::findExecutable(QStringLiteral("wpctl")).isEmpty())
        QProcess::startDetached(QStringLiteral("wpctl"), {QStringLiteral("set-mute"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), QStringLiteral("toggle")});
    else if (!QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty())
        QProcess::startDetached(QStringLiteral("pactl"), {QStringLiteral("set-sink-mute"), QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("toggle")});
}

QString glyph(char32_t code)
{
    return QString::fromUcs4(&code, 1);
}

} // namespace

Service::Service(QObject *parent)
    : QObject(parent)
    , m_keyboard(std::make_unique<Keyboard>())
{
    m_keyState = QList<int>(Keys, 0);
    if (!m_keyboard->isOpen())
        qWarning() << "uinput keyboard was not created; key actions will not type";

    m_ping.setInterval(500);
    m_tick.setInterval(1000);
    m_retry.setInterval(2000);
    connect(&m_ping, &QTimer::timeout, this, &Service::onPing);
    connect(&m_tick, &QTimer::timeout, this, &Service::onTick);
    connect(&m_retry, &QTimer::timeout, this, &Service::tryConnect);

    Config::ensureExamples();
    reload();
    tryConnect();
    if (!m_deck)
        m_retry.start();
    m_tick.start();
}

Service::~Service()
{
    releaseHeld();
    disconnectDeck();
}

void Service::tryConnect()
{
    if (m_deck)
        return;
    const QString path = Deck::findHidraw();
    if (path.isEmpty())
        return;
    auto deck = std::make_unique<Deck>();
    if (!deck->open(path))
        return;
    if (!deck->ping()) {
        qWarning() << "deck did not accept the keep-alive";
        return;
    }
    m_deck = std::move(deck);
    m_notifier = std::make_unique<QSocketNotifier>(m_deck->fd(), QSocketNotifier::Read, this);
    connect(m_notifier.get(), &QSocketNotifier::activated, this, [this](QSocketDescriptor) { onDeckReadable(); });
    m_keyState = QList<int>(Keys, 0);
    m_dialDown[0] = m_dialDown[1] = false;
    m_lastLcd.clear();
    m_lcdDirty = true;
    m_deck->setBrightness(m_config.brightness);
    readVolume(m_volume, m_muted);
    drawKeys();
    drawLcd();
    m_ping.start();
    m_retry.stop();
    qInfo() << "connected:" << path;
}

void Service::disconnectDeck()
{
    m_ping.stop();
    m_notifier.reset();
    if (m_deck)
        m_deck->close();
    m_deck.reset();
    if (!m_retry.isActive())
        m_retry.start();
}

void Service::onDeckReadable()
{
    if (!m_deck)
        return;
    while (true) {
        const QByteArray report = m_deck->read();
        if (report.isEmpty())
            break;
        handleReport(report);
    }
}

void Service::onPing()
{
    if (m_deck && !m_deck->ping()) {
        qWarning() << "deck keep-alive failed";
        disconnectDeck();
    }
}

void Service::onTick()
{
    reload();
    if (!m_deck)
        return;
    readVolume(m_volume, m_muted);
    drawLcd();
}

void Service::reload()
{
    const qint64 stamp = configStamp(Config::configDir());
    if (stamp == m_configStamp && m_configStamp >= 0)
        return;
    m_configStamp = stamp;
    Config loaded = Config::load();
    if (!loaded.error.isEmpty() && loaded.profiles.isEmpty()) {
        m_error = loaded.error;
        m_lcdDirty = true;
        qWarning().noquote() << "config:" << m_error;
        return;
    }
    releaseHeld();
    m_config = std::move(loaded);
    m_error.clear();
    if (m_profile.isEmpty() || !m_config.profile(m_profile)) {
        m_profile = m_config.profile(m_config.startProfile) ? m_config.startProfile : m_config.profiles.first().name;
        m_page = startPageIndex();
        m_history.clear();
        m_folderPath.clear();
    }
    if (const Profile *profile = currentProfile())
        m_page = std::clamp(m_page, 0, static_cast<int>(profile->pages.size()) - 1);
    if (const Profile *profile = currentProfile()) {
        const QList<KeySpec> pageKeys = profile->pages.at(m_page).settings ? QList<KeySpec>{} : profile->pages.at(m_page).keys;
        QList<int> resolved;
        keysAt(pageKeys, m_folderPath, &resolved);
        m_folderPath = resolved;
    }
    qInfo() << "config loaded";
    if (m_deck) {
        m_deck->setBrightness(m_config.brightness);
        drawKeys();
    }
    m_lcdDirty = true;
}

const Profile *Service::currentProfile() const
{
    return m_config.profile(m_profile);
}

Theme Service::currentTheme() const
{
    if (const Profile *profile = currentProfile())
        return profile->theme;
    return Theme{};
}

int Service::startPageIndex() const
{
    const Profile *profile = currentProfile();
    if (!profile || profile->pages.isEmpty())
        return 0;
    for (int i = 0; i < profile->pages.size(); ++i) {
        if (profile->pages.at(i).name == profile->startPage)
            return i;
    }
    return 0;
}

QList<KeySpec> Service::settingsKeys() const
{
    QList<KeySpec> keys;
    const int reserved = 2 + std::min(static_cast<int>(m_config.profiles.size()), 3);
    const int slots = std::max(0, Keys - reserved);
    for (auto it = m_config.themes.cbegin(); it != m_config.themes.cend() && keys.size() < slots; ++it) {
        if (it.key() == QLatin1String("wallpaper"))
            continue;
        KeySpec key;
        key.setTheme = it.key();
        keys.append(key);
    }
    KeySpec dimmer;
    dimmer.icon = glyph(0xF00DE);
    dimmer.label = QStringLiteral("dimmer");
    dimmer.hasBrightness = true;
    dimmer.brightness = -10;
    keys.append(dimmer);
    KeySpec brighter;
    brighter.icon = glyph(0xF00E0);
    brighter.label = QStringLiteral("brighter");
    brighter.hasBrightness = true;
    brighter.brightness = 10;
    keys.append(brighter);
    for (const Profile &profile : m_config.profiles) {
        if (keys.size() >= Keys)
            break;
        KeySpec key;
        key.icon = profile.name == m_profile ? glyph(0xF0009) : glyph(0xF0004);
        key.label = profile.name;
        key.profile = profile.name;
        if (profile.name == m_profile)
            key.fg = QStringLiteral("accent");
        keys.append(key);
    }
    while (keys.size() < Keys)
        keys.append(KeySpec{});
    return keys;
}

QList<KeySpec> Service::currentKeys() const
{
    const Profile *profile = currentProfile();
    if (!profile || profile->pages.isEmpty())
        return QList<KeySpec>(Keys);
    const Page &page = profile->pages.at(std::clamp(m_page, 0, static_cast<int>(profile->pages.size()) - 1));
    if (page.settings)
        return settingsKeys();
    return keysAt(page.keys, m_folderPath);
}

void Service::drawKey(int index, bool pressed)
{
    if (!m_deck)
        return;
    const QList<KeySpec> keys = currentKeys();
    if (index < 0 || index >= keys.size())
        return;
    const KeySpec &spec = keys.at(index);
    QImage image;
    if (!spec.setTheme.isEmpty()) {
        const Profile *profile = currentProfile();
        image = renderThemeKey(spec.setTheme, m_config.themes.value(spec.setTheme, currentTheme()),
                                profile && spec.setTheme == profile->themeName);
    }
    else
        image = renderKey(spec, currentTheme(), pressed);
    if (!m_deck->keyImage(index, encodeJpeg(image)))
        disconnectDeck();
}

void Service::drawKeys()
{
    for (int i = 0; i < Keys; ++i)
        drawKey(i, false);
}

void Service::drawLcd()
{
    if (!m_deck)
        return;
    const Profile *profile = currentProfile();
    LcdState state = keyboardStatus();
    state.profile = m_profile.isEmpty() ? QStringLiteral("galleon-deck") : m_profile;
    state.page = profile && !profile->pages.isEmpty()
        ? placeLabel(profile->pages.at(m_page).name, profile->pages.at(m_page).keys, m_folderPath)
        : QString();
    state.pageIndex = m_page;
    state.pageCount = profile ? profile->pages.size() : 1;
    state.clock = strftimeNow(m_config.clockFormat);
    state.date = strftimeNow(QStringLiteral("%A %d %B")).toUpper();
    state.volume = m_volume;
    state.muted = m_muted;
    state.error = m_error;
    state.screen = profile ? profile->screen : QString();
    const QByteArray jpeg = encodeJpeg(renderLcd(state, currentTheme()));
    if (!m_lcdDirty && jpeg == m_lastLcd)
        return;
    if (!m_deck->lcdImage(jpeg)) {
        disconnectDeck();
        return;
    }
    m_lastLcd = jpeg;
    m_lcdDirty = false;
}

void Service::releaseHeld()
{
    for (auto it = m_held.cbegin(); it != m_held.cend(); ++it)
        m_keyboard->release(it.value());
    m_held.clear();
    m_downAt.clear();
    m_typing = false;
}

void Service::setPage(int index, bool remember)
{
    const Profile *profile = currentProfile();
    if (!profile || profile->pages.isEmpty())
        return;
    const int count = profile->pages.size();
    index %= count;
    if (index < 0)
        index += count;
    if (index == m_page)
        return;
    releaseHeld();
    m_folderPath.clear();
    if (remember)
        m_history.append(m_page);
    m_page = index;
    drawKeys();
    m_lcdDirty = true;
    drawLcd();
}

void Service::gotoPage(const QString &name)
{
    const Profile *profile = currentProfile();
    if (!profile)
        return;
    if (name == QLatin1String("back")) {
        if (!m_folderPath.isEmpty()) {
            leaveFolder(false);
            return;
        }
        setPage(m_history.isEmpty() ? startPageIndex() : m_history.takeLast(), false);
        return;
    }
    if (name == QLatin1String("next")) {
        setPage(m_page + 1, false);
        return;
    }
    if (name == QLatin1String("previous")) {
        setPage(m_page - 1, false);
        return;
    }
    for (int i = 0; i < profile->pages.size(); ++i) {
        if (profile->pages.at(i).name == name) {
            setPage(i, true);
            return;
        }
    }
    qWarning() << "no page named" << name;
}

void Service::setProfile(const QString &name)
{
    if (!m_config.profile(name) || name == m_profile)
        return;
    releaseHeld();
    m_profile = name;
    m_page = startPageIndex();
    m_folderPath.clear();
    m_history.clear();
    qInfo() << "profile:" << name;
    drawKeys();
    m_lcdDirty = true;
    drawLcd();
}

void Service::enterFolder(int index)
{
    releaseHeld();
    m_folderPath.append(index);
    drawKeys();
    m_lcdDirty = true;
    drawLcd();
}

void Service::leaveFolder(bool toRoot)
{
    releaseHeld();
    if (!m_folderPath.isEmpty()) {
        if (toRoot)
            m_folderPath.clear();
        else
            m_folderPath.removeLast();
        drawKeys();
        m_lcdDirty = true;
        drawLcd();
        return;
    }
    if (toRoot) {
        m_history.clear();
        setPage(startPageIndex(), false);
        return;
    }
    gotoPage(QStringLiteral("back"));
}

void Service::changeVolume(int clicks)
{
    applyVolume(clicks * m_config.volumeStep);
    readVolume(m_volume, m_muted);
    m_lcdDirty = true;
    drawLcd();
}

void Service::setBrightness(int delta)
{
    const int level = std::clamp(m_config.brightness + delta, 10, 100);
    if (!assignTopLevel(m_config.path, QStringLiteral("brightness"), QString::number(level)))
        return;
    m_configStamp = -1;
    reload();
}

void Service::setTheme(const QString &name)
{
    const Profile *profile = currentProfile();
    if (!profile || !m_config.themes.contains(name))
        return;
    assignTopLevel(profile->path, QStringLiteral("theme"), QStringLiteral("\"%1\"").arg(name));
    removeTopLevel(profile->path, QStringLiteral("follow_theme"));
    m_configStamp = -1;
    reload();
}

void Service::keyDown(int index)
{
    const QList<KeySpec> keys = currentKeys();
    if (index < 0 || index >= keys.size())
        return;
    const KeySpec spec = keys.at(index);
    if (spec.isEmpty())
        return;
    if (spec.back) {
        m_downAt.insert(index, QDateTime::currentMSecsSinceEpoch());
        drawKey(index, true);
        return;
    }
    if (!spec.folder.isEmpty()) {
        enterFolder(index);
        return;
    }
    const int page = m_page;
    const QString profile = m_profile;
    const QList<int> folder = m_folderPath;
    try {
        if (!spec.key.isEmpty() || !spec.keys.isEmpty()) {
            const QList<int> codes = Keyboard::codes(spec.key.isEmpty() ? spec.keys : spec.key);
            m_keyboard->press(codes);
            m_held.insert(index, codes);
        }
        if (!spec.sequence.isEmpty() && !m_typing) {
            QList<QList<int>> steps;
            for (const QString &step : spec.sequence.split(QLatin1Char(' '), Qt::SkipEmptyParts))
                steps.append(Keyboard::codes(step));
            const QList<int> hold = spec.hold.isEmpty() ? QList<int>{} : Keyboard::codes(spec.hold);
            m_typing = true;
            auto *thread = QThread::create([this, steps, hold, step = spec.stepMs] {
                m_keyboard->typeSequence(steps, hold, step);
                QMetaObject::invokeMethod(this, [this] { m_typing = false; }, Qt::QueuedConnection);
            });
            connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
        }
        if (!spec.exec.isEmpty()) {
            const QStringList args = qEnvironmentVariableIsSet("UWSM_WAIT_VARNAMES")
                ? QStringList{QStringLiteral("app"), QStringLiteral("--"), QStringLiteral("sh"), QStringLiteral("-c"), spec.exec}
                : QStringList{QStringLiteral("-c"), spec.exec};
            const QString program = qEnvironmentVariableIsSet("UWSM_WAIT_VARNAMES") ? QStringLiteral("uwsm") : QStringLiteral("sh");
            QProcess::startDetached(program, args);
        }
        if (!spec.media.isEmpty()) {
            if (!QStandardPaths::findExecutable(QStringLiteral("playerctl")).isEmpty())
                QProcess::startDetached(QStringLiteral("playerctl"), {spec.media});
            m_lcdDirty = true;
        }
        if (!spec.page.isEmpty())
            gotoPage(spec.page);
        if (!spec.profile.isEmpty())
            setProfile(spec.profile);
        if (!spec.setTheme.isEmpty()) {
            setTheme(spec.setTheme);
            return;
        }
        if (spec.hasBrightness) {
            setBrightness(spec.brightness);
            return;
        }
    } catch (const std::exception &error) {
        qWarning() << "key" << index << error.what();
    }
    if (m_page == page && m_profile == profile && m_folderPath == folder)
        drawKey(index, true);
}

void Service::keyUp(int index)
{
    if (m_held.contains(index))
        m_keyboard->release(m_held.take(index));
    if (m_downAt.contains(index)) {
        const qint64 started = m_downAt.take(index);
        const QList<KeySpec> keys = currentKeys();
        if (index >= 0 && index < keys.size() && keys.at(index).back) {
            leaveFolder(QDateTime::currentMSecsSinceEpoch() - started >= FolderHoldMs);
            return;
        }
    }
    const QList<KeySpec> keys = currentKeys();
    if (index >= 0 && index < keys.size() && !keys.at(index).isEmpty())
        drawKey(index, false);
}

void Service::handleReport(const QByteArray &data)
{
    if (data.size() < 5 || static_cast<unsigned char>(data.at(0)) != 0x01)
        return;
    const auto at = [&](int index) { return static_cast<unsigned char>(data.at(index)); };
    if (at(1) == 0x00) {
        for (int i = 0; i < Keys && 4 + i < data.size(); ++i) {
            const int down = at(4 + i) ? 1 : 0;
            if (down && !m_keyState.at(i))
                keyDown(i);
            else if (!down && m_keyState.at(i))
                keyUp(i);
            m_keyState[i] = down;
        }
        return;
    }
    if (at(1) == 0x03 && data.size() >= 7) {
        const int kind = at(4);
        const int raw0 = at(5);
        const int raw1 = at(6);
        if (kind == 1) {
            const int d0 = raw0 > 127 ? raw0 - 256 : raw0;
            const int d1 = raw1 > 127 ? raw1 - 256 : raw1;
            if (d0)
                changeVolume(d0);
            if (d1) {
                if (m_dialDown[1]) {
                    m_dialTurned[1] = true;
                    const QStringList names = [&] {
                        QStringList out;
                        for (const Profile &profile : m_config.profiles)
                            out.append(profile.name);
                        return out;
                    }();
                    const int index = names.indexOf(m_profile);
                    if (index >= 0 && !names.isEmpty())
                        setProfile(names.at((index + (d1 > 0 ? 1 : names.size() - 1)) % names.size()));
                } else {
                    setPage(m_page + (d1 > 0 ? 1 : -1), false);
                }
            }
        } else if (kind == 0) {
            for (int n = 0; n < 2; ++n) {
                const bool down = (n == 0 ? raw0 : raw1) != 0;
                if (down && !m_dialDown[n])
                    m_dialTurned[n] = false;
                else if (!down && m_dialDown[n] && !m_dialTurned[n]) {
                    if (n == 0) {
                        toggleMute();
                        readVolume(m_volume, m_muted);
                        m_lcdDirty = true;
                        drawLcd();
                    } else if (currentProfile()) {
                        m_history.clear();
                        const int home = startPageIndex();
                        if (m_page == home && !m_folderPath.isEmpty()) {
                            m_folderPath.clear();
                            releaseHeld();
                            drawKeys();
                            m_lcdDirty = true;
                            drawLcd();
                        } else {
                            setPage(home, false);
                        }
                    }
                }
                m_dialDown[n] = down;
            }
        }
    }
}

} // namespace galleon
