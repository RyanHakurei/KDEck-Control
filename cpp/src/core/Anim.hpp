#pragma once

#include "Types.hpp"

#include <QImage>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace kdeck {

// Digital interference. amount 0 leaves the image alone. Matches the Python glitch().
QImage glitchImage(const QImage &image, double amount);

// A profile screen image fitted to the LCD, or a null image when it cannot be opened.
QImage loadDeckScreen(const QString &path);

// A trimmed logo, or `fallback` drawn as text when the file is missing.
QImage loadDeckLogo(const QString &path, const Theme &theme, const QString &fallback);

// One frame of the profile-switch screen, before the glitch. `reveal` is 0..1.
// A screen image fades in by itself. Otherwise the logo fades in inside a frame.
QImage transitionFrame(const Theme &theme, const QImage &screen, const QImage &logo, double reveal);

// The clock-screen page dots, drawn on top of `image`.
QImage withPageDots(const QImage &image, int pageIndex, int pageCount, const Theme &theme);

// One step of a boot animation. A set bit in sendMask means keys[i] should be sent.
// A set bit in realMask means key i has finished decrypting and should be drawn for real.
struct PlayFrame {
    QImage lcd;
    QImage keys[Keys];
    quint16 sendMask = 0;
    quint16 realMask = 0;
    double seconds = 1.0 / 30.0;
};

// Built-in boot sequence, or a GIF/APNG/WebP fitted to the LCD.
// A GIF that cannot be opened is not valid; the caller uses the built-in sequence.
class BootSequence {
public:
    BootSequence(const Theme &theme, const QString &title, const QStringList &lines, const QString &logoPath,
                 double speed, const QString &footer, std::function<QImage(int)> realKey);
    BootSequence(const QString &gifPath, const Theme &theme);
    ~BootSequence();

    BootSequence(const BootSequence &) = delete;
    BootSequence &operator=(const BootSequence &) = delete;

    bool valid() const;
    bool next(PlayFrame &frame);

private:
    struct State;
    std::unique_ptr<State> m_state;
};

} // namespace kdeck
