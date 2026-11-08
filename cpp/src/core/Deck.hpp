#pragma once

#include <QByteArray>
#include <QString>

namespace kdeck {

// The deck's hidraw node. Reports are 1024 bytes. Feature reports are 32.
class Deck {
public:
    Deck() = default;
    ~Deck();

    Deck(const Deck &) = delete;
    Deck &operator=(const Deck &) = delete;

    static QString findHidraw();

    bool open(const QString &path);
    void close();
    bool isOpen() const { return m_fd >= 0; }
    int fd() const { return m_fd; }

    bool ping();
    bool setBrightness(int percent);
    bool keyImage(int index, const QByteArray &jpeg);
    bool lcdImage(const QByteArray &jpeg, int x = 0, int y = 0, int w = 720, int h = 384);
    QByteArray read() const;

private:
    bool feature(const QByteArray &payload);
    bool writeAll(const QByteArray &packet);

    int m_fd = -1;
};

} // namespace kdeck
