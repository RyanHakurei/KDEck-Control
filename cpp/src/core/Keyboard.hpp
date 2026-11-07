#pragma once

#include <QList>
#include <QMutex>
#include <QString>

struct libevdev;
struct libevdev_uinput;

namespace galleon {

// A uinput keyboard named "galleon-deck", used for key, shortcut, and media actions.
class Keyboard {
public:
    Keyboard();
    ~Keyboard();

    Keyboard(const Keyboard &) = delete;
    Keyboard &operator=(const Keyboard &) = delete;

    bool isOpen() const { return m_uinput != nullptr; }

    // "SUPER+SHIFT+S" or "KP7". Throws std::runtime_error on an unknown name.
    static QList<int> codes(const QString &combo);

    void press(const QList<int> &codes);
    void release(const QList<int> &codes);
    void tap(const QList<int> &codes);
    void typeSequence(const QList<QList<int>> &steps, const QList<int> &hold, int stepMs);

private:
    void writeKey(int code, int value);
    void sync();

    libevdev *m_dev = nullptr;
    libevdev_uinput *m_uinput = nullptr;
    QMutex m_lock;
};

} // namespace galleon
