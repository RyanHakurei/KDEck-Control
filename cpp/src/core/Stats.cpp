#include "Stats.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <optional>

namespace kdeck {
namespace {

constexpr double FreqMax = 6000.0;
constexpr double TempMax = 100.0;
constexpr double PowerMax = 400.0;

struct SensorMeta {
    double maximum = 0;
};

struct Cache {
    qint64 metaAt = 0;
    qint64 dataAt = 0;
    QString gpu = QStringLiteral("gpu0");
    QString pool;
    QHash<QString, SensorMeta> meta;
    QHash<QString, StatReading> readings;
    bool warned = false;
};

Cache &cache()
{
    static Cache state;
    return state;
}

void note(const QString &message)
{
    Cache &state = cache();
    if (state.warned)
        return;
    state.warned = true;
    qWarning().noquote() << "system stats:" << message;
}

QString shortCpu(QString text)
{
    text.replace(QStringLiteral("(R)"), QStringLiteral(" "));
    text.replace(QStringLiteral("(TM)"), QStringLiteral(" "));
    text.replace(QStringLiteral("®"), QStringLiteral(" "));
    text.replace(QStringLiteral("™"), QStringLiteral(" "));
    text = text.simplified();
    const QRegularExpression match(QStringLiteral("((?:Core )?i[3-9]-\\S+|Ryzen \\d+ \\S+|Core Ultra \\S+)"));
    const auto found = match.match(text);
    if (found.hasMatch())
        return found.captured(1).left(22);
    text.remove(QRegularExpression(QStringLiteral("\\b\\d+(?:st|nd|rd|th) Gen\\b")));
    for (const QString &prefix : {QStringLiteral("Intel "), QStringLiteral("AMD ")}) {
        if (text.startsWith(prefix))
            text = text.mid(prefix.size());
    }
    return text.simplified().left(22);
}

QString cpuModel()
{
    static QString cached;
    static bool loaded = false;
    if (loaded)
        return cached;
    loaded = true;
    QFile file(QStringLiteral("/proc/cpuinfo"));
    if (!file.open(QIODevice::ReadOnly))
        return cached;
    // /proc/cpuinfo reports a size of 0. QFile::atEnd() is then true before any
    // byte is read, so the model line was skipped and the CPU key had no subtitle.
    const QByteArray data = file.readAll();
    qsizetype start = 0;
    while (start < data.size()) {
        qsizetype end = data.indexOf('\n', start);
        if (end < 0)
            end = data.size();
        const QString line = QString::fromUtf8(data.constData() + start, end - start);
        if (line.startsWith(QStringLiteral("model name"), Qt::CaseInsensitive)) {
            cached = shortCpu(line.section(QLatin1Char(':'), 1));
            break;
        }
        start = end + 1;
    }
    return cached;
}

QString shortGpu(QString text)
{
    text = text.trimmed();
    if (text.isEmpty())
        return {};
    for (const QString &prefix : {QStringLiteral("NVIDIA GeForce "), QStringLiteral("AMD Radeon "), QStringLiteral("Intel ")}) {
        if (text.startsWith(prefix)) {
            text = text.mid(prefix.size());
            break;
        }
    }
    if (text.contains(QLatin1Char('[')) && text.endsWith(QLatin1Char(']')))
        text = text.mid(text.lastIndexOf(QLatin1Char('[')) + 1, text.size() - text.lastIndexOf(QLatin1Char('[')) - 2);
    return text.trimmed().left(22);
}

QString fmtBytes(double n)
{
    n = std::max(0.0, n);
    const QStringList units{QStringLiteral("B"), QStringLiteral("KB"), QStringLiteral("MB"), QStringLiteral("GB"),
                            QStringLiteral("TB"), QStringLiteral("PB")};
    for (const QString &unit : units) {
        if (n < 1024.0 || unit == QLatin1String("PB")) {
            if (unit == QLatin1String("B"))
                return QStringLiteral("%1 B").arg(n, 0, 'f', 0);
            const QString text = n >= 100.0 ? QString::number(n, 'f', 0) : QString::number(n, 'f', 1);
            return text + QLatin1Char(' ') + unit;
        }
        n /= 1024.0;
    }
    return QStringLiteral("%1 PB").arg(n, 0, 'f', 0);
}

QString fmtPct(std::optional<double> percent)
{
    if (!percent)
        return QStringLiteral("—");
    return QStringLiteral("%1%").arg(qRound(*percent));
}

QString fmtTemp(std::optional<double> celsius)
{
    if (!celsius)
        return QStringLiteral("—");
    return QStringLiteral("%1°").arg(qRound(*celsius));
}

QString fmtFreq(std::optional<double> mhz)
{
    if (!mhz)
        return QStringLiteral("—");
    if (*mhz >= 1000.0)
        return QStringLiteral("%1 GHz").arg(*mhz / 1000.0, 0, 'f', 1);
    return QStringLiteral("%1 MHz").arg(qRound(*mhz));
}

QString fmtPower(std::optional<double> watts)
{
    if (!watts)
        return QStringLiteral("—");
    return QStringLiteral("%1 W").arg(qRound(*watts));
}

QString rate(std::optional<double> value)
{
    if (!value)
        return QStringLiteral("—");
    return fmtBytes(*value) + QStringLiteral("/s");
}

QString ratio(std::optional<double> used, std::optional<double> total)
{
    if (!used || !total || *total <= 0)
        return {};
    return fmtBytes(*used) + QStringLiteral(" / ") + fmtBytes(*total);
}

StatReading gauge(const QString &value, std::optional<double> percent, const QString &sub = {})
{
    StatReading reading;
    reading.value = value;
    reading.sub = sub;
    if (percent)
        reading.bar = std::clamp(*percent / 100.0, 0.0, 1.0);
    return reading;
}

std::optional<double> asNumber(const QVariant &value)
{
    if (!value.isValid() || value.typeId() == QMetaType::Bool || value.isNull())
        return std::nullopt;
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok)
        return std::nullopt;
    return number;
}

std::optional<double> asPercent(const QVariant &value)
{
    const auto number = asNumber(value);
    if (!number)
        return std::nullopt;
    return std::clamp(*number, 0.0, 100.0);
}

bool loadMeta()
{
    Cache &state = cache();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!state.meta.isEmpty() && now - state.metaAt < 30000)
        return true;
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.ksystemstats1"),
                                                          QStringLiteral("/org/kde/ksystemstats1"),
                                                          QStringLiteral("org.kde.ksystemstats1"),
                                                          QStringLiteral("allSensors"));
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 1500);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        note(reply.errorMessage().isEmpty() ? QStringLiteral("allSensors failed") : reply.errorMessage());
        return false;
    }
    const QDBusArgument arg = reply.arguments().at(0).value<QDBusArgument>();
    QHash<QString, SensorMeta> meta;
    arg.beginMap();
    while (!arg.atEnd()) {
        arg.beginMapEntry();
        QString key;
        QString name;
        QString shortName;
        QString description;
        quint32 unit = 0;
        qint32 kind = 0;
        double min = 0;
        double max = 0;
        arg >> key;
        arg.beginStructure();
        arg >> name >> shortName >> description >> unit >> kind >> min >> max;
        arg.endStructure();
        arg.endMapEntry();
        meta.insert(key, SensorMeta{max});
    }
    arg.endMap();
    state.meta = meta;
    state.metaAt = now;

    QString gpu = QStringLiteral("gpu0");
    double bestGpu = -1;
    QSet<QString> gpus;
    for (auto it = meta.cbegin(); it != meta.cend(); ++it) {
        const QStringList parts = it.key().split(QLatin1Char('/'));
        if (parts.size() == 3 && parts.at(0) == QLatin1String("gpu") && parts.at(1).startsWith(QLatin1String("gpu"))) {
            bool digits = false;
            parts.at(1).mid(3).toInt(&digits);
            if (digits)
                gpus.insert(parts.at(1));
        }
    }
    QStringList gpuNames = gpus.values();
    gpuNames.sort();
    for (const QString &name : gpuNames) {
        const double score = std::max(meta.value(QStringLiteral("gpu/%1/usedVram").arg(name)).maximum,
                                       meta.value(QStringLiteral("gpu/%1/totalVram").arg(name)).maximum);
        if (score > bestGpu) {
            bestGpu = score;
            gpu = name;
        }
    }
    state.gpu = gpu;

    QString pool;
    double bestPool = -1;
    QSet<QString> pools;
    for (auto it = meta.cbegin(); it != meta.cend(); ++it) {
        const QStringList parts = it.key().split(QLatin1Char('/'));
        if (parts.size() == 4 && parts.at(0) == QLatin1String("zfs") && parts.at(1) == QLatin1String("pool"))
            pools.insert(parts.at(2));
    }
    QStringList poolNames = pools.values();
    poolNames.sort();
    for (const QString &name : poolNames) {
        const double score = std::max(meta.value(QStringLiteral("zfs/pool/%1/size").arg(name)).maximum,
                                       meta.value(QStringLiteral("zfs/pool/%1/allocated").arg(name)).maximum);
        if (score > bestPool) {
            bestPool = score;
            pool = name;
        }
    }
    state.pool = pool;
    return true;
}

double limitOf(const QString &sensor, double fallback, double floor)
{
    const double max = cache().meta.value(sensor).maximum;
    return max >= floor ? max : fallback;
}

QHash<QString, QVariant> queryValues(const QStringList &ids)
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.ksystemstats1"),
                                                          QStringLiteral("/org/kde/ksystemstats1"),
                                                          QStringLiteral("org.kde.ksystemstats1"),
                                                          QStringLiteral("sensorData"));
    message << ids;
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 800);
    QHash<QString, QVariant> values;
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        note(reply.errorMessage().isEmpty() ? QStringLiteral("sensorData failed") : reply.errorMessage());
        return values;
    }
    const QDBusArgument arg = reply.arguments().at(0).value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd()) {
        arg.beginStructure();
        QString sensor;
        QDBusVariant value;
        arg >> sensor >> value;
        arg.endStructure();
        values.insert(sensor, value.variant());
    }
    arg.endArray();
    return values;
}

QHash<QString, StatReading> formatReadings(const QHash<QString, QVariant> &values, const QString &gpu, const QString &pool)
{
    auto get = [&](const QString &sensor) { return values.value(sensor); };
    const auto cpu = asPercent(get(QStringLiteral("cpu/all/usage")));
    const auto temp = asNumber(get(QStringLiteral("cpu/all/averageTemperature")));
    const auto freq = asNumber(get(QStringLiteral("cpu/all/averageFrequency")));
    const auto ram = asPercent(get(QStringLiteral("memory/physical/usedPercent")));
    const auto swap = asPercent(get(QStringLiteral("memory/swap/usedPercent")));
    const auto disk = asPercent(get(QStringLiteral("disk/all/usedPercent")));
    const auto usage = asPercent(get(QStringLiteral("gpu/%1/usage").arg(gpu)));
    const auto gpuTemp = asNumber(get(QStringLiteral("gpu/%1/temperature").arg(gpu)));
    const auto power = asNumber(get(QStringLiteral("gpu/%1/power").arg(gpu)));
    const auto vram = asPercent(get(QStringLiteral("gpu/%1/usedPercent").arg(gpu)));
    const double freqMax = limitOf(QStringLiteral("cpu/all/averageFrequency"), FreqMax, 1000);

    QHash<QString, StatReading> out;
    out.insert(QStringLiteral("cpu"), gauge(fmtPct(cpu), cpu, cpuModel()));
    out.insert(QStringLiteral("cpu-temp"), gauge(fmtTemp(temp), temp ? std::optional<double>(*temp / TempMax * 100.0) : std::nullopt));
    out.insert(QStringLiteral("cpu-freq"), gauge(fmtFreq(freq), freq ? std::optional<double>(*freq / freqMax * 100.0) : std::nullopt));
    out.insert(QStringLiteral("ram"), gauge(fmtPct(ram), ram, ratio(asNumber(get(QStringLiteral("memory/physical/used"))),
                                                                     asNumber(get(QStringLiteral("memory/physical/total"))))));
    out.insert(QStringLiteral("swap"), gauge(fmtPct(swap), swap, ratio(asNumber(get(QStringLiteral("memory/swap/used"))),
                                                                       asNumber(get(QStringLiteral("memory/swap/total"))))));
    out.insert(QStringLiteral("disk"), gauge(fmtPct(disk), disk, ratio(asNumber(get(QStringLiteral("disk/all/used"))),
                                                                       asNumber(get(QStringLiteral("disk/all/total"))))));
    out.insert(QStringLiteral("gpu"), gauge(fmtPct(usage), usage, shortGpu(get(QStringLiteral("gpu/%1/name").arg(gpu)).toString())));
    out.insert(QStringLiteral("gpu-temp"), gauge(fmtTemp(gpuTemp), gpuTemp ? std::optional<double>(*gpuTemp / TempMax * 100.0) : std::nullopt));
    const double powerMax = limitOf(QStringLiteral("gpu/%1/power").arg(gpu), PowerMax, 50);
    out.insert(QStringLiteral("gpu-power"), gauge(fmtPower(power), power ? std::optional<double>(*power / powerMax * 100.0) : std::nullopt));
    out.insert(QStringLiteral("vram"), gauge(fmtPct(vram), vram, ratio(asNumber(get(QStringLiteral("gpu/%1/usedVram").arg(gpu))),
                                                                       asNumber(get(QStringLiteral("gpu/%1/totalVram").arg(gpu))))));
    StatReading net;
    net.value = rate(asNumber(get(QStringLiteral("network/all/download"))));
    net.sub = QStringLiteral("up ") + rate(asNumber(get(QStringLiteral("network/all/upload"))));
    out.insert(QStringLiteral("net"), net);
    StatReading io;
    io.value = rate(asNumber(get(QStringLiteral("disk/all/read"))));
    io.sub = QStringLiteral("write ") + rate(asNumber(get(QStringLiteral("disk/all/write"))));
    out.insert(QStringLiteral("io"), io);

    const QString base = pool.isEmpty() ? QString() : QStringLiteral("zfs/pool/%1").arg(pool);
    const auto capacity = base.isEmpty() ? std::nullopt : asPercent(get(base + QStringLiteral("/capacity")));
    const auto frag = base.isEmpty() ? std::nullopt : asPercent(get(base + QStringLiteral("/fragmentation")));
    QString health = get(base + QStringLiteral("/health")).toString().trimmed();
    if (health.isEmpty())
        health = QStringLiteral("—");
    const auto used = pool.isEmpty() ? std::nullopt : asNumber(get(QStringLiteral("zfs/dataset/%1/used").arg(pool)));
    const auto avail = pool.isEmpty() ? std::nullopt : asNumber(get(QStringLiteral("zfs/dataset/%1/available").arg(pool)));
    const auto data = pool.isEmpty() ? std::nullopt : asPercent(get(QStringLiteral("zfs/dataset/%1/usedPercent").arg(pool)));
    const auto arc = asNumber(get(QStringLiteral("zfs/arc/size")));
    const auto arcTarget = asNumber(get(QStringLiteral("zfs/arc/target")));
    std::optional<double> arcPct;
    if (arc && arcTarget && *arcTarget > 0)
        arcPct = std::clamp(*arc / *arcTarget * 100.0, 0.0, 100.0);
    const auto hit = asPercent(get(QStringLiteral("zfs/arc/hitRatio")));
    const auto recent = asPercent(get(QStringLiteral("zfs/arc/recentHitRatio")));
    out.insert(QStringLiteral("zfs"), gauge(fmtPct(capacity), capacity,
                                            base.isEmpty() ? QString() : ratio(asNumber(get(base + QStringLiteral("/allocated"))),
                                                                                asNumber(get(base + QStringLiteral("/size"))))));
    out.insert(QStringLiteral("zfs-health"), gauge(health, std::nullopt, pool));
    out.insert(QStringLiteral("zfs-frag"), gauge(fmtPct(frag), frag, pool));
    StatReading zfsIo;
    zfsIo.value = base.isEmpty() ? QStringLiteral("—") : rate(asNumber(get(base + QStringLiteral("/read"))));
    zfsIo.sub = QStringLiteral("write ") + (base.isEmpty() ? QStringLiteral("—") : rate(asNumber(get(base + QStringLiteral("/write")))));
    out.insert(QStringLiteral("zfs-io"), zfsIo);
    std::optional<double> dataTotal;
    if (used && avail)
        dataTotal = *used + *avail;
    out.insert(QStringLiteral("zfs-data"), gauge(fmtPct(data), data, ratio(used, dataTotal)));
    out.insert(QStringLiteral("arc"), gauge(fmtPct(arcPct), arcPct, ratio(arc, arcTarget)));
    out.insert(QStringLiteral("arc-hit"), gauge(fmtPct(hit), hit, recent ? QStringLiteral("now %1%").arg(qRound(*recent)) : QString()));
    return out;
}

} // namespace

StatReading statReading(const QString &id)
{
    Cache &state = cache();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!state.readings.isEmpty() && now - state.dataAt < 900)
        return state.readings.value(id, StatReading{});

    const bool haveMeta = loadMeta();
    const QString gpu = state.gpu.isEmpty() ? QStringLiteral("gpu0") : state.gpu;
    QStringList ids{QStringLiteral("cpu/all/usage"),
                    QStringLiteral("cpu/all/averageTemperature"),
                    QStringLiteral("cpu/all/averageFrequency"),
                    QStringLiteral("memory/physical/used"),
                    QStringLiteral("memory/physical/total"),
                    QStringLiteral("memory/physical/usedPercent"),
                    QStringLiteral("memory/swap/used"),
                    QStringLiteral("memory/swap/total"),
                    QStringLiteral("memory/swap/usedPercent"),
                    QStringLiteral("disk/all/used"),
                    QStringLiteral("disk/all/total"),
                    QStringLiteral("disk/all/usedPercent"),
                    QStringLiteral("disk/all/read"),
                    QStringLiteral("disk/all/write"),
                    QStringLiteral("network/all/download"),
                    QStringLiteral("network/all/upload"),
                    QStringLiteral("gpu/%1/usage").arg(gpu),
                    QStringLiteral("gpu/%1/temperature").arg(gpu),
                    QStringLiteral("gpu/%1/power").arg(gpu),
                    QStringLiteral("gpu/%1/name").arg(gpu),
                    QStringLiteral("gpu/%1/usedVram").arg(gpu),
                    QStringLiteral("gpu/%1/totalVram").arg(gpu),
                    QStringLiteral("gpu/%1/usedPercent").arg(gpu)};
    if (!state.pool.isEmpty()) {
        const QString base = QStringLiteral("zfs/pool/%1").arg(state.pool);
        for (const char *name : {"capacity", "allocated", "size", "fragmentation", "health", "read", "write"})
            ids.append(base + QLatin1Char('/') + QLatin1String(name));
        const QString root = QStringLiteral("zfs/dataset/%1").arg(state.pool);
        ids.append(root + QStringLiteral("/used"));
        ids.append(root + QStringLiteral("/available"));
        ids.append(root + QStringLiteral("/usedPercent"));
    }
    ids.append(QStringLiteral("zfs/arc/size"));
    ids.append(QStringLiteral("zfs/arc/target"));
    ids.append(QStringLiteral("zfs/arc/hitRatio"));
    ids.append(QStringLiteral("zfs/arc/recentHitRatio"));
    if (haveMeta) {
        QStringList known;
        for (const QString &sensor : ids) {
            if (!sensor.startsWith(QLatin1String("zfs/")) || state.meta.contains(sensor))
                known.append(sensor);
        }
        ids = known;
    }

    const QHash<QString, QVariant> values = queryValues(ids);
    if (values.isEmpty()) {
        if (!state.readings.isEmpty() && now - state.dataAt < 15000)
            return state.readings.value(id, StatReading{});
        return {};
    }
    state.readings = formatReadings(values, gpu, state.pool);
    state.dataAt = now;
    state.warned = false;
    return state.readings.value(id, StatReading{});
}

} // namespace kdeck
