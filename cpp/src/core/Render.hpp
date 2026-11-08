#pragma once

#include "Types.hpp"

#include <QByteArray>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QStringList>

namespace galleon {

// Deck text uses the fonts chosen in KDE System Settings. `spec` may end in
// :bold, :semibold, or :light. Any family name in `spec` is ignored.
// `textFont` is the general font. `fixedTextFont` is the fixed-width font.
QFont textFont(const QString &spec, int pixelSize);
QFont fixedTextFont(const QString &spec, int pixelSize);
QFont fitTextFont(const QString &spec, const QString &text, int pixelSize, int maxWidth);
// Drop the cached family so the next paint reads System Settings again.
void invalidateTextFonts();

QByteArray encodeJpeg(const QImage &image, int quality = 92);
// `when` is a unix time. A negative value shows the first frame, so a folder
// mosaic does not flicker. An animation otherwise shows the frame at `when`.
QImage renderKey(const KeySpec &spec, const Theme &theme, bool pressed = false, double when = -1);
double unixSeconds();
// -1 when the file is missing or is a still. Stat keys are not animations.
int imageFrameIndex(const QString &path, double when);
QImage renderThemeKey(const QString &name, const Theme &theme, bool current);
QImage renderLcd(const LcdState &state, const Theme &theme);
LcdState keyboardStatus();
// Built-in themes in the GTK order, then user themes by file name.
QStringList themeNames(const Config &config);
// A deck key, including a theme swatch and the wallpaper action.
// A pressed swatch is inverted, the same as the GTK preview.
QImage renderDeckKey(const KeySpec &spec, const Theme &pageTheme, const QHash<QString, Theme> &themes,
                     const QString &activeTheme, bool pressed, double when);
QList<KeySpec> settingsKeys(const Config &config, const QString &activeProfile);

} // namespace galleon
