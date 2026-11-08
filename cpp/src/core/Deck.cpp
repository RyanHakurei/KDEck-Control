#include "Deck.hpp"
#include "Types.hpp"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace kdeck {
namespace {

QByteArray packet(const QByteArray &header, const QByteArray &chunk)
{
    QByteArray out(1024, '\0');
    memcpy(out.data(), header.constData(), static_cast<size_t>(header.size()));
    memcpy(out.data() + header.size(), chunk.constData(), static_cast<size_t>(chunk.size()));
    return out;
}

} // namespace

Deck::~Deck()
{
    close();
}

QString Deck::findHidraw()
{
    const QDir dir(QStringLiteral("/sys/class/hidraw"));
    const auto nodes = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    const QString id = QStringLiteral("%1:%2").arg(QLatin1String(DeckVid), QLatin1String(DeckPid));
    for (const QString &name : nodes) {
        QFile uevent(dir.absoluteFilePath(name) + QStringLiteral("/device/uevent"));
        if (!uevent.open(QIODevice::ReadOnly))
            continue;
        const QString text = QString::fromUtf8(uevent.readAll());
        if (!text.contains(id))
            continue;
        // symLinkTarget is the relative link. Interface 0 is only visible
        // once that link is resolved, the same way Python uses realpath.
        const QString device = QFileInfo(dir.absoluteFilePath(name) + QStringLiteral("/device")).canonicalFilePath();
        if (device.contains(QStringLiteral(":1.0/")))
            return QStringLiteral("/dev/") + name;
    }
    return {};
}

bool Deck::open(const QString &path)
{
    close();
    m_fd = ::open(path.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK);
    if (m_fd < 0) {
        qWarning() << "open" << path << strerror(errno);
        return false;
    }
    return true;
}

void Deck::close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

bool Deck::feature(const QByteArray &payload)
{
    if (m_fd < 0)
        return false;
    unsigned char buffer[32] = {};
    const int n = std::min(payload.size(), static_cast<qsizetype>(sizeof buffer));
    memcpy(buffer, payload.constData(), static_cast<size_t>(n));
    if (ioctl(m_fd, HIDIOCSFEATURE(32), buffer) < 0) {
        qWarning() << "feature report:" << strerror(errno);
        return false;
    }
    return true;
}

bool Deck::ping()
{
    return feature(QByteArray::fromHex("0327"));
}

bool Deck::setBrightness(int percent)
{
    QByteArray payload(3, '\0');
    payload[0] = 0x03;
    payload[1] = 0x08;
    payload[2] = static_cast<char>(std::clamp(percent, 0, 100));
    return feature(payload);
}

bool Deck::writeAll(const QByteArray &report)
{
    if (m_fd < 0)
        return false;
    const char *data = report.constData();
    qsizetype left = report.size();
    while (left > 0) {
        const ssize_t wrote = ::write(m_fd, data, static_cast<size_t>(left));
        if (wrote < 0) {
            if (errno == EINTR)
                continue;
            qWarning() << "hid write:" << strerror(errno);
            return false;
        }
        data += wrote;
        left -= wrote;
    }
    return true;
}

bool Deck::keyImage(int index, const QByteArray &jpeg)
{
    int part = 0;
    for (int off = 0; off < jpeg.size();) {
        const int chunkSize = std::min(1016, static_cast<int>(jpeg.size() - off));
        const bool last = off + chunkSize >= jpeg.size();
        QByteArray header(8, '\0');
        header[0] = 0x02;
        header[1] = 0x07;
        header[2] = static_cast<char>(index);
        header[3] = last ? 1 : 0;
        header[4] = static_cast<char>(chunkSize & 0xff);
        header[5] = static_cast<char>((chunkSize >> 8) & 0xff);
        header[6] = static_cast<char>(part & 0xff);
        header[7] = static_cast<char>((part >> 8) & 0xff);
        if (!writeAll(packet(header, jpeg.mid(off, chunkSize))))
            return false;
        off += chunkSize;
        ++part;
    }
    return true;
}

bool Deck::lcdImage(const QByteArray &jpeg, int x, int y, int w, int h)
{
    int part = 0;
    for (int off = 0; off < jpeg.size();) {
        const int chunkSize = std::min(1008, static_cast<int>(jpeg.size() - off));
        const bool last = off + chunkSize >= jpeg.size();
        QByteArray header(16, '\0');
        auto put16 = [&](int at, int value) {
            header[at] = static_cast<char>(value & 0xff);
            header[at + 1] = static_cast<char>((value >> 8) & 0xff);
        };
        header[0] = 0x02;
        header[1] = 0x0c;
        put16(2, x);
        put16(4, y);
        put16(6, w);
        put16(8, h);
        header[10] = last ? 1 : 0;
        put16(11, part);
        put16(13, chunkSize);
        header[15] = 0;
        if (!writeAll(packet(header, jpeg.mid(off, chunkSize))))
            return false;
        off += chunkSize;
        ++part;
    }
    return true;
}

QByteArray Deck::read() const
{
    if (m_fd < 0)
        return {};
    QByteArray buffer(1024, '\0');
    const ssize_t n = ::read(m_fd, buffer.data(), static_cast<size_t>(buffer.size()));
    if (n < 0)
        return {};
    buffer.resize(static_cast<int>(n));
    return buffer;
}

} // namespace kdeck
