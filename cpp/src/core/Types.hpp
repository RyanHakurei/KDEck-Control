#pragma once

#include <QByteArray>
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
    // Desktop-file id or executable path, plus extra arguments. Not a shell command.
    QString app, args;
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

// One window-focus rule. Profile files omit `profile`; config.toml rules include it.
struct SwitchRule {
    QString classPattern;
    QString titlePattern;
    QString profile;
};

struct Profile {
    QString name, path, themeName, startPage, screen, logo;
    Theme theme;
    QList<Page> pages;
    QList<SwitchRule> autoSwitch;
    bool settingsPage = true;
    // follow_theme with no theme of its own. An empty target follows the start profile.
    bool followTheme = false;
    QString followTarget;
};

struct Config {
    QString dir;
    QString path;
    int brightness = 70;
    int volumeStep = 2;
    QString clockFormat = QStringLiteral("%H:%M");
    QString startProfile;
    // media_players. Empty follows whichever player playerctl picks.
    QStringList mediaPlayers;
    // [addons] dir from config.toml. Empty means only installed packages are listed.
    QString addonDir;
    // [wallpaper_theme]. Fonts fall back to the amber theme when the file omits them.
    bool wallpaperAuto = false;
    QString wallpaperCommand;
    QString wallpaperFont = QStringLiteral("Quantico");
    QString wallpaperClockFont = QStringLiteral("Quantico:bold");
    // [transition] and [boot]. Defaults match the GTK configurator.
    bool transitionEnabled = true;
    double transitionSpeed = 1.0;
    double transitionHold = 0.5;
    bool bootEnabled = true;
    double bootSpeed = 1.0;
    QString bootTitle = QStringLiteral("GALLEON DECK");
    QStringList bootLines;
    QString bootLogo;
    QString bootGif;
    // [auto_switch]. An empty fallback means stay on the current profile.
    QString autoFallback;
    QList<SwitchRule> autoRules;
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
    QString title;
    QString artist;
    QString artPath;
    int position = -1;
    int length = -1;
    bool playing = false;
};

// Latest mtime across config.toml, profiles, and themes, plus the file count.
// A save anywhere changes it, which is how the service notices an edit.
qint64 configStamp();

// $XDG_RUNTIME_DIR/fileName, or /tmp/fileName when the runtime dir is unset.
inline QString runtimePath(const QString &fileName)
{
    const QByteArray runtime = qgetenv("XDG_RUNTIME_DIR");
    const QString dir = runtime.isEmpty() ? QStringLiteral("/tmp") : QString::fromUtf8(runtime);
    return dir + QLatin1Char('/') + fileName;
}

// True when themes/wallpaper.toml was generated from this image at this mtime.
bool wallpaperThemeMatches(const QString &dir, const QString &imagePath);

QString configPath(const QString &dir, const QString &path);
QString placeLabel(const QString &pageName, const QList<KeySpec> &keys, const QList<int> &folderPath);
QList<KeySpec> keysAt(const QList<KeySpec> &pageKeys, const QList<int> &folderPath, QList<int> *resolved = nullptr);

// Replace or insert a top-level `key = literal` line. `literal` is already TOML.
bool assignTopLevel(const QString &path, const QString &key, const QString &literal);
bool removeTopLevel(const QString &path, const QString &key);
// Replace or insert `key = literal` inside a `[table]` section. `literal` is already TOML.
bool assignTableKey(const QString &path, const QString &table, const QString &key, const QString &literal);
bool removeTableKey(const QString &path, const QString &table, const QString &key);
// Replace an existing `key = ...` inside `[table]`, including a value that spans lines.
// A single-line value keeps a trailing comment. Returns false when the key is absent.
bool replaceTableValue(const QString &path, const QString &table, const QString &key, const QString &literal);

} // namespace galleon
