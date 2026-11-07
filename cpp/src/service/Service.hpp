#pragma once

#include "Types.hpp"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSocketNotifier>
#include <QTimer>

#include <memory>

namespace galleon {

class Deck;
class Keyboard;

// Headless driver. It keeps the deck in host mode, draws keys and the clock,
// and turns presses into keys, commands, pages, and profiles.
class Service : public QObject {
    Q_OBJECT

public:
    explicit Service(QObject *parent = nullptr);
    ~Service() override;

private:
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
    void drawLcd();
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

    QList<int> m_keyState;
    QHash<int, QList<int>> m_held;
    QHash<int, qint64> m_downAt;
    bool m_dialDown[2] = {false, false};
    bool m_dialTurned[2] = {false, false};
    int m_volume = -1;
    bool m_muted = false;
    QByteArray m_lastLcd;
    bool m_lcdDirty = true;
    bool m_typing = false;
};

} // namespace galleon
