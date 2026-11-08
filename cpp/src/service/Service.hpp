#pragma once

#include "Types.hpp"

#include <QAtomicInt>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSharedPointer>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>

#include <memory>
#include <optional>

namespace galleon {

class Deck;
class FocusThread;
class Keyboard;

// Headless driver. It keeps the deck in host mode, draws keys and the clock,
// and turns presses into keys, commands, pages, and profiles.
class Service : public QObject {
    Q_OBJECT

public:
    explicit Service(QObject *parent = nullptr);
    ~Service() override;

private:
    friend class FocusThread;
    void tryConnect();
    void disconnectDeck();
    void onDeckReadable();
    void onPing();
    void onTick();
    void reload();
    void handleReport(const QByteArray &data);
    void keyDown(int index);
    void keyUp(int index);
    void releaseHeld();
    void drawKeys();
    void drawKey(int index, bool pressed);
    void drawStatKeys();
    void drawDueAnimations();
    void drawLcd();
    void publishState();
    void stopRepeats();
    void toggleRepeat(int index, const KeySpec &spec);
    void confirmCommand(int index, const QString &program, const QStringList &args, int page, const QString &profile, const QList<int> &folder);
    void flashResult(int index, bool ok, int page, const QString &profile, const QList<int> &folder);
    void applyWallpaperTheme();
    void followWallpaper();
    void playBoot();
    void playTransition();
    void applyFocus(const QString &windowClass, const QString &title);
    QString focusTarget(const QString &windowClass, const QString &title) const;
    bool inputPending(int timeoutMs) const;
    void drainInput();
    QImage settledKey(int index) const;
    void setPage(int index, bool remember);
    void setProfile(const QString &name);
    void gotoPage(const QString &name);
    void enterFolder(int index);
    void leaveFolder(bool toRoot);
    void changeVolume(int clicks);
    void setBrightness(int delta);
    void setTheme(const QString &name);
    QList<KeySpec> currentKeys() const;
    QList<KeySpec> settingsKeys() const;
    const Profile *currentProfile() const;
    Theme currentTheme() const;
    int startPageIndex() const;

    Config m_config;
    QString m_profile;
    int m_page = 0;
    QList<int> m_folderPath;
    QList<int> m_history;
    QString m_error;
    qint64 m_configStamp = -1;

    std::unique_ptr<Deck> m_deck;
    std::unique_ptr<Keyboard> m_keyboard;
    std::unique_ptr<QSocketNotifier> m_notifier;
    QTimer m_ping;
    QTimer m_tick;
    QTimer m_retry;
    QTimer m_anim;
    QHash<int, QString> m_animShown;
    // Encoded animation frames, keyed by the file, the frame, and the colours
    // that change the picture. A repeating frame is sent again without drawing.
    QHash<QString, QByteArray> m_jpegCache;
    QHash<int, qint64> m_flashUntil;

    QList<int> m_keyState;
    QHash<int, QList<int>> m_held;
    QHash<int, qint64> m_downAt;
    struct RepeatSlot {
        QSharedPointer<QAtomicInt> stop;
        QThread *thread = nullptr;
    };
    QHash<int, RepeatSlot> m_repeating;
    QThread *m_sequenceThread = nullptr;
    bool m_dialDown[2] = {false, false};
    bool m_dialTurned[2] = {false, false};
    int m_volume = -1;
    bool m_muted = false;
    QByteArray m_lastLcd;
    QByteArray m_published;
    bool m_lcdDirty = true;
    QAtomicInt m_typing{0};
    int m_tickCount = 0;
    std::optional<QString> m_autoTarget;
    FocusThread *m_focus = nullptr;
};

} // namespace galleon
