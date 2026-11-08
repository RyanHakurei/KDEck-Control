#include "Media.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace galleon {
namespace {

struct MediaCache {
    qint64 at = 0;
    QString key;
    QString title;
    QString artist;
    QString artPath;
    int position = -1;
    int length = -1;
    bool playing = false;
};

struct Track {
    QString artist;
    QString title;
    QString artUrl;
    int position = -1;
    int length = -1;
    bool playing = false;
    bool ok = false;
};

// playerctl matches a name exactly, or as the part before the first dot
// (firefox matches firefox.instance123, and does not match firefoxdev).
bool playerMatches(const QString &instance, const QString &pattern)
{
    if (pattern.isEmpty() || instance.isEmpty())
        return false;
    if (pattern.contains(QLatin1Char('*')) || pattern.contains(QLatin1Char('?'))) {
        const QRegularExpression re(QRegularExpression::wildcardToRegularExpression(pattern));
        return re.match(instance).hasMatch();
    }
    if (instance == pattern)
        return true;
    return instance.size() > pattern.size() && instance.startsWith(pattern)
        && instance.at(pattern.size()) == QLatin1Char('.');
}

bool playerAllowed(const QString &instance, const QStringList &players)
{
    for (const QString &pattern : players) {
        if (playerMatches(instance, pattern))
            return true;
    }
    return false;
}

QString playerStem(const QString &instance)
{
    const int dot = instance.indexOf(QLatin1Char('.'));
    if (dot > 0)
        return instance.left(dot);
    return instance;
}

Track parseTrack(const QStringList &parts, int statusIndex)
{
    Track track;
    if (parts.size() <= statusIndex)
        return track;
    const QString status = parts.at(statusIndex).trimmed();
    if (status != QLatin1String("Playing") && status != QLatin1String("Paused"))
        return track;
    track.ok = true;
    track.playing = status == QLatin1String("Playing");
    track.artist = parts.value(statusIndex + 1).trimmed();
    track.title = parts.value(statusIndex + 2).trimmed();
    track.artUrl = parts.value(statusIndex + 3).trimmed();
    bool ok = false;
    const qint64 positionUs = parts.value(statusIndex + 4).trimmed().toLongLong(&ok);
    if (ok)
        track.position = static_cast<int>(positionUs / 1000000);
    ok = false;
    const qint64 lengthUs = parts.value(statusIndex + 5).trimmed().toLongLong(&ok);
    if (ok && lengthUs > 0)
        track.length = static_cast<int>(lengthUs / 1000000);
    return track;
}

QString artFile(const QString &url);

QString runPlayerctl(const QStringList &args)
{
    QProcess process;
    process.start(QStringLiteral("playerctl"), args);
    if (!process.waitForFinished(800))
        return {};
    return QString::fromUtf8(process.readAllStandardOutput());
}

void storeTrack(MediaCache &media, const Track &track)
{
    media.playing = track.playing;
    media.artist = track.artist;
    media.title = track.title;
    media.position = track.position;
    media.length = track.length;
    media.artPath = artFile(track.artUrl);
}

QString artFile(const QString &url)
{
    if (url.isEmpty())
        return {};
    if (url.startsWith(QLatin1String("file://"))) {
        const QString path = QUrl(url).toLocalFile();
        return QFileInfo::exists(path) ? path : QString();
    }
    if (url.startsWith(QLatin1Char('/')) && QFileInfo::exists(url))
        return url;
    if (!url.startsWith(QLatin1String("http://")) && !url.startsWith(QLatin1String("https://")))
        return {};

    const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/galleon-deck");
    QDir().mkpath(cache);
    const QString dest = cache + QLatin1Char('/')
        + QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
    if (QFileInfo(dest).size() > 0)
        return dest;

    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("galleon-deck"));
    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(1500);
    loop.exec();
    const bool timedOut = !timer.isActive();
    timer.stop();
    if (timedOut || reply->error() != QNetworkReply::NoError) {
        reply->abort();
        reply->deleteLater();
        return {};
    }
    const QByteArray data = reply->read(2'000'000);
    reply->deleteLater();
    if (data.isEmpty())
        return {};
    QSaveFile file(dest);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(data);
    return file.commit() ? dest : QString();
}

void apply(LcdState &state, const MediaCache &media)
{
    state.title = media.title;
    state.artist = media.artist;
    state.artPath = media.artPath;
    state.position = media.position;
    state.length = media.length;
    state.playing = media.playing;
}

} // namespace

void fillMedia(LcdState &state, const QStringList &players)
{
    static MediaCache media;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString key = players.join(QLatin1Char('\n'));
    if (media.at > 0 && media.key == key && now - media.at < 900) {
        apply(state, media);
        return;
    }
    media = {};
    media.at = now;
    media.key = key;
    if (!QStandardPaths::findExecutable(QStringLiteral("playerctl")).isEmpty()) {
        if (players.isEmpty()) {
            const QString out = runPlayerctl(
                {QStringLiteral("metadata"), QStringLiteral("--format"),
                 QStringLiteral("{{status}}\t{{artist}}\t{{title}}\t{{mpris:artUrl}}\t{{position}}\t{{mpris:length}}")});
            const Track track = parseTrack(out.split(QLatin1Char('\t')), 0);
            if (track.ok)
                storeTrack(media, track);
        } else {
            const QString out = runPlayerctl(
                {QStringLiteral("--all-players"), QStringLiteral("metadata"), QStringLiteral("--format"),
                 QStringLiteral("{{playerName}}\t{{status}}\t{{artist}}\t{{title}}\t{{mpris:artUrl}}\t{{position}}\t{{mpris:length}}")});
            Track chosen;
            int rank = 0;
            const QStringList lines = out.split(QLatin1Char('\n'));
            for (const QString &line : lines) {
                if (line.trimmed().isEmpty())
                    continue;
                const QStringList parts = line.split(QLatin1Char('\t'));
                if (parts.isEmpty() || !playerAllowed(parts.at(0).trimmed(), players))
                    continue;
                const Track track = parseTrack(parts, 1);
                if (!track.ok)
                    continue;
                const int next = track.playing ? 2 : 1;
                if (next <= rank)
                    continue;
                rank = next;
                chosen = track;
                if (rank == 2)
                    break;
            }
            if (chosen.ok)
                storeTrack(media, chosen);
        }
    }
    apply(state, media);
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

QStringList listMediaPlayers()
{
    QStringList names;
    if (QStandardPaths::findExecutable(QStringLiteral("playerctl")).isEmpty())
        return names;
    const QStringList lines = runPlayerctl({QStringLiteral("--list-all")}).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString instance = line.trimmed();
        if (instance.isEmpty())
            continue;
        const QString stem = playerStem(instance);
        if (stem == QLatin1String("playerctld") || names.contains(stem))
            continue;
        names.append(stem);
    }
    return names;
}

QString normalizeMediaPlayer(const QString &raw)
{
    const QString name = raw.trimmed();
    if (name.isEmpty())
        return {};
    if (name.contains(QLatin1Char(',')) || name.contains(QLatin1Char('\n')) || name.contains(QLatin1Char('\r')))
        return {};
    if (name.contains(QLatin1Char('*')) || name.contains(QLatin1Char('?')))
        return name;
    return playerStem(name);
}

} // namespace galleon
