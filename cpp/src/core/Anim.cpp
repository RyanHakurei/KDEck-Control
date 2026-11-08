#include "Anim.hpp"
#include "Render.hpp"

#include <QFont>
#include <QFontMetrics>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

namespace galleon {
namespace {

constexpr double BootFps = 30.0;
constexpr char Cipher[] = "0123456789ABCDEF#$%&*+=/<>?!";

int stretched(double stretch, int count)
{
    return std::max(1, static_cast<int>(std::lround(count * stretch)));
}

QColor mix(const QColor &a, const QColor &b, double t)
{
    const auto channel = [&](int from, int to) { return qRound(from + (to - from) * t); };
    return QColor(channel(a.red(), b.red()), channel(a.green(), b.green()), channel(a.blue(), b.blue()));
}

// Pillow's "m" anchor is the middle of the em box, not the tight glyph bounds.
void drawCentered(QPainter &painter, const QPointF &center, const QString &text, const QFont &font, const QColor &colour)
{
    painter.setFont(font);
    painter.setPen(colour);
    const QFontMetricsF metrics(font);
    const qreal baseline = center.y() + (metrics.ascent() - metrics.descent()) / 2.0;
    const qreal x = center.x() - metrics.horizontalAdvance(text) / 2.0;
    painter.drawText(QPointF(x, baseline), text);
}

void drawLeftMiddle(QPainter &painter, const QPointF &leftMiddle, const QString &text, const QFont &font, const QColor &colour)
{
    painter.setFont(font);
    painter.setPen(colour);
    const QFontMetricsF metrics(font);
    const qreal baseline = leftMiddle.y() + (metrics.ascent() - metrics.descent()) / 2.0;
    painter.drawText(QPointF(leftMiddle.x(), baseline), text);
}

void drawFrame(QPainter &painter, const QRectF &box, const Theme &theme, const QColor &outline, int width)
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
    painter.setPen(QPen(theme.tick, width + 1));
    const qreal arm = std::max<qreal>(12, box.width() / 7);
    painter.drawLine(QPointF(box.right() - arm, box.top()), QPointF(box.right(), box.top()));
    painter.drawLine(QPointF(box.right(), box.top()), QPointF(box.right(), box.top() + arm));
    painter.drawLine(QPointF(box.left(), box.bottom() - arm), QPointF(box.left(), box.bottom()));
    painter.drawLine(QPointF(box.left(), box.bottom()), QPointF(box.left() + arm, box.bottom()));
}

QImage scanlines()
{
    QImage overlay(LcdW, LcdH, QImage::Format_ARGB32_Premultiplied);
    overlay.fill(Qt::transparent);
    QPainter painter(&overlay);
    painter.setPen(QColor(0, 0, 0, 70));
    for (int y = 0; y < LcdH; y += 3)
        painter.drawLine(0, y, LcdW, y);
    return overlay;
}

QImage finishFrame(const QImage &image, const QImage &overlay)
{
    QImage out = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&out);
    painter.drawImage(0, 0, overlay);
    return out.convertToFormat(QImage::Format_RGB32);
}

QRect contentBounds(const QImage &image)
{
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    int minX = argb.width();
    int minY = argb.height();
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < argb.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            if (qAlpha(line[x]) == 0)
                continue;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    if (maxX < 0)
        return image.rect();
    return QRect(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

QImage fitCover(const QImage &source, const QSize &size)
{
    const QImage scaled = source.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = std::max(0, (scaled.width() - size.width()) / 2);
    const int y = std::max(0, (scaled.height() - size.height()) / 2);
    return scaled.copy(x, y, size.width(), size.height());
}

int sampleChannel(const QImage &image, int x, int y, int shift, int (*channel)(QRgb))
{
    const int from = x + shift;
    if (from < 0 || from >= image.width())
        return 0;
    return channel(reinterpret_cast<const QRgb *>(image.constScanLine(y))[from]);
}

QImage shiftBands(const QImage &image, QRandomGenerator &rng, double amount, int bandLow, int bandHigh, double slide, double chance)
{
    QImage out(image.size(), image.format());
    out.fill(image.hasAlphaChannel() ? Qt::transparent : Qt::black);
    QPainter painter(&out);
    int y = 0;
    while (y < image.height()) {
        const int low = std::max(1, bandLow);
        const int high = std::max(low, bandHigh);
        const int band = rng.bounded(low, high + 1);
        int dx = 0;
        if (rng.generateDouble() < chance)
            dx = static_cast<int>((rng.generateDouble() * 2.0 - 1.0) * slide * amount);
        painter.drawImage(QPoint(dx, y), image, QRect(0, y, image.width(), band));
        y += band;
    }
    return out;
}

QList<int> shuffled(QRandomGenerator &rng, int count)
{
    QList<int> values;
    values.reserve(count);
    for (int i = 0; i < count; ++i)
        values.append(i);
    for (int i = count - 1; i > 0; --i)
        std::swap(values[i], values[rng.bounded(i + 1)]);
    return values;
}

class KeyDecrypt {
public:
    static constexpr int Block = 16;

    KeyDecrypt(const Theme &theme, QRandomGenerator &rng, double stretch, const std::function<QImage(int)> &realKey)
        : m_theme(theme)
        , m_rng(rng)
        , m_realKey(realKey)
        , m_gap(std::max(1, static_cast<int>(std::lround(4 * stretch))))
        , m_length(std::max(2, static_cast<int>(std::lround(12 * stretch))))
        , m_cells((KeyPx / Block) * (KeyPx / Block))
        , m_order(shuffled(rng, Keys))
    {
        for (int i = 0; i < Keys; ++i)
            m_reveal.append(shuffled(rng, m_cells));
    }

    int done() const { return m_done; }

    void arm(int frame) { m_start = frame; }

    void apply(int frame, PlayFrame &out)
    {
        const bool scramble = frame % 3 == 0;
        for (int i = 0; i < Keys; ++i) {
            if (m_finished[i])
                continue;
            const int begin = m_start < 0 ? -1 : m_start + m_order.indexOf(i) * m_gap;
            if (m_start < 0 || frame < begin) {
                if (scramble)
                    send(out, i, cipher());
                continue;
            }
            const int t = frame - begin;
            if (t < m_length) {
                send(out, i, resolving(i, (t + 1.0) / m_length));
            } else if (t == m_length) {
                send(out, i, flashed(m_realKey(i)));
            } else {
                out.realMask = static_cast<quint16>(out.realMask | (1u << i));
                out.sendMask = static_cast<quint16>(out.sendMask | (1u << i));
                m_finished[i] = true;
                ++m_done;
            }
        }
    }

private:
    void send(PlayFrame &out, int index, const QImage &image)
    {
        out.keys[index] = image;
        out.sendMask = static_cast<quint16>(out.sendMask | (1u << index));
    }

    QImage cipher()
    {
        QImage image(KeyPx, KeyPx, QImage::Format_RGB32);
        image.fill(m_theme.keyBg);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        const int width = std::max(1, m_theme.borderWidth);
        const QRectF box(width, width, KeyPx - 1.0 - 2.0 * width, KeyPx - 1.0 - 2.0 * width);
        drawFrame(painter, box, m_theme, m_theme.border, width);
        const QFont font = fixedTextFont(QStringLiteral(":bold"), 26);
        const int alphabet = static_cast<int>(sizeof Cipher) - 1;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                const QChar ch = QLatin1Char(Cipher[m_rng.bounded(alphabet)]);
                const QColor fill = m_rng.generateDouble() < 0.2 ? m_theme.fg : m_theme.dim;
                drawCentered(painter, QPointF(26 + col * 36, 26 + row * 36), ch, font, fill);
            }
        }
        return image;
    }

    QImage resolving(int index, double frac)
    {
        const int shown = std::clamp(static_cast<int>(m_cells * frac), 0, m_cells);
        QImage image = cipher();
        const QImage real = m_realKey(index).convertToFormat(QImage::Format_RGB32);
        const int perRow = KeyPx / Block;
        const int fresh = std::max(1, m_cells / m_length);
        const QList<int> &cells = m_reveal.at(index);
        for (int k = 0; k < shown; ++k) {
            const int cell = cells.at(k);
            const int row = cell / perRow;
            const int col = cell % perRow;
            const QRect box(col * Block, row * Block, Block, Block);
            QPainter painter(&image);
            painter.drawImage(box, real, box);
            if (k < shown - fresh)
                continue;
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(m_theme.accent.red(), m_theme.accent.green(), m_theme.accent.blue(), 150));
            painter.drawRect(box);
        }
        return image;
    }

    static QImage flashed(const QImage &source)
    {
        QImage image = source.convertToFormat(QImage::Format_RGB32);
        for (int y = 0; y < image.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const auto lift = [](int value) { return std::min(255, static_cast<int>(value * 1.7) + 40); };
                line[x] = qRgb(lift(qRed(line[x])), lift(qGreen(line[x])), lift(qBlue(line[x])));
            }
        }
        return image;
    }

    Theme m_theme;
    QRandomGenerator &m_rng;
    std::function<QImage(int)> m_realKey;
    int m_gap = 1;
    int m_length = 2;
    int m_cells = 1;
    int m_start = -1;
    int m_done = 0;
    bool m_finished[Keys] = {};
    QList<int> m_order;
    QList<QList<int>> m_reveal;
};

QImage glitchRgb(const QImage &image, QRandomGenerator &rng, double amount)
{
    if (amount <= 0.02)
        return image;
    const QImage source = image.convertToFormat(QImage::Format_RGB32);
    const int w = source.width();
    const int h = source.height();
    QImage banded = shiftBands(source, rng, amount, std::max(2, h / 20), std::max(3, h / 5), w * 0.25, 0.55);
    banded = banded.convertToFormat(QImage::Format_RGB32);
    const int redShift = -static_cast<int>(w * 0.05 * amount);
    const int blueShift = static_cast<int>(w * 0.03 * amount);
    QImage split(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(banded.constScanLine(y));
        auto *out = reinterpret_cast<QRgb *>(split.scanLine(y));
        for (int x = 0; x < w; ++x) {
            out[x] = qRgb(sampleChannel(banded, x, y, redShift, qRed), qGreen(line[x]), sampleChannel(banded, x, y, blueShift, qBlue));
        }
    }
    if (rng.generateDouble() < 0.3 * amount) {
        for (int y = 0; y < h; ++y) {
            auto *line = reinterpret_cast<QRgb *>(split.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const auto flare = [](int value) { return std::min(255, static_cast<int>(value * 1.5) + 20); };
                line[x] = qRgb(flare(qRed(line[x])), flare(qGreen(line[x])), flare(qBlue(line[x])));
            }
        }
    }
    return split;
}

void scaleAlpha(QImage &image, double factor)
{
    image = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int alpha = std::clamp(static_cast<int>(qAlpha(line[x]) * factor), 0, 255);
            line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), alpha);
        }
    }
}

QImage glitchLogo(const QImage &layer, QRandomGenerator &rng, double amount)
{
    if (amount <= 0.02)
        return layer;
    QImage banded = shiftBands(layer, rng, amount, 6, 40, 60.0, 0.6);
    banded = banded.convertToFormat(QImage::Format_ARGB32);
    const int shift = -static_cast<int>(14 * amount);
    QImage split(banded.size(), QImage::Format_ARGB32);
    for (int y = 0; y < banded.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(banded.constScanLine(y));
        auto *out = reinterpret_cast<QRgb *>(split.scanLine(y));
        for (int x = 0; x < banded.width(); ++x)
            out[x] = qRgba(sampleChannel(banded, x, y, shift, qRed), qGreen(line[x]), qBlue(line[x]), qAlpha(line[x]));
    }
    return split;
}

} // namespace

QImage glitchImage(const QImage &image, double amount)
{
    return glitchRgb(image, *QRandomGenerator::global(), amount);
}

QImage loadDeckScreen(const QString &path)
{
    if (path.isEmpty())
        return {};
    const QImage image(path);
    if (image.isNull())
        return {};
    return fitCover(image, QSize(LcdW, LcdH)).convertToFormat(QImage::Format_RGB32);
}

QImage loadDeckLogo(const QString &path, const Theme &theme, const QString &fallback)
{
    const QSize box(LcdW - 60, LcdH - 80);
    if (!path.isEmpty()) {
        QImage image(path);
        if (!image.isNull()) {
            image = image.convertToFormat(QImage::Format_ARGB32);
            image = image.copy(contentBounds(image));
            image = image.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            return image;
        }
    }
    QImage image(box, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const QString text = fallback.trimmed().isEmpty() ? QStringLiteral("GALLEON DECK") : fallback.trimmed();
    const QFont font = fitTextFont(theme.clockFont, text, 90, image.width() - 20);
    drawCentered(painter, QPointF(image.width() / 2.0, image.height() / 2.0), text, font, theme.fg);
    return image;
}

QImage transitionFrame(const Theme &theme, const QImage &screen, const QImage &logo, double reveal)
{
    QImage base(LcdW, LcdH, QImage::Format_RGB32);
    base.fill(theme.bg);
    QImage layer(LcdW, LcdH, QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    QPainter painter(&layer);
    if (!screen.isNull())
        painter.drawImage(0, 0, screen);
    else if (!logo.isNull())
        painter.drawImage((LcdW - logo.width()) / 2, (LcdH - logo.height()) / 2, logo);
    painter.end();
    scaleAlpha(layer, std::min(1.0, reveal * 1.6));
    QImage out = base.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter compose(&out);
    compose.drawImage(0, 0, layer);
    compose.end();
    out = out.convertToFormat(QImage::Format_RGB32);
    if (!screen.isNull())
        return out;
    QPainter frame(&out);
    frame.setRenderHint(QPainter::Antialiasing);
    if (theme.keyStyle == QLatin1String("hud"))
        drawFrame(frame, QRectF(4, 4, LcdW - 9, LcdH - 9), theme, theme.frame, 3);
    else {
        frame.setPen(QPen(theme.frame, 3));
        frame.setBrush(Qt::NoBrush);
        frame.drawRect(QRectF(4, 4, LcdW - 9, LcdH - 9));
    }
    return out;
}

QImage withPageDots(const QImage &image, int pageIndex, int pageCount, const Theme &theme)
{
    if (pageCount < 1)
        return image;
    QImage out = image.convertToFormat(QImage::Format_RGB32);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < pageCount; ++i) {
        const int x = LcdW - 36 - (pageCount - 1 - i) * 30;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 170));
        painter.drawEllipse(QRectF(x - 12, 28, 24, 24));
        const bool current = i == pageIndex;
        painter.setPen(QPen(current ? theme.accent : theme.dim, 2));
        painter.setBrush(current ? QBrush(theme.accent) : Qt::NoBrush);
        painter.drawEllipse(QRectF(x - 9, 31, 18, 18));
    }
    return out;
}

struct BootSequence::State {
    bool valid = false;
    bool gif = false;
    QImageReader reader;
    Theme theme;
    QString title;
    QString footer;
    QList<QPair<QString, QString>> rows;
    int total = 0;
    QImage logo;
    QImage scan;
    QImage holdBase;
    QRandomGenerator rng{1};
    std::function<QImage(int)> realKey;
    std::unique_ptr<KeyDecrypt> keys;
    double stretch = 1;
    int phase = 0;
    int index = 0;
    int keyFrame = 0;
    int textFrame = 0;
    double shown = 0;
};

BootSequence::BootSequence(const Theme &theme, const QString &title, const QStringList &lines, const QString &logoPath,
                           double speed, const QString &footer, std::function<QImage(int)> realKey)
    : m_state(std::make_unique<State>())
{
    m_state->valid = true;
    m_state->theme = theme;
    m_state->title = title.isEmpty() ? QStringLiteral("GALLEON DECK") : title;
    m_state->footer = footer;
    m_state->stretch = 1.0 / std::max(0.1, speed);
    m_state->scan = scanlines();
    m_state->rng.seed(QRandomGenerator::global()->generate());
    m_state->realKey = std::move(realKey);
    m_state->keys = std::make_unique<KeyDecrypt>(theme, m_state->rng, m_state->stretch, m_state->realKey);
    const QString logoText = m_state->title.section(QStringLiteral("//"), 0, 0).trimmed();
    m_state->logo = loadDeckLogo(logoPath, theme, logoText.isEmpty() ? m_state->title : logoText);
    for (const QString &line : lines.mid(0, 6)) {
        const QString label = line.section(QLatin1Char('|'), 0, 0);
        const QString status = line.section(QLatin1Char('|'), 1);
        const QString typed = QStringLiteral("> %1 ").arg(label).leftJustified(28, QLatin1Char('.')) + QLatin1Char(' ');
        m_state->rows.append({typed, status});
        m_state->total += typed.size() + status.size();
    }
    m_state->total += m_state->title.size();
}

BootSequence::BootSequence(const QString &gifPath, const Theme &theme)
    : m_state(std::make_unique<State>())
{
    m_state->gif = true;
    m_state->theme = theme;
    m_state->reader.setFileName(gifPath);
    m_state->reader.setAutoTransform(true);
    m_state->valid = m_state->reader.canRead();
}

BootSequence::~BootSequence() = default;

bool BootSequence::valid() const
{
    return m_state && m_state->valid;
}

bool BootSequence::next(PlayFrame &frame)
{
    frame = PlayFrame{};
    frame.seconds = 1.0 / BootFps;
    if (!m_state || !m_state->valid)
        return false;
    if (m_state->gif) {
        const QImage raw = m_state->reader.read();
        if (raw.isNull())
            return false;
        const QImage fitted = raw.scaled(LcdW, LcdH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QImage image(LcdW, LcdH, QImage::Format_RGB32);
        image.fill(m_state->theme.bg);
        QPainter painter(&image);
        painter.drawImage((LcdW - fitted.width()) / 2, (LcdH - fitted.height()) / 2, fitted);
        frame.lcd = image;
        const int delay = m_state->reader.nextImageDelay();
        frame.seconds = delay > 0 ? delay / 1000.0 : 1.0 / BootFps;
        return true;
    }

    State &state = *m_state;
    if (state.phase == 0) {
        const int count = stretched(state.stretch, 12);
        if (state.index >= count) {
            state.phase = 1;
            state.index = 0;
            return next(frame);
        }
        const double t = (state.index + 1.0) / count;
        QImage image(LcdW, LcdH, QImage::Format_RGB32);
        image.fill(Qt::black);
        QPainter painter(&image);
        const double width = LcdW * std::min(1.0, t * 2);
        const double height = t <= 0.5 ? 2 : std::max(2.0, LcdH * (t - 0.5) * 2);
        painter.setPen(Qt::NoPen);
        painter.setBrush(mix(Qt::white, state.theme.bg, std::max(0.0, t - 0.5) * 2));
        painter.drawRect(QRectF((LcdW - width) / 2, (LcdH - height) / 2, width, height));
        frame.lcd = finishFrame(image, state.scan);
        ++state.index;
        return true;
    }

    if (state.phase == 1) {
        const double step = state.total / static_cast<double>(stretched(state.stretch, 40));
        const double limit = state.total + step * stretched(state.stretch, 6);
        if (step <= 0 || state.shown >= limit) {
            state.phase = 2;
            state.index = 0;
            state.keys->arm(state.keyFrame);
            return next(frame);
        }
        state.shown += step;
        ++state.textFrame;
        QImage image(LcdW, LcdH, QImage::Format_RGB32);
        image.fill(state.theme.bg);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setPen(QPen(state.theme.frame, 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(4, 4, LcdW - 9, LcdH - 9));
        const QFont titleFont = fixedTextFont(QStringLiteral(":bold"), 30);
        const QFont lineFont = fixedTextFont(QString(), 24);
        int left = static_cast<int>(state.shown);
        const QString title = state.title.left(std::max(0, left));
        drawLeftMiddle(painter, QPointF(32, 40), title, titleFont, state.theme.accent);
        QPointF cursor(32 + QFontMetricsF(titleFont).horizontalAdvance(title), 40);
        left -= state.title.size();
        qreal y = 100;
        for (const auto &row : state.rows) {
            if (left <= 0)
                break;
            const QString typed = row.first.left(std::max(0, left));
            drawLeftMiddle(painter, QPointF(32, y), typed, lineFont, state.theme.fg);
            qreal x = 32 + QFontMetricsF(lineFont).horizontalAdvance(typed);
            const QString status = row.second.left(std::max(0, left - static_cast<int>(row.first.size())));
            if (!status.isEmpty()) {
                drawLeftMiddle(painter, QPointF(x, y), status, lineFont, state.theme.accent);
                x += QFontMetricsF(lineFont).horizontalAdvance(status);
            }
            cursor = QPointF(x, y);
            left -= row.first.size() + row.second.size();
            y += 44;
        }
        const int blink = stretched(state.stretch, 1);
        if ((state.textFrame / blink) % 16 < 10) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(state.theme.fg);
            painter.drawRect(QRectF(cursor.x() + 4, cursor.y() - 12, 14, 24));
        }
        frame.lcd = finishFrame(image, state.scan);
        state.keys->apply(state.keyFrame, frame);
        ++state.keyFrame;
        return true;
    }

    if (state.phase == 2) {
        const int count = stretched(state.stretch, 36);
        if (state.index >= count) {
            state.phase = 3;
            state.index = 0;
            return next(frame);
        }
        const double p = (state.index + 1.0) / count;
        const double amount = std::pow(1.0 - p, 2.0);
        QImage image(LcdW, LcdH, QImage::Format_RGB32);
        image.fill(state.theme.bg);
        if (!state.logo.isNull()) {
            QImage layer(LcdW, LcdH, QImage::Format_ARGB32_Premultiplied);
            layer.fill(Qt::transparent);
            QPainter painter(&layer);
            painter.drawImage((LcdW - state.logo.width()) / 2, 16, state.logo);
            painter.end();
            if (amount > 0.02)
                layer = glitchLogo(layer, state.rng, amount);
            scaleAlpha(layer, std::min(1.0, p * 1.6));
            QPainter compose(&image);
            compose.drawImage(0, 0, layer);
        }
        if (state.rng.generateDouble() < 0.6 * amount) {
            for (int y = 0; y < image.height(); ++y) {
                auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
                for (int x = 0; x < image.width(); ++x) {
                    const auto lift = [](int value) { return std::min(255, static_cast<int>(value * 1.6)); };
                    line[x] = qRgb(lift(qRed(line[x])), lift(qGreen(line[x])), lift(qBlue(line[x])));
                }
            }
        }
        if (state.index + 1 >= count)
            state.holdBase = image;
        frame.lcd = finishFrame(image, state.scan);
        state.keys->apply(state.keyFrame, frame);
        ++state.keyFrame;
        ++state.index;
        return true;
    }

    const int minimum = stretched(state.stretch, state.footer.size() + 18);
    if (state.index >= minimum && state.keys->done() >= Keys)
        return false;
    QImage image = state.holdBase.isNull() ? QImage(LcdW, LcdH, QImage::Format_RGB32) : state.holdBase;
    if (state.holdBase.isNull())
        image.fill(state.theme.bg);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const int per = stretched(state.stretch, 1);
    const QString footer = state.footer.left(state.index / per + 1);
    const QFont font = fixedTextFont(QStringLiteral(":bold"), 26);
    drawCentered(painter, QPointF(LcdW / 2.0, LcdH - 34), footer, font, state.theme.accent);
    frame.lcd = finishFrame(image, state.scan);
    state.keys->apply(state.keyFrame, frame);
    ++state.keyFrame;
    ++state.index;
    return true;
}

} // namespace galleon
