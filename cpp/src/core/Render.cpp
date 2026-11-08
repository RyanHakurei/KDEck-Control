#include "Render.hpp"

#include "Stats.hpp"

#include <QBuffer>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHash>
#include <QImageReader>
#include <QMutex>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace kdeck {
namespace {

struct FontFace {
    QString family;
    QFont::Weight weight = QFont::Normal;
};

FontFace parseFont(const QString &spec)
{
    const int colon = spec.indexOf(QLatin1Char(':'));
    FontFace face;
    face.family = colon < 0 ? spec : spec.left(colon);
    const QString weight = colon < 0 ? QString() : spec.mid(colon + 1).toLower();
    if (weight == QLatin1String("bold"))
        face.weight = QFont::Bold;
    else if (weight == QLatin1String("semibold"))
        face.weight = QFont::DemiBold;
    else if (weight == QLatin1String("light"))
        face.weight = QFont::Light;
    return face;
}

QFont makeFont(const QString &spec, int pixelSize)
{
    const FontFace face = parseFont(spec);
    QFont font(face.family);
    font.setPixelSize(std::max(1, pixelSize));
    font.setWeight(face.weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

enum class FontRole { General, Fixed };

struct FontCache {
    qint64 stamp = -1;
    bool fresh = false;
    QString general;
    QString fixed;
};

FontCache &fontCache()
{
    static FontCache cache;
    return cache;
}

// Read one key from kdeglobals. QSettings rewrites the file, so this only reads.
QString kdeglobalsValue(const QString &path, const QString &key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    bool inGeneral = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inGeneral = line == QLatin1String("[General]");
            continue;
        }
        if (!inGeneral || line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq > 0 && line.left(eq).trimmed() == key)
            return line.mid(eq + 1).trimmed();
    }
    return {};
}

QString familyFromSettings(const QString &saved)
{
    if (saved.isEmpty())
        return {};
    QFont font;
    if (font.fromString(saved) && !font.family().isEmpty())
        return font.family();
    return saved.section(QLatin1Char(','), 0, 0).trimmed();
}

QString kdeFontFamily(FontRole role)
{
    const QString path = QDir::homePath() + QStringLiteral("/.config/kdeglobals");
    const qint64 stamp = QFileInfo::exists(path) ? QFileInfo(path).lastModified().toMSecsSinceEpoch() : 0;
    FontCache &cache = fontCache();
    if (!cache.fresh || stamp != cache.stamp) {
        cache.fresh = true;
        cache.stamp = stamp;
        cache.general = familyFromSettings(kdeglobalsValue(path, QStringLiteral("font")));
        cache.fixed = familyFromSettings(kdeglobalsValue(path, QStringLiteral("fixed")));
        if (QGuiApplication::instance()) {
            if (cache.general.isEmpty())
                cache.general = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
            if (cache.fixed.isEmpty())
                cache.fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
        }
        if (cache.general.isEmpty())
            cache.general = QFont().family();
        if (cache.fixed.isEmpty())
            cache.fixed = cache.general;
    }
    return role == FontRole::Fixed ? cache.fixed : cache.general;
}

QFont makeTextFont(const QString &spec, int pixelSize, FontRole role = FontRole::General)
{
    const FontFace face = parseFont(spec);
    QFont font(kdeFontFamily(role));
    if (spec.contains(QLatin1Char(':')))
        font.setWeight(face.weight);
    font.setPixelSize(std::max(1, pixelSize));
    font.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::NoFontMerging));
    return font;
}

QFont fitFont(const QString &spec, const QString &text, int pixelSize, int maxWidth)
{
    QFont font = makeTextFont(spec, pixelSize);
    while (pixelSize > 10 && QFontMetrics(font).horizontalAdvance(text) > maxWidth) {
        pixelSize -= 2;
        font = makeTextFont(spec, pixelSize);
    }
    return font;
}

// Pillow's "m" anchor is the middle of the em box, not the tight glyph bounds.
enum class HAlign { Left, Center, Right };
void drawMiddle(QPainter &painter, const QPointF &at, const QString &text, const QFont &font, const QColor &colour, HAlign align);

void drawFrame(QPainter &painter, const QRectF &box, const Theme &theme, const QColor &outline, int width, const QColor &tick)
{
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(outline, width));
    if (theme.keyStyle != QLatin1String("hud")) {
        painter.drawRoundedRect(box, theme.radius, theme.radius);
        return;
    }
    const qreal cut = theme.radius;
    QPainterPath path;
    path.moveTo(box.left() + cut, box.top());
    path.lineTo(box.right(), box.top());
    path.lineTo(box.right(), box.bottom() - cut);
    path.lineTo(box.right() - cut, box.bottom());
    path.lineTo(box.left(), box.bottom());
    path.lineTo(box.left(), box.top() + cut);
    path.closeSubpath();
    painter.drawPath(path);
    painter.setPen(QPen(tick, width + 1));
    const qreal arm = std::max<qreal>(12, box.width() / 7);
    painter.drawLine(QPointF(box.right() - arm, box.top()), QPointF(box.right(), box.top()));
    painter.drawLine(QPointF(box.right(), box.top()), QPointF(box.right(), box.top() + arm));
    painter.drawLine(QPointF(box.left(), box.bottom() - arm), QPointF(box.left(), box.bottom()));
    painter.drawLine(QPointF(box.left(), box.bottom()), QPointF(box.left() + arm, box.bottom()));
}

QImage loadImage(const QString &path)
{
    QImage image(path);
    if (image.isNull())
        return {};
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// Same limits as the Python deck: a page of GIFs must not stall the preview,
// and delays under 20 ms are the GIF "as fast as possible" value.
constexpr int MaxAnimFrames = 240;
constexpr int MaxAnimEdge = 320;
constexpr double MinFrameSeconds = 0.02;

struct AnimEntry {
    qint64 mtime = -1;
    qint64 size = -1;
    bool animated = false;
    QList<QImage> frames;
    QList<double> durations;
};

QMutex g_animMutex;
QHash<QString, AnimEntry> g_animCache;

double frameSeconds(int delayMs)
{
    if (delayMs < 20)
        delayMs = 100;
    return std::max(MinFrameSeconds, delayMs / 1000.0);
}

int frameIndexAt(const QList<double> &durations, double when)
{
    if (durations.isEmpty())
        return 0;
    double period = 0;
    for (double duration : durations)
        period += duration;
    if (period <= 0)
        return 0;
    double t = std::fmod(when, period);
    if (t < 0)
        t += period;
    double acc = 0;
    for (int i = 0; i < durations.size(); ++i) {
        acc += durations.at(i);
        if (t < acc)
            return i;
    }
    return durations.size() - 1;
}

AnimEntry decodeAnimation(const QString &path)
{
    AnimEntry entry;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    reader.setDecideFormatFromContent(true);
    int count = reader.imageCount();
    if (count == 1)
        return entry;
    // imageCount() is 0 when the plugin will not say. Read until it stops,
    // but do not walk an unbounded file.
    if (count <= 0)
        count = MaxAnimFrames * 2;
    // read() already steps to the next frame. The GIF plugin's jumpToNextImage()
    // then returns false, so a pre-jump kept only frame 0 and the key never played.
    bool truncated = false;
    for (int i = 0; i < count; ++i) {
        QImage frame = reader.read();
        if (frame.isNull())
            break;
        if (std::max(frame.width(), frame.height()) > MaxAnimEdge)
            frame = frame.scaled(MaxAnimEdge, MaxAnimEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        frame = frame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const double duration = frameSeconds(reader.nextImageDelay());
        // A run of identical pictures is one hold. Exports pad movement with
        // copies, and resending those copies stalls the bus.
        if (!entry.frames.isEmpty() && frame == entry.frames.last()) {
            entry.durations.last() += duration;
            continue;
        }
        if (entry.frames.size() >= MaxAnimFrames) {
            truncated = true;
            break;
        }
        entry.frames.append(frame);
        entry.durations.append(duration);
    }
    if (truncated)
        qInfo().noquote() << "image" << path << ": playing the first" << MaxAnimFrames << "changed frames of" << count;
    if (entry.frames.size() <= 1) {
        entry.frames.clear();
        entry.durations.clear();
        return entry;
    }
    entry.animated = true;
    return entry;
}

AnimEntry preparedAnimation(const QString &path)
{
    if (path.isEmpty())
        return {};
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return {};
    const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
    const qint64 size = info.size();
    QMutexLocker locker(&g_animMutex);
    const auto found = g_animCache.constFind(path);
    if (found != g_animCache.cend() && found->mtime == mtime && found->size == size)
        return *found;
    AnimEntry entry = decodeAnimation(path);
    entry.mtime = mtime;
    entry.size = size;
    g_animCache.insert(path, entry);
    while (g_animCache.size() > 24) {
        auto it = g_animCache.begin();
        if (it.key() == path)
            ++it;
        if (it == g_animCache.end())
            break;
        g_animCache.erase(it);
    }
    return entry;
}

QImage keyPicture(const QString &path, double when)
{
    const AnimEntry entry = preparedAnimation(path);
    if (!entry.animated)
        return loadImage(path);
    const int index = when < 0 ? 0 : frameIndexAt(entry.durations, when);
    return entry.frames.value(index);
}

QImage fitCover(const QImage &source, const QSize &size)
{
    const QImage scaled = source.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = std::max(0, (scaled.width() - size.width()) / 2);
    const int y = std::max(0, (scaled.height() - size.height()) / 2);
    return scaled.copy(x, y, size.width(), size.height());
}

void darken(QImage &image)
{
    image = image.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            line[x] = qRgb(qRed(line[x]) * 55 / 100, qGreen(line[x]) * 55 / 100, qBlue(line[x]) * 55 / 100);
        }
    }
}

QString glyph(char32_t code)
{
    return QString::fromUcs4(&code, 1);
}

QImage blankKey(const Theme &theme)
{
    QImage image(KeyPx, KeyPx, QImage::Format_RGB32);
    image.fill(theme.keyBg);
    return image;
}

// Page dots from the clock screen. `backing` is the dark disc Python draws only
// on top of a profile picture, so the rings stay readable.
void drawPageDotsColoured(QPainter &painter, const LcdState &state, const Theme &theme, bool backing)
{
    for (int i = 0; i < state.pageCount; ++i) {
        const int x = LcdW - 36 - (state.pageCount - 1 - i) * 30;
        if (backing) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 170));
            painter.drawEllipse(QRectF(x - 12, 28, 24, 24));
        }
        const bool current = i == state.pageIndex;
        painter.setPen(QPen(current ? theme.accent : theme.dim, 2));
        painter.setBrush(current ? QBrush(theme.accent) : Qt::NoBrush);
        painter.drawEllipse(QRectF(x - 9, 31, 18, 18));
    }
}

QImage renderStatKey(const KeySpec &spec, const Theme &theme, bool pressed)
{
    QColor bg = theme.colour(spec.bg.isEmpty() ? QStringLiteral("key_bg") : spec.bg);
    QColor fg = theme.colour(spec.fg.isEmpty() ? QStringLiteral("fg") : spec.fg);
    const QColor dim = theme.dim;
    const QColor accent = theme.accent;
    if (pressed)
        std::swap(bg, fg);
    QImage image(KeyPx, KeyPx, QImage::Format_RGB32);
    image.fill(bg);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const int width = std::max(1, theme.borderWidth);
    const QRectF box(width, width, KeyPx - 1.0 - 2.0 * width, KeyPx - 1.0 - 2.0 * width);
    drawFrame(painter, box, theme, theme.colour(spec.border.isEmpty() ? QStringLiteral("border") : spec.border), width, theme.tick);

    const StatReading info = statReading(spec.stat);
    const bool hasBar = info.bar >= 0;
    const int labelY = hasBar ? 30 : 46;
    const int valueY = hasBar ? 74 : 88;
    const int subY = hasBar ? 108 : 122;
    if (!spec.label.isEmpty())
        drawMiddle(painter, QPointF(KeyPx / 2.0, labelY), spec.label, fitFont(theme.font, spec.label, 20, KeyPx - 24), dim, HAlign::Center);
    const QString value = info.value.isEmpty() ? QStringLiteral("—") : info.value;
    drawMiddle(painter, QPointF(KeyPx / 2.0, valueY), value, fitFont(theme.font, value, 48, KeyPx - 28), fg, HAlign::Center);
    if (!info.sub.isEmpty())
        drawMiddle(painter, QPointF(KeyPx / 2.0, subY), info.sub, fitFont(theme.font, info.sub, 16, KeyPx - 20), dim, HAlign::Center);
    if (hasBar) {
        const int x0 = 18;
        const int x1 = KeyPx - 18;
        const int y0 = 128;
        const int y1 = 146;
        const int radius = theme.keyStyle == QLatin1String("hud") ? 0 : 9;
        painter.setPen(QPen(dim, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(x0, y0, x1 - x0, y1 - y0), radius, radius);
        const int fill = static_cast<int>((x1 - x0 - 6) * std::clamp(info.bar, 0.0, 1.0));
        if (fill > 0) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(accent);
            painter.drawRoundedRect(QRectF(x0 + 3, y0 + 3, fill, y1 - y0 - 6), radius == 0 ? 0 : radius * 2 / 3, radius == 0 ? 0 : radius * 2 / 3);
        }
    }
    return image;
}

int readHex(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    bool ok = false;
    const int value = QString::fromUtf8(file.readAll()).trimmed().toInt(&ok, 16);
    return ok ? value : -1;
}

int readNumber(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    bool ok = false;
    const int value = QString::fromUtf8(file.readAll()).trimmed().toInt(&ok, 10);
    return ok ? value : -1;
}

double readDouble(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    bool ok = false;
    const double value = QString::fromUtf8(file.readAll()).trimmed().toDouble(&ok);
    return ok ? value : -1;
}

// Host poll rate for this keyboard's boot interface, from its USB endpoint.
// Same walk as Python `_keyboard_hz`: decimal interface class, hex endpoint fields.
int keyboardHz(const QString &dev)
{
    QString node = QFileInfo(QStringLiteral("/sys/class/input/%1/device").arg(dev)).canonicalFilePath();
    if (node.isEmpty())
        return 0;
    QString usb;
    for (int i = 0; i < 8; ++i) {
        if (QFileInfo::exists(node + QStringLiteral("/speed")) && QFileInfo::exists(node + QStringLiteral("/idVendor"))) {
            usb = node;
            break;
        }
        const QString parent = QFileInfo(node).dir().absolutePath();
        if (parent == node || parent.isEmpty())
            break;
        node = parent;
    }
    if (usb.isEmpty())
        return 0;
    const double speed = readDouble(usb + QStringLiteral("/speed"));
    if (speed < 0)
        return 0;
    const QDir usbDir(usb);
    const auto children = usbDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System);
    for (const QString &name : children) {
        if (!name.contains(QLatin1Char(':')))
            continue;
        const QString iface = usbDir.absoluteFilePath(name);
        if (readNumber(iface + QStringLiteral("/bInterfaceClass")) != 3
            || readNumber(iface + QStringLiteral("/bInterfaceProtocol")) != 1)
            continue;
        const QDir ifaceDir(iface);
        const auto endpoints = ifaceDir.entryList({QStringLiteral("ep_*")}, QDir::Dirs | QDir::System);
        for (const QString &endpoint : endpoints) {
            const QString ep = ifaceDir.absoluteFilePath(endpoint);
            const int addr = readHex(ep + QStringLiteral("/bEndpointAddress"));
            const int interval = readHex(ep + QStringLiteral("/bInterval"));
            if (addr < 0 || interval <= 0 || (addr & 0x80) == 0)
                continue;
            // High speed and above count 125 µs microframes. Full speed counts milliseconds.
            if (speed >= 400.0)
                return static_cast<int>(8000.0 / std::pow(2.0, interval - 1));
            return static_cast<int>(1000.0 / interval);
        }
    }
    return 0;
}

void drawMiddle(QPainter &painter, const QPointF &at, const QString &text, const QFont &font, const QColor &colour, HAlign align)
{
    painter.setFont(font);
    painter.setPen(colour);
    const QFontMetricsF metrics(font);
    const qreal baseline = at.y() + (metrics.ascent() - metrics.descent()) / 2.0;
    qreal x = at.x();
    if (align == HAlign::Center)
        x -= metrics.horizontalAdvance(text) / 2.0;
    else if (align == HAlign::Right)
        x -= metrics.horizontalAdvance(text);
    painter.drawText(QPointF(x, baseline), text);
}

// Same wrap as render_lcd: words, six lines, width LCD_W - 60.
QStringList wrapError(const QString &error, const QFont &font)
{
    QStringList lines;
    QString line;
    const QFontMetricsF metrics(font);
    const qreal limit = LcdW - 60;
    const QString flat = error.simplified();
    const QStringList words = flat.isEmpty() ? QStringList{} : flat.split(QLatin1Char(' '));
    for (const QString &word : words) {
        const QString trial = line + QLatin1Char(' ') + word;
        if (metrics.horizontalAdvance(trial) > limit) {
            lines.append(line);
            line = word;
        } else {
            line = trial.trimmed();
        }
    }
    lines.append(line);
    if (lines.size() > 6)
        lines.resize(6);
    return lines;
}

// Lock chips and the poll rate, right-aligned just left of the page dots.
// Returns the title's right edge, the same value as Python `_draw_keyboard_status`.
int drawKeyboardStatus(QPainter &painter, const LcdState &state, const Theme &theme)
{
    const int n = state.pageCount;
    const int dotsLeft = n > 0 ? LcdW - 36 - (std::max(n, 1) - 1) * 30 - 9 : LcdW - 28;
    if (!state.hasLocks && state.pollHz <= 0)
        return dotsLeft - 16;

    const QString iconFamily = theme.iconFont.isEmpty() ? QStringLiteral("Symbols Nerd Font") : theme.iconFont;
    const QFont chipFont = makeFont(iconFamily, 16);
    const QString icons[] = {glyph(0xF0A9B), glyph(0xF03A0), glyph(0xF0E7A)};
    const bool lit[] = {state.caps, state.num, state.scroll};
    int pieces[3] = {};
    const int pieceCount = state.hasLocks ? 3 : 0;
    if (state.hasLocks) {
        const QFontMetrics metrics(chipFont);
        for (int i = 0; i < 3; ++i)
            pieces[i] = std::max(22, metrics.horizontalAdvance(icons[i]) + 10);
    }

    QString hzText;
    if (state.pollHz > 0)
        hzText = QString::number(state.pollHz / 1000.0, 'g', 6) + QStringLiteral(" kHz");
    const QFont hzFont = makeTextFont(theme.font, 16);
    const int hzW = hzText.isEmpty() ? 0 : QFontMetrics(hzFont).horizontalAdvance(hzText);
    constexpr int gap = 6;
    int total = 0;
    for (int i = 0; i < pieceCount; ++i)
        total += pieces[i];
    if (pieceCount > 1)
        total += gap * (pieceCount - 1);
    if (hzW > 0)
        total += (pieceCount > 0 ? 8 : 0) + hzW;

    int x = dotsLeft - 14 - total;
    constexpr int y = 40;
    const qreal radius = theme.keyStyle == QLatin1String("hud") ? 0 : 5;
    for (int i = 0; i < pieceCount; ++i) {
        const int width = pieces[i];
        const QRectF box(x, y - 10, width, 20);
        if (lit[i]) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(theme.accent);
            painter.drawRoundedRect(box, radius, radius);
            drawMiddle(painter, box.center(), icons[i], chipFont, theme.bg, HAlign::Center);
        } else {
            painter.setPen(QPen(theme.dim, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(box, radius, radius);
            drawMiddle(painter, box.center(), icons[i], chipFont, theme.dim, HAlign::Center);
        }
        x += width + gap;
    }
    if (!hzText.isEmpty()) {
        if (pieceCount > 0)
            x += 2;
        drawMiddle(painter, QPointF(x, y), hzText, hzFont, theme.fg, HAlign::Left);
    }
    return dotsLeft - 14 - total - 12;
}

} // namespace

QFont textFont(const QString &spec, int pixelSize)
{
    return makeTextFont(spec, pixelSize, FontRole::General);
}

QFont fixedTextFont(const QString &spec, int pixelSize)
{
    return makeTextFont(spec, pixelSize, FontRole::Fixed);
}

QFont fitTextFont(const QString &spec, const QString &text, int pixelSize, int maxWidth)
{
    return fitFont(spec, text, pixelSize, maxWidth);
}

void invalidateTextFonts()
{
    fontCache().fresh = false;
}

double unixSeconds()
{
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

int imageFrameIndex(const QString &path, double when)
{
    const AnimEntry entry = preparedAnimation(path);
    if (!entry.animated)
        return -1;
    if (when < 0)
        return 0;
    return frameIndexAt(entry.durations, when);
}

QByteArray encodeJpeg(const QImage &image, int quality)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.convertToFormat(QImage::Format_RGB32).save(&buffer, "JPEG", quality);
    return bytes;
}

QImage renderKey(const KeySpec &spec, const Theme &theme, bool pressed, double when)
{
    if (!spec.stat.isEmpty())
        return renderStatKey(spec, theme, pressed);
    if (!spec.folder.isEmpty() && spec.image.isEmpty() && spec.icon.isEmpty()) {
        QImage image = blankKey(theme);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        const int width = theme.borderWidth;
        const QRectF box(width, width, KeyPx - 2.0 * width, KeyPx - 2.0 * width);
        QColor bg = theme.colour(spec.bg.isEmpty() ? QStringLiteral("key_bg") : spec.bg);
        QColor fg = theme.colour(spec.fg.isEmpty() ? QStringLiteral("fg") : spec.fg);
        if (pressed)
            std::swap(bg, fg);
        painter.fillRect(image.rect(), bg);
        drawFrame(painter, box, theme, theme.colour(spec.border.isEmpty() ? QStringLiteral("border") : spec.border), width, theme.tick);
        const bool labeled = !spec.label.isEmpty();
        const int gap = 3;
        const int left = width + 7;
        const int top = width + 7;
        const int right = KeyPx - width - 7;
        const int bottom = KeyPx - width - (labeled ? 30 : 7);
        const qreal cellW = (right - left - gap * 2) / 3.0;
        const qreal cellH = (bottom - top - gap * 3) / 4.0;
        for (int i = 0; i < Keys; ++i) {
            const KeySpec child = i < spec.folder.size() ? spec.folder.at(i) : KeySpec{};
            KeySpec shown = child;
            if (!child.folder.isEmpty() && child.image.isEmpty() && child.icon.isEmpty()) {
                shown = KeySpec{};
                shown.icon = glyph(0xF07B);
                shown.label = child.label;
            }
            QImage face = renderKey(shown, theme, false);
            const int tw = std::max(1, static_cast<int>(cellW));
            const int th = std::max(1, static_cast<int>(cellH));
            face = face.scaled(tw, th, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            const int x = static_cast<int>(left + (i % 3) * (cellW + gap));
            const int y = static_cast<int>(top + (i / 3) * (cellH + gap));
            painter.drawImage(x, y, face);
        }
        if (labeled)
            drawMiddle(painter, QPointF(KeyPx / 2.0, KeyPx - width - 16), spec.label, fitFont(theme.font, spec.label, 22, KeyPx - 16), fg, HAlign::Center);
        painter.end();
        if (pressed)
            darken(image);
        return image;
    }

    QColor bg = theme.colour(spec.bg.isEmpty() ? QStringLiteral("key_bg") : spec.bg);
    QColor fg = theme.colour(spec.fg.isEmpty() ? QStringLiteral("fg") : spec.fg);
    if (pressed)
        std::swap(bg, fg);

    if (spec.fill && !spec.image.isEmpty()) {
        const QImage picture = keyPicture(configPath(Config::configDir(), spec.image), when);
        if (!picture.isNull()) {
            QImage image = fitCover(picture, QSize(KeyPx, KeyPx)).convertToFormat(QImage::Format_RGB32);
            if (!spec.label.isEmpty()) {
                QPainter painter(&image);
                painter.setRenderHint(QPainter::TextAntialiasing);
                const QFont font = fitFont(theme.font, spec.label, 26, KeyPx - 16);
                drawMiddle(painter, QPointF(KeyPx / 2.0 + 1, KeyPx - 21), spec.label, font, Qt::black, HAlign::Center);
                drawMiddle(painter, QPointF(KeyPx / 2.0, KeyPx - 22), spec.label, font, fg, HAlign::Center);
            }
            if (pressed)
                darken(image);
            return image;
        }
    }

    QImage image(KeyPx, KeyPx, QImage::Format_RGB32);
    image.fill(bg);
    if (spec.isEmpty() && spec.icon.isEmpty())
        return image;

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const int width = std::max(1, theme.borderWidth);
    const QRectF box(width, width, KeyPx - 1.0 - 2.0 * width, KeyPx - 1.0 - 2.0 * width);
    drawFrame(painter, box, theme, theme.colour(spec.border.isEmpty() ? QStringLiteral("border") : spec.border), width, theme.tick);

    const bool labeled = !spec.label.isEmpty();
    bool drewPicture = false;
    if (!spec.image.isEmpty()) {
        const QImage picture = keyPicture(configPath(Config::configDir(), spec.image), when);
        if (!picture.isNull()) {
            const int edge = labeled ? 96 : 124;
            const QImage thumb = picture.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const int x = (KeyPx - thumb.width()) / 2;
            const int y = labeled ? 16 : (KeyPx - thumb.height()) / 2;
            painter.drawImage(x, y, thumb);
            drewPicture = true;
        }
    }
    if (!drewPicture && !spec.icon.isEmpty()) {
        const int size = labeled ? 72 : 96;
        const QFont font = makeFont(theme.iconFont, size);
        const qreal y = labeled ? 62 : KeyPx / 2.0;
        drawMiddle(painter, QPointF(KeyPx / 2.0, y), spec.icon, font, fg, HAlign::Center);
    }
    if (labeled) {
        if (!spec.icon.isEmpty() || drewPicture) {
            const QFont font = fitFont(theme.font, spec.label, 26, KeyPx - 16);
            drawMiddle(painter, QPointF(KeyPx / 2.0, 128), spec.label, font, fg, HAlign::Center);
        } else {
            const int size = spec.label.size() <= 2 ? 72 : 40;
            const QFont font = fitFont(theme.font, spec.label, size, KeyPx - 20);
            drawMiddle(painter, QPointF(KeyPx / 2.0, KeyPx / 2.0), spec.label, font, fg, HAlign::Center);
        }
    }
    return image;
}

QImage renderThemeKey(const QString &name, const Theme &theme, bool current)
{
    QImage image(KeyPx, KeyPx, QImage::Format_RGB32);
    image.fill(theme.keyBg);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const int width = theme.borderWidth + (current ? 2 : 0);
    const QRectF box(width, width, KeyPx - 1.0 - 2.0 * width, KeyPx - 1.0 - 2.0 * width);
    drawFrame(painter, box, theme, current ? theme.accent : theme.border, width, theme.tick);
    const QColor swatches[] = {theme.fg, theme.accent, theme.dim};
    for (int i = 0; i < 3; ++i) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(swatches[i]);
        painter.drawEllipse(QPointF(46 + i * 34, 46), 12, 12);
    }
    const QString title = name.toUpper();
    drawMiddle(painter, QPointF(KeyPx / 2.0, 100), title, fitFont(theme.font, title, 28, KeyPx - 20), theme.fg, HAlign::Center);
    if (current)
        drawMiddle(painter, QPointF(KeyPx / 2.0, 132), QStringLiteral("ACTIVE"), makeTextFont(theme.font, 16), theme.accent, HAlign::Center);
    return image;
}

LcdState keyboardStatus()
{
    // The physical keyboard wins over the deck, a YubiKey, or a virtual device.
    // LEDs live in /sys/class/leds/inputN::capslock, not under inputN/leds.
    LcdState state;
    const QDir ledsRoot(QStringLiteral("/sys/class/leds"));
    if (!ledsRoot.exists())
        return state;

    struct Group {
        QString caps;
        QString num;
        QString scroll;
    };
    QHash<QString, Group> groups;
    const auto nodes = ledsRoot.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System);
    for (const QString &node : nodes) {
        const int sep = node.indexOf(QStringLiteral("::"));
        if (sep <= 0)
            continue;
        const QString kind = node.mid(sep + 2);
        if (kind != QLatin1String("capslock") && kind != QLatin1String("numlock") && kind != QLatin1String("scrolllock"))
            continue;
        Group &group = groups[node.left(sep)];
        const QString brightness = ledsRoot.absoluteFilePath(node) + QStringLiteral("/brightness");
        if (kind == QLatin1String("capslock"))
            group.caps = brightness;
        else if (kind == QLatin1String("numlock"))
            group.num = brightness;
        else
            group.scroll = brightness;
    }

    int bestScore = 0;
    QString bestName;
    QString bestDev;
    Group best;
    bool found = false;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        const Group &group = it.value();
        if (group.caps.isEmpty() || group.num.isEmpty() || group.scroll.isEmpty())
            continue;
        QString kname;
        QFile nameFile(QStringLiteral("/sys/class/input/%1/name").arg(it.key()));
        if (nameFile.open(QIODevice::ReadOnly))
            kname = QString::fromUtf8(nameFile.readAll()).trimmed();
        QFileInfo deviceInfo(QStringLiteral("/sys/class/input/%1/device").arg(it.key()));
        QString real = deviceInfo.canonicalFilePath();
        if (real.isEmpty())
            real = deviceInfo.absoluteFilePath();
        const QString lower = kname.toLower();
        int score = 0;
        if (!real.contains(QStringLiteral("/virtual/")) && !lower.startsWith(QLatin1String("ckb")))
            score += 2;
        if (lower.contains(QLatin1String("keyboard")))
            score += 4;
        if (lower.contains(QLatin1String("yubi")))
            score -= 3;
        if (!found || score > bestScore || (score == bestScore && kname > bestName)) {
            found = true;
            bestScore = score;
            bestName = kname;
            bestDev = it.key();
            best = group;
        }
    }
    if (!found)
        return state;

    auto lit = [](const QString &path) { return readNumber(path) > 0; };
    state.caps = lit(best.caps);
    state.num = lit(best.num);
    state.scroll = lit(best.scroll);
    state.hasLocks = true;
    state.pollHz = keyboardHz(bestDev);
    return state;
}

QImage renderLcd(const LcdState &state, const Theme &theme)
{
    if (!state.screen.isEmpty() && state.error.isEmpty()) {
        const QImage picture = loadImage(configPath(Config::configDir(), state.screen));
        if (!picture.isNull()) {
            QImage image = fitCover(picture, QSize(LcdW, LcdH)).convertToFormat(QImage::Format_RGB32);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            drawPageDotsColoured(painter, state, theme, true);
            return image;
        }
    }

    QImage image(LcdW, LcdH, QImage::Format_RGB32);
    image.fill(theme.bg);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    painter.setPen(QPen(theme.frame, 3));
    painter.setBrush(Qt::NoBrush);
    if (theme.keyStyle == QLatin1String("hud")) {
        Theme frameTheme = theme;
        frameTheme.radius = 28;
        drawFrame(painter, QRectF(4, 4, LcdW - 8, LcdH - 8), frameTheme, theme.frame, 3, theme.accent);
    } else {
        painter.drawRect(QRectF(4, 4, LcdW - 8, LcdH - 8));
    }

    const int titleRight = drawKeyboardStatus(painter, state, theme);
    const QString title = (state.profile + QStringLiteral(" / ") + state.page).toUpper();
    const QFont titleFont = fitFont(theme.font, title, 32, std::max(80, titleRight - 28));
    drawMiddle(painter, QPointF(28, 40), title, titleFont, theme.accent, HAlign::Left);

    drawPageDotsColoured(painter, state, theme, false);

    painter.setPen(QPen(theme.dim, 1));
    painter.drawLine(QPointF(24, 70), QPointF(LcdW - 24, 70));

    if (!state.error.isEmpty()) {
        drawMiddle(painter, QPointF(LcdW / 2.0, 130), QStringLiteral("CONFIG ERROR"), makeTextFont(theme.font, 44), theme.accent, HAlign::Center);
        const QFont body = makeTextFont(theme.font, 20);
        const QFontMetricsF metrics(body);
        // Pillow's multiline spacing is the "A" box plus 6px, hung from the ascender.
        const qreal step = metrics.ascent() + 6;
        qreal y = 180;
        painter.setFont(body);
        painter.setPen(theme.fg);
        for (const QString &row : wrapError(state.error, body)) {
            if (!row.isEmpty())
                painter.drawText(QPointF(28, y + metrics.ascent()), row);
            y += step;
        }
        return image;
    }

    const int rows = (state.title.isEmpty() ? 0 : 1) + (state.artist.isEmpty() ? 0 : 1);
    QImage cover;
    if (!state.artPath.isEmpty()) {
        const QImage art = loadImage(state.artPath);
        if (!art.isNull())
            cover = fitCover(art, QSize(236, 236)).convertToFormat(QImage::Format_RGB32);
    }
    int clockY = 150;
    int clockPx = 110;
    if (rows > 0 || !cover.isNull()) {
        const int index = std::min(rows, 3);
        if (cover.isNull()) {
            static const int ys[] = {150, 138, 124, 112};
            static const int sizes[] = {110, 100, 86, 72};
            clockY = ys[index];
            clockPx = sizes[index];
        } else {
            static const int ys[] = {158, 140, 126, 114};
            static const int sizes[] = {100, 96, 86, 70};
            clockY = ys[index];
            clockPx = sizes[index];
        }
    }
    qreal centerX = LcdW / 2.0;
    int maxWidth = LcdW - 60;
    int rowX = 56;
    int volRight = LcdW - 40;
    int volBar = LcdW - 130;
    if (!cover.isNull()) {
        const int artX = LcdW - 28 - cover.width();
        const int artY = 84;
        painter.drawImage(artX, artY, cover);
        painter.setPen(QPen(theme.dim, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRect(artX, artY, cover.width() - 1, cover.height() - 1));
        const int left = artX - 16;
        maxWidth = std::max(40, left - 28);
        centerX = (28.0 + left) / 2.0;
        rowX = 36;
        volRight = artX - 16;
        volBar = artX - 88;
    }
    QFont clockFont = makeTextFont(theme.clockFont, clockPx);
    while (clockPx > 40 && QFontMetrics(clockFont).horizontalAdvance(state.clock) > maxWidth) {
        clockPx -= 4;
        clockFont = makeTextFont(theme.clockFont, clockPx);
    }
    drawMiddle(painter, QPointF(centerX, clockY), state.clock, clockFont, theme.fg, HAlign::Center);
    const int dateY = clockY + clockPx / 2 + (cover.isNull() ? 14 : 12);
    const QFont dateFont = fitFont(theme.font, state.date, cover.isNull() ? 30 : 26, maxWidth);
    drawMiddle(painter, QPointF(centerX, dateY), state.date, dateFont, theme.dim, HAlign::Center);
    if (rows > 0) {
        const int gap = 38;
        int y = std::min(dateY + 36, 276 - (rows - 1) * gap);
        auto row = [&](const QString &icon, const QString &text) {
            int x = rowX;
            if (!icon.isEmpty()) {
                drawMiddle(painter, QPointF(x, y), icon, makeFont(theme.iconFont, 30), theme.accent, HAlign::Left);
                x += 36;
            }
            const QFont font = fitFont(theme.font, text, 22, std::max(40, maxWidth - (x - rowX) - 8));
            drawMiddle(painter, QPointF(x, y), text, font, theme.fg, HAlign::Left);
            y += gap;
        };
        if (!state.title.isEmpty())
            row(state.playing ? glyph(0xF040A) : glyph(0xF03E4), state.title);
        if (!state.artist.isEmpty())
            row(glyph(0xF0370), state.artist);
    }
    if (state.position >= 0) {
        const int y = 312;
        const int radius = theme.keyStyle == QLatin1String("hud") ? 0 : 9;
        auto clockText = [](int seconds) {
            seconds = std::max(0, seconds);
            const int hours = seconds / 3600;
            const int minutes = (seconds % 3600) / 60;
            const int secs = seconds % 60;
            if (hours > 0)
                return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0'));
            return QStringLiteral("%1:%2").arg(minutes).arg(secs, 2, 10, QLatin1Char('0'));
        };
        const QFont timeFont = makeTextFont(theme.font, 26);
        drawMiddle(painter, QPointF(96 - 8, y), clockText(state.position), timeFont, theme.accent, HAlign::Right);
        if (state.length > 0)
            drawMiddle(painter, QPointF(volRight, y), clockText(state.length), timeFont, theme.fg, HAlign::Right);
        painter.setPen(QPen(theme.dim, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(96, y - 9, volBar - 96, 18), radius, radius);
        if (state.length > 0) {
            const int fill = static_cast<int>((volBar - 96 - 6) * std::clamp(state.position / static_cast<double>(state.length), 0.0, 1.0));
            if (fill > 0) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(theme.accent);
                painter.drawRoundedRect(QRectF(99, y - 6, fill, 12), radius * 2 / 3, radius * 2 / 3);
            }
        }
    }

    if (state.volume >= 0) {
        const int y = 340;
        const int x0 = 96;
        const int x1 = volBar;
        const int radius = theme.keyStyle == QLatin1String("hud") ? 0 : 9;
        const QString icon = state.muted ? glyph(0xF075F) : glyph(0xF057E);
        drawMiddle(painter, QPointF(56, y), icon, makeFont(theme.iconFont, 30), state.muted ? theme.dim : theme.accent, HAlign::Left);
        painter.setPen(QPen(theme.dim, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(x0, y - 9, x1 - x0, 18), radius, radius);
        const int fill = static_cast<int>((x1 - x0 - 6) * std::min(state.volume, 100) / 100.0);
        if (fill > 0) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(state.muted ? theme.dim : theme.fg);
            painter.drawRoundedRect(QRectF(x0 + 3, y - 6, fill, 12), radius * 2 / 3, radius * 2 / 3);
        }
        const QString volumeText = state.muted ? QStringLiteral("MUTE") : QStringLiteral("%1%").arg(state.volume);
        drawMiddle(painter, QPointF(volRight, y), volumeText, makeTextFont(theme.font, 26), state.muted ? theme.dim : theme.fg, HAlign::Right);
    }
    return image;
}

QStringList themeNames(const Config &config)
{
    const QStringList preferred{QStringLiteral("amber"), QStringLiteral("cyberpunk"), QStringLiteral("synthwave"),
                                QStringLiteral("matrix"), QStringLiteral("arctic"), QStringLiteral("hud"), QStringLiteral("mono")};
    QStringList names;
    for (const QString &name : preferred) {
        if (config.themes.contains(name))
            names.append(name);
    }
    QStringList rest;
    for (auto it = config.themes.cbegin(); it != config.themes.cend(); ++it) {
        if (!names.contains(it.key()))
            rest.append(it.key());
    }
    rest.sort();
    names += rest;
    return names;
}

QImage renderDeckKey(const KeySpec &spec, const Theme &pageTheme, const QHash<QString, Theme> &themes,
                     const QString &activeTheme, bool pressed, double when)
{
    if (spec.wallpaperTheme && !themes.contains(QStringLiteral("wallpaper"))) {
        KeySpec face;
        face.icon = QString::fromUcs4(U"\U000F0E09");
        face.label = QStringLiteral("wallpaper");
        return renderKey(face, pageTheme, pressed, when);
    }
    QString themeName = spec.setTheme;
    if (spec.wallpaperTheme)
        themeName = QStringLiteral("wallpaper");
    if (!themeName.isEmpty()) {
        QImage image = renderThemeKey(themeName, themes.value(themeName, pageTheme), themeName == activeTheme);
        if (pressed)
            image.invertPixels(QImage::InvertRgb);
        return image;
    }
    return renderKey(spec, pageTheme, pressed, when);
}

QList<KeySpec> settingsKeys(const Config &config, const QString &activeProfile)
{
    QList<KeySpec> keys;
    // Wallpaper, the two brightness keys, and up to three profile keys stay on the page.
    const int reserved = 3 + std::min(static_cast<int>(config.profiles.size()), 3);
    const int slots = std::max(0, Keys - reserved);
    for (const QString &name : themeNames(config)) {
        if (keys.size() >= slots)
            break;
        if (name == QLatin1String("wallpaper"))
            continue;
        KeySpec key;
        key.setTheme = name;
        keys.append(key);
    }
    KeySpec wallpaper;
    wallpaper.wallpaperTheme = true;
    keys.append(wallpaper);
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
    for (const Profile &profile : config.profiles) {
        if (keys.size() >= Keys)
            break;
        KeySpec key;
        key.icon = profile.name == activeProfile ? glyph(0xF0009) : glyph(0xF0004);
        key.label = profile.name;
        key.profile = profile.name;
        if (profile.name == activeProfile)
            key.fg = QStringLiteral("accent");
        keys.append(key);
    }
    while (keys.size() < Keys)
        keys.append(KeySpec{});
    return keys;
}

} // namespace kdeck
