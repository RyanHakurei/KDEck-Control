#include "Service.hpp"

#include "Anim.hpp"
#include "Apps.hpp"
#include "Deck.hpp"
#include "Keyboard.hpp"
#include "Media.hpp"
#include "Render.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>

#include <poll.h>

#include <cerrno>
#include <cmath>

#include <linux/input-event-codes.h>

#include <algorithm>
#include <cstdlib>

namespace galleon {
namespace {

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

QString statePath()
{
    const QByteArray runtime = qgetenv("XDG_RUNTIME_DIR");
    const QString dir = runtime.isEmpty() ? QStringLiteral("/tmp") : QString::fromUtf8(runtime);
    return dir + QStringLiteral("/galleon-deck.state");
}

QString pythonDir()
{
    const QString installed = QFileInfo(QString::fromUtf8(GALLEAN_ADDON_SCRIPT)).absolutePath();
    if (QFileInfo::exists(installed + QStringLiteral("/galleon_deck.py")))
        return installed;
#ifdef GALLEAN_ADDON_SRC
    const QString source = QFileInfo(QString::fromUtf8(GALLEAN_ADDON_SRC)).absolutePath();
    if (QFileInfo::exists(source + QStringLiteral("/galleon_deck.py")))
        return source;
#endif
    return {};
}

int mediaKey(const QString &action)
{
    if (action == QLatin1String("next"))
        return KEY_NEXTSONG;
    if (action == QLatin1String("previous"))
        return KEY_PREVIOUSSONG;
    if (action == QLatin1String("stop"))
        return KEY_STOPCD;
    return KEY_PLAYPAUSE;
}

QString focusSourceName()
{
    const bool x11 = qgetenv("XDG_SESSION_TYPE") == "x11" || (!qgetenv("DISPLAY").isEmpty() && qgetenv("WAYLAND_DISPLAY").isEmpty());
    if (!qgetenv("HYPRLAND_INSTANCE_SIGNATURE").isEmpty())
        return QStringLiteral("hyprland");
    if (!qgetenv("SWAYSOCK").isEmpty() && !QStandardPaths::findExecutable(QStringLiteral("swaymsg")).isEmpty())
        return QStringLiteral("sway");
    if (!qgetenv("I3SOCK").isEmpty() && !QStandardPaths::findExecutable(QStringLiteral("i3-msg")).isEmpty())
        return QStringLiteral("i3");
    if (!qgetenv("NIRI_SOCKET").isEmpty() && !QStandardPaths::findExecutable(QStringLiteral("niri")).isEmpty())
        return QStringLiteral("niri");
    if (x11 && !QStandardPaths::findExecutable(QStringLiteral("xprop")).isEmpty())
        return QStringLiteral("x11");
    return {};
}

void writePidFile()
{
    QSaveFile file(runtimePath(QStringLiteral("galleon-deck.pid")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QByteArray::number(QCoreApplication::applicationPid()));
    file.commit();
}

void removePidFile()
{
    const QString path = runtimePath(QStringLiteral("galleon-deck.pid"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    bool ok = false;
    const qint64 pid = QString::fromUtf8(file.readAll()).trimmed().toLongLong(&ok);
    file.close();
    if (ok && pid == QCoreApplication::applicationPid())
        QFile::remove(path);
}

qint64 jsonId(const QJsonValue &value)
{
    return static_cast<qint64>(value.toDouble());
}

QString shownThemeName(const Profile *profile)
{
    if (!profile)
        return {};
    return profile->followTheme ? profile->theme.name : profile->themeName;
}

// Same cases as the Python service's run(): an explicit uwsm session, or a
// compositor started by uwsm. Cached so a key press does not ask systemd twice.
bool launchUnderUwsm()
{
    if (qEnvironmentVariableIsSet("UWSM_WAIT_VARNAMES"))
        return true;
    static const int cached = [] {
        if (QStandardPaths::findExecutable(QStringLiteral("uwsm")).isEmpty())
            return 0;
        QProcess proc;
        proc.start(QStringLiteral("systemctl"),
                   {QStringLiteral("--user"), QStringLiteral("is-active"), QStringLiteral("--quiet"),
                    QStringLiteral("wayland-wm@*.service")});
        if (!proc.waitForFinished(2000))
            return 0;
        return proc.exitCode() == 0 ? 1 : 0;
    }();
    return cached == 1;
}

// The unit sets QT_QPA_PLATFORM=offscreen so this process has no window.
// A key that opens an app must not pass that on, or Qt programs such as
// Dolphin and Konsole start with no display. Python never set the variable.
QProcessEnvironment launchEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("QT_QPA_PLATFORM"));
    return env;
}

bool startDetachedInSession(const QString &program, const QStringList &args)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(args);
    process.setProcessEnvironment(launchEnvironment());
    return process.startDetached();
}

} // namespace

class FocusThread : public QThread {
public:
    FocusThread(Service *service, const QString &source, QObject *parent)
        : QThread(parent)
        , m_service(service)
        , m_source(source)
    {
    }

    void stop() { m_stop.storeRelaxed(1); }

protected:
    void run() override
    {
        while (!m_stop.loadRelaxed()) {
            if (m_source == QLatin1String("hyprland"))
                watchHyprland();
            else if (m_source == QLatin1String("sway") || m_source == QLatin1String("i3"))
                watchMessages(m_source == QLatin1String("sway") ? QStringLiteral("swaymsg") : QStringLiteral("i3-msg"));
            else if (m_source == QLatin1String("niri"))
                watchNiri();
            else if (m_source == QLatin1String("x11"))
                watchX11();
            else
                return;
            if (m_stop.loadRelaxed())
                return;
            QThread::msleep(3000);
        }
    }

private:
    void deliver(const QString &windowClass, const QString &title)
    {
        if (m_stop.loadRelaxed())
            return;
        Service *service = m_service;
        QMetaObject::invokeMethod(service, [service, windowClass, title] { service->applyFocus(windowClass, title); }, Qt::QueuedConnection);
    }

    bool waitLine(QProcess &process, QString &line)
    {
        while (!m_stop.loadRelaxed()) {
            if (process.canReadLine()) {
                line = QString::fromUtf8(process.readLine()).trimmed();
                return true;
            }
            if (process.state() == QProcess::NotRunning)
                return false;
            process.waitForReadyRead(500);
        }
        return false;
    }

    void watchHyprland()
    {
        const QString path = runtimePath(QStringLiteral("hypr/%1/.socket2.sock").arg(QString::fromUtf8(qgetenv("HYPRLAND_INSTANCE_SIGNATURE"))));
        QLocalSocket socket;
        socket.connectToServer(path);
        if (!socket.waitForConnected(3000))
            return;
        QByteArray buffer;
        while (!m_stop.loadRelaxed()) {
            if (socket.state() != QLocalSocket::ConnectedState)
                return;
            if (!socket.waitForReadyRead(500))
                continue;
            buffer += socket.readAll();
            while (true) {
                const int newline = buffer.indexOf('\n');
                if (newline < 0)
                    break;
                const QString line = QString::fromUtf8(buffer.left(newline));
                buffer.remove(0, newline + 1);
                const int split = line.indexOf(QStringLiteral(">>"));
                if (split < 0 || line.left(split) != QLatin1String("activewindow"))
                    continue;
                const QString payload = line.mid(split + 2);
                const int comma = payload.indexOf(QLatin1Char(','));
                deliver(comma < 0 ? payload : payload.left(comma), comma < 0 ? QString() : payload.mid(comma + 1));
            }
        }
    }

    void watchMessages(const QString &tool)
    {
        QProcess process;
        process.start(tool, {QStringLiteral("-t"), QStringLiteral("subscribe"), QStringLiteral("-m"), QStringLiteral("[\"window\"]")});
        if (!process.waitForStarted(3000))
            return;
        QString line;
        while (waitLine(process, line)) {
            const QJsonObject event = QJsonDocument::fromJson(line.toUtf8()).object();
            const QString change = event.value(QStringLiteral("change")).toString();
            if (change != QLatin1String("focus") && change != QLatin1String("title"))
                continue;
            const QJsonObject container = event.value(QStringLiteral("container")).toObject();
            QString windowClass = container.value(QStringLiteral("app_id")).toString();
            if (windowClass.isEmpty())
                windowClass = container.value(QStringLiteral("window_properties")).toObject().value(QStringLiteral("class")).toString();
            deliver(windowClass, container.value(QStringLiteral("name")).toString());
        }
        process.kill();
        process.waitForFinished(1000);
    }

    void watchNiri()
    {
        QProcess process;
        process.start(QStringLiteral("niri"), {QStringLiteral("msg"), QStringLiteral("--json"), QStringLiteral("event-stream")});
        if (!process.waitForStarted(3000))
            return;
        QHash<qint64, QJsonObject> windows;
        qint64 focused = -1;
        bool haveFocus = false;
        QString line;
        while (waitLine(process, line)) {
            const QJsonObject event = QJsonDocument::fromJson(line.toUtf8()).object();
            bool changed = false;
            if (event.contains(QStringLiteral("WindowsChanged"))) {
                windows.clear();
                const QJsonArray list = event.value(QStringLiteral("WindowsChanged")).toObject().value(QStringLiteral("windows")).toArray();
                for (const QJsonValue &value : list) {
                    const QJsonObject window = value.toObject();
                    windows.insert(jsonId(window.value(QStringLiteral("id"))), window);
                    if (window.value(QStringLiteral("is_focused")).toBool()) {
                        focused = jsonId(window.value(QStringLiteral("id")));
                        haveFocus = true;
                    }
                }
                changed = true;
            } else if (event.contains(QStringLiteral("WindowOpenedOrChanged"))) {
                const QJsonObject window = event.value(QStringLiteral("WindowOpenedOrChanged")).toObject().value(QStringLiteral("window")).toObject();
                windows.insert(jsonId(window.value(QStringLiteral("id"))), window);
                if (window.value(QStringLiteral("is_focused")).toBool()) {
                    focused = jsonId(window.value(QStringLiteral("id")));
                    haveFocus = true;
                }
                changed = true;
            } else if (event.contains(QStringLiteral("WindowFocusChanged"))) {
                const QJsonValue id = event.value(QStringLiteral("WindowFocusChanged")).toObject().value(QStringLiteral("id"));
                if (id.isNull())
                    haveFocus = false;
                else {
                    focused = jsonId(id);
                    haveFocus = true;
                }
                changed = true;
            }
            if (!changed)
                continue;
            const QJsonObject window = haveFocus ? windows.value(focused) : QJsonObject();
            deliver(window.value(QStringLiteral("app_id")).toString(), window.value(QStringLiteral("title")).toString());
        }
        process.kill();
        process.waitForFinished(1000);
    }

    void watchX11()
    {
        QProcess process;
        process.start(QStringLiteral("xprop"), {QStringLiteral("-root"), QStringLiteral("-spy"), QStringLiteral("_NET_ACTIVE_WINDOW")});
        if (!process.waitForStarted(3000))
            return;
        const QRegularExpression idRx(QStringLiteral("window id # (0x[0-9a-fA-F]+)"));
        const QRegularExpression quoted(QStringLiteral("\"([^\"]*)\""));
        const QRegularExpression titleRx(QStringLiteral("_NET_WM_NAME\\(\\w+\\) = \"(.*)\""));
        QString line;
        while (waitLine(process, line)) {
            const auto id = idRx.match(line);
            if (!id.hasMatch())
                continue;
            bool ok = false;
            if (id.captured(1).toULongLong(&ok, 16) == 0 || !ok)
                continue;
            QProcess props;
            props.start(QStringLiteral("xprop"), {QStringLiteral("-id"), id.captured(1), QStringLiteral("WM_CLASS"), QStringLiteral("_NET_WM_NAME")});
            if (!props.waitForFinished(1500))
                continue;
            const QString text = QString::fromUtf8(props.readAllStandardOutput());
            QString windowClass;
            auto matches = quoted.globalMatch(text.section(QLatin1Char('\n'), 0, 0));
            while (matches.hasNext())
                windowClass = matches.next().captured(1);
            const auto title = titleRx.match(text);
            deliver(windowClass, title.hasMatch() ? title.captured(1) : QString());
        }
        process.kill();
        process.waitForFinished(1000);
    }

    Service *m_service;
    QString m_source;
    QAtomicInt m_stop{0};
};

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
    // 10 ms, so a 20 ms frame is not skipped between polls.
    m_anim.setInterval(10);
    connect(&m_ping, &QTimer::timeout, this, &Service::onPing);
    connect(&m_tick, &QTimer::timeout, this, &Service::onTick);
    connect(&m_retry, &QTimer::timeout, this, &Service::tryConnect);
    connect(&m_anim, &QTimer::timeout, this, &Service::drawDueAnimations);

    Config::ensureExamples();
    writePidFile();
    reload();
    tryConnect();
    if (!m_deck)
        m_retry.start();
    m_tick.start();
    m_anim.start();
    const QString source = focusSourceName();
    if (source.isEmpty())
        qInfo() << "auto-switch: no window focus source for this desktop; switch profiles by key or dial";
    else
        m_focus = new FocusThread(this, source, this);
    if (m_focus)
        m_focus->start();
}

Service::~Service()
{
    if (m_focus) {
        m_focus->stop();
        m_focus->wait(5000);
    }
    removePidFile();
    stopRepeats();
    if (m_sequenceThread) {
        m_sequenceThread->wait(3000);
        delete m_sequenceThread;
        m_sequenceThread = nullptr;
    }
    releaseHeld();
    disconnectDeck();
    QFile::remove(statePath());
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
    playBoot();
    if (!m_deck)
        return;
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
    if (++m_tickCount % 10 == 0)
        followWallpaper();
    reload();
    readVolume(m_volume, m_muted);
    if (m_deck) {
        drawLcd();
        drawStatKeys();
    }
    publishState();
}

void Service::reload()
{
    const qint64 stamp = configStamp();
    if (stamp == m_configStamp && m_configStamp >= 0)
        return;
    m_configStamp = stamp;
    m_jpegCache.clear();
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
    return galleon::settingsKeys(m_config, m_profile);
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

QString animJpegKey(const KeySpec &spec, const Theme &theme, const QString &activeTheme, int frame, bool pressed, const QFileInfo &info)
{
    return spec.image + QLatin1Char('\n') + QString::number(info.lastModified().toMSecsSinceEpoch()) + QLatin1Char('\n')
        + QString::number(info.size()) + QLatin1Char('\n') + QString::number(frame) + QLatin1Char('\n')
        + (pressed ? QStringLiteral("1") : QStringLiteral("0")) + QLatin1Char('\n') + (spec.fill ? QStringLiteral("1") : QStringLiteral("0"))
        + QLatin1Char('\n') + spec.label + QLatin1Char('\n') + spec.icon + QLatin1Char('\n') + spec.fg + QLatin1Char('\n') + spec.bg
        + QLatin1Char('\n') + spec.border + QLatin1Char('\n') + activeTheme + QLatin1Char('\n') + theme.font + QLatin1Char('\n')
        + theme.iconFont + QLatin1Char('\n') + theme.keyStyle + QLatin1Char('\n') + QString::number(theme.radius) + QLatin1Char('\n')
        + QString::number(theme.borderWidth) + QLatin1Char('\n') + theme.fg.name() + QLatin1Char('\n') + theme.keyBg.name()
        + QLatin1Char('\n') + theme.bg.name() + QLatin1Char('\n') + theme.accent.name() + QLatin1Char('\n') + theme.dim.name()
        + QLatin1Char('\n') + theme.border.name() + QLatin1Char('\n') + theme.tick.name();
}

void Service::drawKey(int index, bool pressed)
{
    if (!m_deck)
        return;
    const QList<KeySpec> keys = currentKeys();
    if (index < 0 || index >= keys.size())
        return;
    const KeySpec &spec = keys.at(index);
    const Theme theme = currentTheme();
    const QString active = shownThemeName(currentProfile());
    const double now = unixSeconds();
    const QString imagePath = configPath(m_config.dir, spec.image);
    const int frame = spec.stat.isEmpty() && !spec.image.isEmpty() ? imageFrameIndex(imagePath, now) : -1;
    QString cacheKey;
    if (frame >= 0)
        cacheKey = animJpegKey(spec, theme, active, frame, pressed, QFileInfo(imagePath));
    if (frame < 0) {
        m_animShown.remove(index);
    } else {
        const bool shown = pressed || m_repeating.contains(index);
        m_animShown.insert(index, spec.image + QLatin1Char('\n') + QString::number(frame) + (shown ? QStringLiteral("\n1") : QStringLiteral("\n0")));
    }
    QByteArray jpeg;
    if (!cacheKey.isEmpty()) {
        const auto found = m_jpegCache.constFind(cacheKey);
        if (found != m_jpegCache.cend())
            jpeg = found.value();
    }
    if (jpeg.isEmpty()) {
        jpeg = encodeJpeg(renderDeckKey(spec, theme, m_config.themes, active, pressed, now));
        if (!cacheKey.isEmpty()) {
            m_jpegCache.insert(cacheKey, jpeg);
            while (m_jpegCache.size() > 512)
                m_jpegCache.erase(m_jpegCache.begin());
        }
    }
    if (!m_deck->keyImage(index, jpeg))
        disconnectDeck();
}

void Service::drawDueAnimations()
{
    if (!m_deck)
        return;
    const QList<KeySpec> keys = currentKeys();
    const double now = unixSeconds();
    const qint64 clock = QDateTime::currentMSecsSinceEpoch();
    QElapsedTimer budget;
    budget.start();
    int sent = 0;
    for (int i = 0; i < keys.size(); ++i) {
        if (m_flashUntil.value(i, 0) > clock)
            continue;
        const KeySpec &spec = keys.at(i);
        if (!spec.stat.isEmpty() || spec.image.isEmpty()) {
            m_animShown.remove(i);
            continue;
        }
        const int frame = imageFrameIndex(configPath(m_config.dir, spec.image), now);
        if (frame < 0) {
            m_animShown.remove(i);
            continue;
        }
        const bool shown = m_held.contains(i) || m_repeating.contains(i);
        const QString mark = spec.image + QLatin1Char('\n') + QString::number(frame) + (shown ? QStringLiteral("\n1") : QStringLiteral("\n0"));
        if (m_animShown.value(i) == mark)
            continue;
        drawKey(i, m_held.contains(i));
        // Leave the thread free for a key report. The next wake sends the rest.
        if (++sent >= 1 && budget.elapsed() >= 8)
            return;
    }
}

void Service::drawKeys()
{
    for (int i = 0; i < Keys; ++i)
        drawKey(i, false);
}

void Service::drawStatKeys()
{
    const QList<KeySpec> keys = currentKeys();
    for (int i = 0; i < keys.size(); ++i) {
        if (!keys.at(i).stat.isEmpty() && !m_held.contains(i) && !m_downAt.contains(i))
            drawKey(i, m_repeating.contains(i));
    }
}

void Service::drawLcd()
{
    if (!m_deck)
        return;
    const Profile *profile = currentProfile();
    LcdState state = keyboardStatus();
    fillMedia(state, m_config.mediaPlayers);
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

void Service::stopRepeats()
{
    for (auto it = m_repeating.begin(); it != m_repeating.end(); ++it) {
        if (it->stop)
            it->stop->storeRelaxed(1);
    }
    for (auto it = m_repeating.begin(); it != m_repeating.end(); ++it) {
        if (!it->thread)
            continue;
        it->thread->wait(2000);
        delete it->thread;
    }
    m_repeating.clear();
}

void Service::toggleRepeat(int index, const KeySpec &spec)
{
    if (m_repeating.contains(index)) {
        RepeatSlot slot = m_repeating.take(index);
        if (slot.stop)
            slot.stop->storeRelaxed(1);
        if (slot.thread) {
            slot.thread->wait(2000);
            delete slot.thread;
        }
        drawKey(index, false);
        return;
    }
    if (spec.repeat.trimmed().toUpper().startsWith(QLatin1String("KP")))
        m_keyboard->ensureNumlock();
    QList<int> codes;
    try {
        codes = Keyboard::codes(spec.repeat);
    } catch (const std::exception &error) {
        qWarning() << "repeat" << error.what();
        return;
    }
    const int interval = std::clamp(spec.repeatMs, 20, 10000);
    auto stop = QSharedPointer<QAtomicInt>::create(0);
    Keyboard *keyboard = m_keyboard.get();
    auto *thread = QThread::create([keyboard, codes, interval, stop] {
        while (stop->loadRelaxed() == 0) {
            keyboard->press(codes);
            QThread::msleep(20);
            keyboard->release(codes);
            for (int waited = 0; waited < interval && stop->loadRelaxed() == 0; waited += 20)
                QThread::msleep(20);
        }
    });
    RepeatSlot slot;
    slot.stop = stop;
    slot.thread = thread;
    m_repeating.insert(index, slot);
    thread->start();
    drawKey(index, true);
}

void Service::releaseHeld()
{
    stopRepeats();
    for (auto it = m_held.cbegin(); it != m_held.cend(); ++it)
        m_keyboard->release(it.value());
    m_held.clear();
    m_downAt.clear();
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
    publishState();
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
    playTransition();
    m_lcdDirty = true;
    drawLcd();
    publishState();
}

void Service::enterFolder(int index)
{
    releaseHeld();
    m_folderPath.append(index);
    drawKeys();
    m_lcdDirty = true;
    drawLcd();
    publishState();
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
        publishState();
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
    const QString path = profile->path;
    if (profile->followTheme) {
        const QString shown = profile->theme.name.isEmpty() ? QStringLiteral("amber") : profile->theme.name;
        if (name == shown) {
            const Theme stock = m_config.themes.value(name);
            const Theme current = profile->theme;
            const auto pin = [&](const char *field, const QColor &now, const QColor &base) {
                if (!now.isValid() || now.name(QColor::HexRgb) == base.name(QColor::HexRgb))
                    return;
                assignTableKey(path, QStringLiteral("theme_overrides"), QString::fromLatin1(field),
                               QStringLiteral("\"%1\"").arg(now.name(QColor::HexRgb)));
            };
            pin("bg", current.bg, stock.bg);
            pin("key_bg", current.keyBg, stock.keyBg);
            pin("fg", current.fg, stock.fg);
            pin("accent", current.accent, stock.accent);
            pin("frame", current.frame, stock.frame);
            pin("dim", current.dim, stock.dim);
            pin("border", current.border, stock.border);
        }
    }
    assignTopLevel(path, QStringLiteral("theme"), QStringLiteral("\"%1\"").arg(name));
    removeTopLevel(path, QStringLiteral("follow_theme"));
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
            const QString combo = spec.key.isEmpty() ? spec.keys : spec.key;
            if (combo.trimmed().toUpper().startsWith(QLatin1String("KP")))
                m_keyboard->ensureNumlock();
            const QList<int> codes = Keyboard::codes(combo);
            m_keyboard->press(codes);
            m_held.insert(index, codes);
        }
        if (!spec.sequence.isEmpty() && !m_typing.loadRelaxed()) {
            QList<QList<int>> steps;
            for (const QString &step : spec.sequence.split(QLatin1Char(' '), Qt::SkipEmptyParts))
                steps.append(Keyboard::codes(step));
            const QList<int> hold = spec.hold.isEmpty() ? QList<int>{} : Keyboard::codes(spec.hold);
            m_typing.storeRelaxed(1);
            Keyboard *keyboard = m_keyboard.get();
            QAtomicInt *typing = &m_typing;
            if (m_sequenceThread) {
                m_sequenceThread->wait(3000);
                delete m_sequenceThread;
            }
            m_sequenceThread = QThread::create([keyboard, steps, hold, step = spec.stepMs, typing] {
                keyboard->typeSequence(steps, hold, step);
                typing->storeRelaxed(0);
            });
            m_sequenceThread->start();
        }
        if (!spec.repeat.isEmpty())
            toggleRepeat(index, spec);
        if (!spec.exec.isEmpty()) {
            const QStringList args = qEnvironmentVariableIsSet("UWSM_WAIT_VARNAMES")
                ? QStringList{QStringLiteral("app"), QStringLiteral("--"), QStringLiteral("sh"), QStringLiteral("-c"), spec.exec}
                : QStringList{QStringLiteral("-c"), spec.exec};
            const QString program = qEnvironmentVariableIsSet("UWSM_WAIT_VARNAMES") ? QStringLiteral("uwsm") : QStringLiteral("sh");
            if (spec.confirm)
                confirmCommand(index, program, args, page, profile, folder);
            else
                startDetachedInSession(program, args);
        }
        if (!spec.app.isEmpty()) {
            const QStringList argv = applicationArgv(spec.app, spec.args);
            if (argv.isEmpty()) {
                qWarning().noquote() << "app: can't find" << spec.app;
            } else if (launchUnderUwsm()) {
                QStringList wrapped{QStringLiteral("app"), QStringLiteral("--")};
                wrapped += argv;
                startDetachedInSession(QStringLiteral("uwsm"), wrapped);
            } else {
                startDetachedInSession(argv.first(), argv.mid(1));
            }
        }
        if (!spec.media.isEmpty()) {
            if (!QStandardPaths::findExecutable(QStringLiteral("playerctl")).isEmpty())
                QProcess::startDetached(QStringLiteral("playerctl"), {spec.media});
            else
                m_keyboard->tap({mediaKey(spec.media)});
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
        if (spec.wallpaperTheme) {
            applyWallpaperTheme();
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
                            publishState();
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

void Service::publishState()
{
    const Profile *profile = currentProfile();
    QString pageName;
    if (profile && m_page >= 0 && m_page < profile->pages.size())
        pageName = profile->pages.at(m_page).name;
    QJsonArray folder;
    for (int index : m_folderPath)
        folder.append(index);
    QJsonObject object;
    object.insert(QStringLiteral("profile"), m_profile);
    object.insert(QStringLiteral("page"), pageName);
    object.insert(QStringLiteral("folder"), folder);
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (bytes == m_published)
        return;
    QSaveFile file(statePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(bytes);
    if (!file.commit())
        return;
    m_published = bytes;
}

void Service::confirmCommand(int index, const QString &program, const QStringList &args, int page, const QString &profile, const QList<int> &folder)
{
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process, index, page, profile, folder](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        flashResult(index, status == QProcess::NormalExit && code == 0, page, profile, folder);
    });
    process->setProcessEnvironment(launchEnvironment());
    process->start(program, args);
}

void Service::flashResult(int index, bool ok, int page, const QString &profile, const QList<int> &folder)
{
    auto showing = [this, page, profile, folder] {
        return m_deck && m_page == page && m_profile == profile && m_folderPath == folder;
    };
    KeySpec face;
    face.icon = QString::fromUcs4(ok ? U"\U000F012C" : U"\U000F0156");
    face.label = ok ? QStringLiteral("done") : QStringLiteral("failed");
    face.bg = ok ? QStringLiteral("accent") : QStringLiteral("frame");
    face.fg = QStringLiteral("key_bg");
    face.border = QStringLiteral("key_bg");
    const Theme theme = currentTheme();
    m_flashUntil.insert(index, QDateTime::currentMSecsSinceEpoch() + 1700);
    auto once = [this, index, face, theme, showing] {
        if (!showing())
            return;
        m_deck->keyImage(index, encodeJpeg(renderKey(face, theme, false)));
        QTimer::singleShot(600, this, [this, index, showing] {
            if (showing())
                drawKey(index, false);
        });
    };
    QTimer::singleShot(150, this, [once] { once(); });
    QTimer::singleShot(950, this, [once] { once(); });
}

QString Service::focusTarget(const QString &windowClass, const QString &title) const
{
    const auto matches = [](const SwitchRule &rule, const QString &windowClass, const QString &title) {
        const QRegularExpression classRx(rule.classPattern);
        const QRegularExpression titleRx(rule.titlePattern);
        if (!classRx.isValid() || !titleRx.isValid())
            return false;
        return classRx.match(windowClass).hasMatch() && titleRx.match(title).hasMatch();
    };
    for (const Profile &profile : m_config.profiles) {
        for (const SwitchRule &rule : profile.autoSwitch) {
            if (!matches(rule, windowClass, title))
                continue;
            return rule.profile.isEmpty() ? profile.name : rule.profile;
        }
    }
    for (const SwitchRule &rule : m_config.autoRules) {
        if (matches(rule, windowClass, title))
            return rule.profile;
    }
    return m_config.autoFallback;
}

void Service::applyFocus(const QString &windowClass, const QString &title)
{
    const QString target = focusTarget(windowClass, title);
    if (m_autoTarget && *m_autoTarget == target)
        return;
    m_autoTarget = target;
    if (!target.isEmpty())
        setProfile(target);
}

bool Service::inputPending(int timeoutMs) const
{
    if (!m_deck)
        return false;
    pollfd ready{};
    ready.fd = m_deck->fd();
    ready.events = POLLIN;
    while (true) {
        const int rc = ::poll(&ready, 1, std::max(0, timeoutMs));
        if (rc < 0 && errno == EINTR)
            continue;
        return rc > 0;
    }
}

void Service::drainInput()
{
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 50;
    while (m_deck && QDateTime::currentMSecsSinceEpoch() < deadline) {
        const int left = static_cast<int>(deadline - QDateTime::currentMSecsSinceEpoch());
        if (left <= 0 || !inputPending(left))
            return;
        if (m_deck->read().isEmpty())
            return;
    }
}

QImage Service::settledKey(int index) const
{
    const Theme theme = currentTheme();
    const QList<KeySpec> keys = currentKeys();
    if (index < 0 || index >= keys.size())
        return renderKey(KeySpec{}, theme, false, -1);
    return renderDeckKey(keys.at(index), theme, m_config.themes, shownThemeName(currentProfile()), false, -1);
}

void Service::playBoot()
{
    if (!m_deck || !m_config.bootEnabled)
        return;
    const Theme theme = currentTheme();
    QImage blank(KeyPx, KeyPx, QImage::Format_RGB32);
    blank.fill(theme.keyBg);
    const QByteArray blankJpeg = encodeJpeg(blank);
    for (int i = 0; i < Keys; ++i) {
        if (!m_deck->keyImage(i, blankJpeg)) {
            disconnectDeck();
            return;
        }
    }
    const QString footer = m_profile.isEmpty() ? QStringLiteral("DECK ONLINE")
                                               : QStringLiteral("DECK ONLINE // %1").arg(m_profile.toUpper());
    std::unique_ptr<BootSequence> sequence;
    if (!m_config.bootGif.isEmpty()) {
        auto gif = std::make_unique<BootSequence>(configPath(m_config.dir, m_config.bootGif), theme);
        if (gif->valid())
            sequence = std::move(gif);
    }
    if (!sequence) {
        const QString logo = m_config.bootLogo.isEmpty() ? QString() : configPath(m_config.dir, m_config.bootLogo);
        sequence = std::make_unique<BootSequence>(theme, m_config.bootTitle, m_config.bootLines, logo, m_config.bootSpeed, footer,
                                                  [this](int index) { return settledKey(index); });
    }
    const auto interrupted = [this](int timeoutMs) {
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
        while (m_deck) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now >= deadline)
                return false;
            if (!inputPending(static_cast<int>(deadline - now)))
                return false;
            const QByteArray report = m_deck->read();
            if (report.isEmpty())
                return false;
            if (report.size() < 5 || static_cast<unsigned char>(report.at(0)) != 0x01)
                continue;
            const int end = std::min(static_cast<int>(report.size()), 4 + Keys);
            for (int i = 4; i < end; ++i) {
                if (static_cast<unsigned char>(report.at(i)) != 0)
                    return true;
            }
        }
        return true;
    };
    PlayFrame frame;
    while (m_deck && sequence->next(frame)) {
        if (!m_deck->lcdImage(encodeJpeg(frame.lcd))) {
            disconnectDeck();
            return;
        }
        for (int i = 0; i < Keys && m_deck; ++i) {
            if (frame.realMask & (1u << i))
                drawKey(i, false);
            else if ((frame.sendMask & (1u << i)) && !m_deck->keyImage(i, encodeJpeg(frame.keys[i]))) {
                disconnectDeck();
                return;
            }
        }
        if (!m_deck)
            return;
        if (interrupted(std::max(0, static_cast<int>(std::lround(frame.seconds * 1000)))))
            break;
    }
    drainInput();
    m_lastLcd.clear();
    m_lcdDirty = true;
}

void Service::playTransition()
{
    if (!m_deck || !m_config.transitionEnabled) {
        drawKeys();
        return;
    }
    const double speed = std::clamp(m_config.transitionSpeed, 0.25, 4.0);
    const int frames = std::max(2, static_cast<int>(std::lround(9.0 / speed)));
    const double hold = std::clamp(m_config.transitionHold, 0.0, 3.0) / speed;
    const Theme theme = currentTheme();
    const Profile *profile = currentProfile();
    QImage clean[Keys];
    for (int i = 0; i < Keys; ++i)
        clean[i] = settledKey(i);
    const QImage art = profile ? loadDeckScreen(configPath(m_config.dir, profile->screen)) : QImage();
    const QImage logo = art.isNull() ? loadDeckLogo(profile ? configPath(m_config.dir, profile->logo) : QString(), theme, m_profile.toUpper()) : QImage();
    bool skipped = false;
    for (int frame = 0; frame < frames && m_deck; ++frame) {
        const double done = (frame + 1.0) / frames;
        const double amount = std::pow(1.0 - done, 1.5);
        QImage screen = glitchImage(transitionFrame(theme, art, logo, done), amount);
        if (!art.isNull() && profile)
            screen = withPageDots(screen, m_page, profile->pages.size(), theme);
        if (!m_deck->lcdImage(encodeJpeg(screen))) {
            disconnectDeck();
            return;
        }
        for (int i = 0; i < Keys; ++i) {
            if (!m_deck->keyImage(i, encodeJpeg(glitchImage(clean[i], amount)))) {
                disconnectDeck();
                return;
            }
        }
        if (inputPending(std::max(0, static_cast<int>(std::lround(50.0 / speed))))) {
            skipped = true;
            break;
        }
    }
    if (!skipped && m_deck) {
        drawKeys();
        if (hold > 0) {
            QImage held = transitionFrame(theme, art, logo, 1.0);
            if (!art.isNull() && profile)
                held = withPageDots(held, m_page, profile->pages.size(), theme);
            if (!m_deck->lcdImage(encodeJpeg(held))) {
                disconnectDeck();
                return;
            }
            inputPending(static_cast<int>(std::lround(hold * 1000)));
        }
    }
    drawKeys();
    m_lastLcd.clear();
    m_lcdDirty = true;
}

void Service::followWallpaper()
{
    if (!m_config.wallpaperAuto)
        return;
    const QString dir = pythonDir();
    if (dir.isEmpty())
        return;
    const QString prelude = QStringLiteral("import galleon_deck as g\n"
                                           "cfg = {}\n"
                                           "try:\n"
                                           "    import tomllib\n"
                                           "    with open(g.CONFIG_PATH, 'rb') as f:\n"
                                           "        cfg = tomllib.load(f).get('wallpaper_theme', {}) or {}\n"
                                           "except Exception:\n"
                                           "    pass\n");
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONPATH"), dir);
    QProcess query;
    query.setProcessEnvironment(env);
    query.start(QStringLiteral("python3"), {QStringLiteral("-c"), prelude + QStringLiteral("print(g.wallpaper_path(cfg) or '')\n")});
    if (!query.waitForFinished(4000) || query.exitCode() != 0)
        return;
    const QString path = QString::fromUtf8(query.readAllStandardOutput()).trimmed();
    if (path.isEmpty() || wallpaperThemeMatches(m_config.dir, path))
        return;
    QProcess write;
    write.setProcessEnvironment(env);
    write.start(QStringLiteral("python3"),
                {QStringLiteral("-c"), prelude + QStringLiteral("raise SystemExit(0 if g.write_wallpaper_theme(cfg) else 1)\n")});
    if (!write.waitForFinished(8000) || write.exitCode() != 0)
        return;
    m_configStamp = -1;
}

void Service::applyWallpaperTheme()
{
    const QString dir = pythonDir();
    if (dir.isEmpty())
        return;
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONPATH"), dir);
    process.setProcessEnvironment(env);
    process.start(QStringLiteral("python3"),
                  {QStringLiteral("-c"),
                   QStringLiteral("import galleon_deck as g\n"
                                  "cfg = {}\n"
                                  "try:\n"
                                  "    import tomllib\n"
                                  "    with open(g.CONFIG_PATH, 'rb') as f:\n"
                                  "        cfg = tomllib.load(f).get('wallpaper_theme', {})\n"
                                  "except Exception:\n"
                                  "    pass\n"
                                  "raise SystemExit(0 if g.write_wallpaper_theme(cfg) else 1)\n")});
    if (!process.waitForFinished(8000) || process.exitCode() != 0)
        return;
    setTheme(QStringLiteral("wallpaper"));
}

} // namespace galleon
