#include "Render.hpp"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace galleon {
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

QFont fitFont(const QString &spec, const QString &text, int pixelSize, int maxWidth)
{
    QFont font = makeFont(spec, pixelSize);
    while (pixelSize > 10 && QFontMetrics(font).horizontalAdvance(text) > maxWidth) {
        pixelSize -= 2;
        font = makeFont(spec, pixelSize);
    }
    return font;
}

void drawCentered(QPainter &painter, const QPointF &center, const QString &text, const QFont &font, const QColor &colour)
{
    painter.setFont(font);
    painter.setPen(colour);
    const QFontMetricsF metrics(font);
    QRectF box = metrics.boundingRect(text);
    box.moveCenter(center);
    painter.drawText(box, Qt::AlignCenter, text);
}

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

void drawPageDotsColoured(QPainter &painter, const LcdState &state, const Theme &theme)
{
    for (int i = 0; i < state.pageCount; ++i) {
        const int x = LcdW - 36 - (state.pageCount - 1 - i) * 30;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 170));
        painter.drawEllipse(QPoint(x, 40), 12, 12);
        painter.setBrush(i == state.pageIndex ? theme.accent : theme.dim);
        painter.drawEllipse(QPoint(x, 40), 9, 9);
    }
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

} // namespace

QByteArray encodeJpeg(const QImage &image, int quality)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.convertToFormat(QImage::Format_RGB32).save(&buffer, "JPEG", quality);
    return bytes;
}

QImage renderKey(const KeySpec &spec, const Theme &theme, bool pressed)
{
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
            drawCentered(painter, QPointF(KeyPx / 2.0, KeyPx - width - 16), spec.label, fitFont(theme.font, spec.label, 22, KeyPx - 16), fg);
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
        const QImage picture = loadImage(configPath(Config::configDir(), spec.image));
        if (!picture.isNull()) {
            QImage image = fitCover(picture, QSize(KeyPx, KeyPx)).convertToFormat(QImage::Format_RGB32);
            if (!spec.label.isEmpty()) {
                QPainter painter(&image);
                painter.setRenderHint(QPainter::TextAntialiasing);
                const QFont font = fitFont(theme.font, spec.label, 26, KeyPx - 16);
                drawCentered(painter, QPointF(KeyPx / 2.0 + 1, KeyPx - 21), spec.label, font, Qt::black);
                drawCentered(painter, QPointF(KeyPx / 2.0, KeyPx - 22), spec.label, font, fg);
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
        const QImage picture = loadImage(configPath(Config::configDir(), spec.image));
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
        drawCentered(painter, QPointF(KeyPx / 2.0, y), spec.icon, font, fg);
    }
    if (labeled) {
        if (!spec.icon.isEmpty() || drewPicture) {
            const QFont font = fitFont(theme.font, spec.label, 26, KeyPx - 16);
            drawCentered(painter, QPointF(KeyPx / 2.0, 128), spec.label, font, fg);
        } else {
            const int size = spec.label.size() <= 2 ? 72 : 40;
            const QFont font = fitFont(theme.font, spec.label, size, KeyPx - 20);
            drawCentered(painter, QPointF(KeyPx / 2.0, KeyPx / 2.0), spec.label, font, fg);
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
    drawCentered(painter, QPointF(KeyPx / 2.0, 100), title, fitFont(theme.font, title, 28, KeyPx - 20), theme.fg);
    if (current)
        drawCentered(painter, QPointF(KeyPx / 2.0, 132), QStringLiteral("ACTIVE"), makeFont(theme.font, 16), theme.accent);
    return image;
}

LcdState keyboardStatus()
{
    LcdState state;
    QDir hid(QStringLiteral("/sys/class/hidraw"));
    const QString deckId = QStringLiteral("%1:%2").arg(QLatin1String(DeckVid), QLatin1String(DeckPid));
    QString bestPath;
    int bestRank = 100;
    int bestHz = 0;
    const auto nodes = hid.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &node : nodes) {
        const QString device = QFileInfo(hid.absoluteFilePath(node) + QStringLiteral("/device")).canonicalFilePath();
        QFile uevent(device + QStringLiteral("/uevent"));
        if (!uevent.open(QIODevice::ReadOnly))
            continue;
        const QString text = QString::fromUtf8(uevent.readAll());
        if (!text.contains(QStringLiteral("0003:")))
            continue;
        const bool deck = text.contains(deckId);
        const bool yubikey = text.contains(QStringLiteral("Yubico"), Qt::CaseInsensitive);
        int rank = 2;
        if (deck || yubikey)
            rank = 3;
        if (text.contains(QStringLiteral("usb")) && !deck)
            rank = 1;
        const QDir inputDir(device);
        const auto inputs = inputDir.entryList({QStringLiteral("input*")});
        // Walk one level up to the USB device for speed and LEDs when present.
        QDir usb(device);
        usb.cdUp();
        const int speed = readNumber(usb.absoluteFilePath(QStringLiteral("speed")));
        int hz = 0;
        const auto children = usb.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &child : children) {
            if (!child.contains(QLatin1Char(':')))
                continue;
            const QString iface = usb.absoluteFilePath(child);
            const int protocol = readHex(iface + QStringLiteral("/bInterfaceProtocol"));
            const int cls = readHex(iface + QStringLiteral("/bInterfaceClass"));
            if (cls == 3 && protocol == 1) {
                const int interval = readHex(iface + QStringLiteral("/bInterval"));
                if (interval > 0)
                    hz = speed >= 480 ? 8000 / (1 << (interval - 1)) : 1000 / interval;
            }
        }
        Q_UNUSED(inputs);
        if (rank < bestRank || (rank == bestRank && hz > bestHz)) {
            bestRank = rank;
            bestHz = hz;
            bestPath = device;
        }
    }

    auto led = [](const QString &root, const QString &name) {
        const QDir leds(root + QStringLiteral("/leds"));
        if (!leds.exists())
            return false;
        const auto entries = leds.entryList({QStringLiteral("*::%1").arg(name)});
        for (const QString &entry : entries) {
            if (readNumber(leds.absoluteFilePath(entry) + QStringLiteral("/brightness")) > 0)
                return true;
        }
        return false;
    };

    if (!bestPath.isEmpty()) {
        const QDir input(bestPath);
        const auto inputs = input.entryList({QStringLiteral("input*")}, QDir::Dirs);
        QString inputPath;
        for (const QString &name : inputs) {
            inputPath = input.absoluteFilePath(name);
            break;
        }
        if (!inputPath.isEmpty()) {
            state.caps = led(inputPath, QStringLiteral("capslock"));
            state.num = led(inputPath, QStringLiteral("numlock"));
            state.scroll = led(inputPath, QStringLiteral("scrolllock"));
            state.hasLocks = true;
        }
        state.pollHz = bestHz;
    }
    return state;
}

QImage renderLcd(const LcdState &state, const Theme &theme)
{
    if (!state.screen.isEmpty()) {
        const QImage picture = loadImage(configPath(Config::configDir(), state.screen));
        if (!picture.isNull()) {
            QImage image = fitCover(picture, QSize(LcdW, LcdH)).convertToFormat(QImage::Format_RGB32);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            drawPageDotsColoured(painter, state, theme);
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

    const int dotsWidth = state.pageCount * 30 + 24;
    const QString title = state.profile + QStringLiteral("  ") + state.page;
    const QFont titleFont = fitFont(theme.font, title, 32, LcdW - dotsWidth - 220);
    painter.setFont(titleFont);
    painter.setPen(theme.fg);
    const QFontMetrics titleMetrics(titleFont);
    painter.drawText(QPoint(28, 40 + titleMetrics.ascent() / 2 - 4), title);

    if (state.hasLocks || state.pollHz > 0) {
        const QString caps = glyph(0xF0A9B);
        const QString num = glyph(0xF03A0);
        const QString scroll = glyph(0xF0E7A);
        const QFont iconFont = makeFont(theme.iconFont, 18);
        const QString rate = state.pollHz >= 1000 ? QStringLiteral("%1 kHz").arg(state.pollHz / 1000) : QString::number(state.pollHz);
        int x = LcdW - dotsWidth - 16;
        auto chip = [&](const QString &text, bool on, const QFont &font) {
            const int w = QFontMetrics(font).horizontalAdvance(text) + 16;
            x -= w + 8;
            const QRectF chipBox(x, 26, w, 28);
            painter.setFont(font);
            if (on) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(theme.accent);
                painter.drawRoundedRect(chipBox, theme.keyStyle == QLatin1String("hud") ? 0 : 5, theme.keyStyle == QLatin1String("hud") ? 0 : 5);
                painter.setPen(theme.bg);
            } else {
                painter.setPen(QPen(theme.dim, 1));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(chipBox, theme.keyStyle == QLatin1String("hud") ? 0 : 5, theme.keyStyle == QLatin1String("hud") ? 0 : 5);
                painter.setPen(theme.dim);
            }
            drawCentered(painter, chipBox.center(), text, font, on ? theme.bg : theme.dim);
        };
        chip(rate, state.pollHz > 0, makeFont(theme.font, 16));
        chip(scroll, state.scroll, iconFont);
        chip(num, state.num, iconFont);
        chip(caps, state.caps, iconFont);
    }

    drawPageDotsColoured(painter, state, theme);

    painter.setPen(QPen(theme.dim, 1));
    painter.drawLine(QPointF(24, 70), QPointF(LcdW - 24, 70));

    if (!state.error.isEmpty()) {
        painter.setPen(theme.fg);
        painter.setFont(makeFont(theme.font, 22));
        painter.drawText(QRect(40, 120, LcdW - 80, 180), Qt::AlignCenter | Qt::TextWordWrap, state.error);
        return image;
    }

    const QFont clockFont = makeFont(theme.clockFont, 110);
    drawCentered(painter, QPointF(LcdW / 2.0, 168), state.clock, clockFont, theme.fg);
    const QFont dateFont = fitFont(theme.font, state.date, 30, LcdW - 60);
    drawCentered(painter, QPointF(LcdW / 2.0, 168 + 55 + 28), state.date, dateFont, theme.dim);

    if (state.volume >= 0) {
        const int y = 340;
        const int x0 = 96;
        const int x1 = LcdW - 130;
        const int radius = theme.keyStyle == QLatin1String("hud") ? 0 : 9;
        const QString icon = state.muted ? glyph(0xF075F) : glyph(0xF057E);
        drawCentered(painter, QPointF(56, y), icon, makeFont(theme.iconFont, 30), state.muted ? theme.dim : theme.accent);
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
        painter.setPen(state.muted ? theme.dim : theme.fg);
        painter.setFont(makeFont(theme.font, 26));
        painter.drawText(QRect(x1, y - 18, LcdW - 40 - x1, 36), Qt::AlignVCenter | Qt::AlignRight, volumeText);
    }
    return image;
}

} // namespace galleon
