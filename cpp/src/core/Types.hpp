#pragma once

#include <QColor>
#include <QHash>
#include <QList>
#include <QString>

namespace galleon {

inline constexpr int Keys = 12;
inline constexpr int KeyPx = 160;
inline constexpr int LcdW = 720;
inline constexpr int LcdH = 384;
inline constexpr int FolderHoldMs = 1000;

// HID_ID in the deck's uevent. Interface 0 is the Stream Deck protocol.
inline constexpr char DeckVid[] = "00001B1C";
inline constexpr char DeckPid[] = "00002B18";

struct Theme {
    QString name;
    QColor bg, keyBg, fg, accent, dim, border, frame, tick;
    int radius = 6;
    int borderWidth = 3;
    QString font = QStringLiteral("Quantico");
    QString clockFont = QStringLiteral("Quantico:bold");
    QString iconFont = QStringLiteral("Symbols Nerd Font");
    QString keyStyle = QStringLiteral("rounded");

    // "#rrggbb", or a theme colour name such as "accent".
    QColor colour(const QString &value) const;
};

struct KeySpec {
    QString label, icon, image;
    QString fg, bg, border;
    QString key, keys, exec, media, page, profile, stat;
    QString sequence, hold, repeat, setTheme;
    bool confirm = false;
    bool back = false;
    bool fill = false;
    bool wallpaperTheme = false;
    bool hasBrightness = false;
    int brightness = 0;
    int stepMs = 40;
    int repeatMs = 100;
    QList<KeySpec> folder;

    bool isEmpty() const;
    QString actionKind() const;
};

struct Page {
    QString name;
    QList<KeySpec> keys;
    bool settings = false;
};

struct Profile {
    QString name, path, themeName, startPage, screen, logo;
    Theme theme;
    QList<Page> pages;
    bool settingsPage = true;
};

struct Config {
    QString dir;
    QString path;
    int brightness = 70;
    int volumeStep = 2;
    QString clockFormat = QStringLiteral("%H:%M");
    QString startProfile;
    QHash<QString, Theme> themes;
    QList<Profile> profiles;
    QString error;

    const Profile *profile(const QString &name) const;
    Profile *profile(const QString &name);

    static QString configDir();
    static Config load();
    static bool ensureExamples();
};

struct LcdState {
    QString profile, page, clock, date, error, screen;
    int pageIndex = 0;
    int pageCount = 1;
    int volume = -1;
    bool muted = false;
    bool hasLocks = false;
    bool caps = false;
    bool num = false;
    bool scroll = false;
    int pollHz = 0;
};

QString configPath(const QString &dir, const QString &path);
QString placeLabel(const QString &pageName, const QList<KeySpec> &keys, const QList<int> &folderPath);
QList<KeySpec> keysAt(const QList<KeySpec> &pageKeys, const QList<int> &folderPath, QList<int> *resolved = nullptr);

// Replace or insert a top-level `key = literal` line. `literal` is already TOML.
bool assignTopLevel(const QString &path, const QString &key, const QString &literal);
bool removeTopLevel(const QString &path, const QString &key);

} // namespace galleon
