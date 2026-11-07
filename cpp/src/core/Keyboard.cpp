#include "Keyboard.hpp"

#include <QHash>
#include <QThread>

#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>

#include <algorithm>
#include <stdexcept>

namespace galleon {
namespace {

QString canonicalName(QString name)
{
    name = name.trimmed().toUpper();
    static const QHash<QString, QString> aliases = {
        {QStringLiteral("CTRL"), QStringLiteral("LEFTCTRL")},
        {QStringLiteral("SHIFT"), QStringLiteral("LEFTSHIFT")},
        {QStringLiteral("ALT"), QStringLiteral("LEFTALT")},
        {QStringLiteral("SUPER"), QStringLiteral("LEFTMETA")},
        {QStringLiteral("WIN"), QStringLiteral("LEFTMETA")},
        {QStringLiteral("META"), QStringLiteral("LEFTMETA")},
    };
    return aliases.value(name, name);
}

} // namespace

Keyboard::Keyboard()
{
    m_dev = libevdev_new();
    if (!m_dev)
        return;
    libevdev_set_name(m_dev, "galleon-deck");
    libevdev_enable_event_type(m_dev, EV_KEY);
    for (int code = 1; code < 0x2ff; ++code)
        libevdev_enable_event_code(m_dev, EV_KEY, code, nullptr);
    if (libevdev_uinput_create_from_device(m_dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &m_uinput) != 0) {
        libevdev_free(m_dev);
        m_dev = nullptr;
        m_uinput = nullptr;
    }
}

Keyboard::~Keyboard()
{
    if (m_uinput)
        libevdev_uinput_destroy(m_uinput);
    if (m_dev)
        libevdev_free(m_dev);
}

QList<int> Keyboard::codes(const QString &combo)
{
    QList<int> out;
    const auto parts = combo.toUpper().split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (QString part : parts) {
        const QString name = canonicalName(part);
        const QByteArray lookup = QByteArray("KEY_") + name.toLatin1();
        const int code = libevdev_event_code_from_name(EV_KEY, lookup.constData());
        if (code < 0)
            throw std::runtime_error(QStringLiteral("unknown key %1").arg(name).toStdString());
        out.append(code);
    }
    if (out.isEmpty())
        throw std::runtime_error("empty key");
    return out;
}

void Keyboard::writeKey(int code, int value)
{
    if (m_uinput)
        libevdev_uinput_write_event(m_uinput, EV_KEY, code, value);
}

void Keyboard::sync()
{
    if (m_uinput)
        libevdev_uinput_write_event(m_uinput, EV_SYN, SYN_REPORT, 0);
}

void Keyboard::press(const QList<int> &codes)
{
    QMutexLocker locker(&m_lock);
    for (int code : codes)
        writeKey(code, 1);
    sync();
}

void Keyboard::release(const QList<int> &codes)
{
    QMutexLocker locker(&m_lock);
    for (auto it = codes.crbegin(); it != codes.crend(); ++it)
        writeKey(*it, 0);
    sync();
}

void Keyboard::tap(const QList<int> &codes)
{
    press(codes);
    release(codes);
}

void Keyboard::typeSequence(const QList<QList<int>> &steps, const QList<int> &hold, int stepMs)
{
    const int pause = std::clamp(stepMs, 10, 500);
    if (!hold.isEmpty()) {
        press(hold);
        QThread::msleep(static_cast<unsigned long>(pause));
    }
    try {
        for (const QList<int> &step : steps) {
            press(step);
            QThread::msleep(static_cast<unsigned long>(pause));
            release(step);
            QThread::msleep(static_cast<unsigned long>(pause));
        }
    } catch (...) {
        if (!hold.isEmpty())
            release(hold);
        throw;
    }
    if (!hold.isEmpty())
        release(hold);
}

} // namespace galleon
