#pragma once

#include "Types.hpp"

#include <QByteArray>
#include <QImage>

namespace galleon {

QByteArray encodeJpeg(const QImage &image, int quality = 92);
QImage renderKey(const KeySpec &spec, const Theme &theme, bool pressed = false);
QImage renderThemeKey(const QString &name, const Theme &theme, bool current);
QImage renderLcd(const LcdState &state, const Theme &theme);
LcdState keyboardStatus();

} // namespace galleon
