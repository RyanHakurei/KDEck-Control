#pragma once

#include "Types.hpp"

#include <QEvent>
#include <QObject>
#include <QQuickImageProvider>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include <memory>

namespace kdeck {

class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY changed)
    Q_PROPERTY(QString profile READ profile WRITE setProfile NOTIFY changed)
    Q_PROPERTY(QStringList pages READ pages NOTIFY changed)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY changed)
    Q_PROPERTY(QString pageName READ pageName NOTIFY changed)
    Q_PROPERTY(int keyIndex READ keyIndex WRITE setKeyIndex NOTIFY changed)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY changed)
    Q_PROPERTY(QString icon READ icon WRITE setIcon NOTIFY changed)
    Q_PROPERTY(int iconCount READ iconCount NOTIFY iconsChanged)
    Q_PROPERTY(QString iconFilter READ iconFilter WRITE setIconFilter NOTIFY iconsChanged)
    Q_PROPERTY(QString image READ image NOTIFY changed)
    Q_PROPERTY(QStringList actionKindIds READ actionKindIds CONSTANT)
    Q_PROPERTY(QStringList actionKindLabels READ actionKindLabels CONSTANT)
    Q_PROPERTY(int actionKindIndex READ actionKindIndex WRITE setActionKindIndex NOTIFY changed)
    Q_PROPERTY(QString actionKindId READ actionKindId NOTIFY changed)
    Q_PROPERTY(QString actionValue READ actionValue WRITE setActionValue NOTIFY changed)
    Q_PROPERTY(QString appId READ appId WRITE setAppId NOTIFY changed)
    Q_PROPERTY(QString appArgs READ appArgs WRITE setAppArgs NOTIFY changed)
    Q_PROPERTY(QString appLabel READ appLabel NOTIFY changed)
    Q_PROPERTY(QVariantList applicationChoices READ applicationChoices NOTIFY applicationsChanged)
    Q_PROPERTY(QStringList pressKindIds READ pressKindIds CONSTANT)
    Q_PROPERTY(QStringList pressKindLabels READ pressKindLabels CONSTANT)
    Q_PROPERTY(int pressKindIndex READ pressKindIndex WRITE setPressKindIndex NOTIFY changed)
    Q_PROPERTY(QString pressKindId READ pressKindId NOTIFY changed)
    Q_PROPERTY(QStringList statIds READ statIds NOTIFY changed)
    Q_PROPERTY(QStringList statTitles READ statTitles NOTIFY changed)
    Q_PROPERTY(int statIndex READ statIndex WRITE setStatIndex NOTIFY changed)
    Q_PROPERTY(QStringList pageChoices READ pageChoices NOTIFY changed)
    Q_PROPERTY(QStringList mediaChoices READ mediaChoices CONSTANT)
    Q_PROPERTY(int repeatMs READ repeatMs WRITE setRepeatMs NOTIFY changed)
    Q_PROPERTY(int stepMs READ stepMs WRITE setStepMs NOTIFY changed)
    Q_PROPERTY(QString hold READ hold WRITE setHold NOTIFY changed)
    Q_PROPERTY(bool keyEditable READ keyEditable NOTIFY changed)
    Q_PROPERTY(bool confirm READ confirm WRITE setConfirm NOTIFY changed)
    Q_PROPERTY(QString special READ special NOTIFY changed)
    Q_PROPERTY(QString fg READ fg WRITE setFg NOTIFY changed)
    Q_PROPERTY(QString bg READ bg WRITE setBg NOTIFY changed)
    Q_PROPERTY(QString border READ border WRITE setBorder NOTIFY changed)
    Q_PROPERTY(bool canOpenFolder READ canOpenFolder NOTIFY changed)
    Q_PROPERTY(int folderDepth READ folderDepth NOTIFY changed)
    Q_PROPERTY(QStringList folderCrumbs READ folderCrumbs NOTIFY changed)
    Q_PROPERTY(QString place READ place NOTIFY changed)
    Q_PROPERTY(QStringList themes READ themes NOTIFY changed)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY changed)
    Q_PROPERTY(QString logo READ logo WRITE setLogo NOTIFY changed)
    Q_PROPERTY(QString followSource READ followSource NOTIFY changed)
    Q_PROPERTY(QString wallpaperHint READ wallpaperHint NOTIFY changed)
    Q_PROPERTY(bool wallpaperAuto READ wallpaperAuto WRITE setWallpaperAuto NOTIFY changed)
    Q_PROPERTY(QString wallpaperCommand READ wallpaperCommand WRITE setWallpaperCommand NOTIFY changed)
    Q_PROPERTY(QString wallpaperFont READ wallpaperFont WRITE setWallpaperFont NOTIFY changed)
    Q_PROPERTY(QString wallpaperClockFont READ wallpaperClockFont WRITE setWallpaperClockFont NOTIFY changed)
    Q_PROPERTY(QString textFontFamily READ textFontFamily NOTIFY changed)
    Q_PROPERTY(QString fixedFontFamily READ fixedFontFamily NOTIFY changed)
    Q_PROPERTY(bool transitionEnabled READ transitionEnabled WRITE setTransitionEnabled NOTIFY changed)
    Q_PROPERTY(double transitionSpeed READ transitionSpeed WRITE setTransitionSpeed NOTIFY changed)
    Q_PROPERTY(double transitionHold READ transitionHold WRITE setTransitionHold NOTIFY changed)
    Q_PROPERTY(bool bootEnabled READ bootEnabled WRITE setBootEnabled NOTIFY changed)
    Q_PROPERTY(double bootSpeed READ bootSpeed WRITE setBootSpeed NOTIFY changed)
    Q_PROPERTY(QString bootTitle READ bootTitle WRITE setBootTitle NOTIFY changed)
    Q_PROPERTY(QString bootLinesText READ bootLinesText WRITE setBootLinesText NOTIFY changed)
    Q_PROPERTY(QString bootLogo READ bootLogo WRITE setBootLogo NOTIFY changed)
    Q_PROPERTY(QString bootGif READ bootGif WRITE setBootGif NOTIFY changed)
    Q_PROPERTY(QString autoFallback READ autoFallback WRITE setAutoFallback NOTIFY changed)
    Q_PROPERTY(QVariantList switchRules READ switchRules NOTIFY changed)
    Q_PROPERTY(QVariantList profileSwitchRules READ profileSwitchRules NOTIFY changed)
    Q_PROPERTY(QString focusHint READ focusHint CONSTANT)
    Q_PROPERTY(QString startPage READ startPage WRITE setStartPage NOTIFY changed)
    Q_PROPERTY(bool settingsPage READ settingsPage WRITE setSettingsPage NOTIFY changed)
    Q_PROPERTY(int brightness READ brightness WRITE setBrightness NOTIFY changed)
    Q_PROPERTY(int volumeStep READ volumeStep WRITE setVolumeStep NOTIFY changed)
    Q_PROPERTY(QString clockFormat READ clockFormat WRITE setClockFormat NOTIFY changed)
    Q_PROPERTY(QString startProfile READ startProfile WRITE setStartProfile NOTIFY changed)
    Q_PROPERTY(QStringList mediaPlayers READ mediaPlayers NOTIFY changed)
    Q_PROPERTY(QStringList playerChoices READ playerChoices NOTIFY playersChanged)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int previewRevision READ previewRevision NOTIFY previewChanged)
    Q_PROPERTY(int animRevision READ animRevision NOTIFY animTicked)
    Q_PROPERTY(int statRevision READ statRevision NOTIFY statTicked)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVariantList addons READ addons NOTIFY addonsChanged)
    Q_PROPERTY(QString addonStatus READ addonStatus NOTIFY addonsChanged)
    Q_PROPERTY(QString addonDir READ addonDir NOTIFY addonsChanged)
    Q_PROPERTY(bool followTheme READ followTheme NOTIFY changed)
    Q_PROPERTY(bool serviceRunning READ serviceRunning NOTIFY serviceRunningChanged)

public:
    explicit Controller(QObject *parent = nullptr);
    ~Controller() override;

    QStringList profiles() const;
    QString profile() const { return m_profile; }
    void setProfile(const QString &name);

    QStringList pages() const;
    int page() const { return m_page; }
    void setPage(int page);
    Q_INVOKABLE bool pageIsSettings(int index) const;
    QString pageName() const;

    int keyIndex() const { return m_key; }
    void setKeyIndex(int index);

    QString label() const;
    void setLabel(const QString &label);
    QString icon() const;
    void setIcon(const QString &icon);
    QString image() const;
    QString fg() const;
    void setFg(const QString &value);
    QString bg() const;
    void setBg(const QString &value);
    QString border() const;
    void setBorder(const QString &value);
    bool canOpenFolder() const;
    int folderDepth() const { return m_folderPath.size(); }
    QStringList folderCrumbs() const;
    QString place() const;

    QStringList themes() const;
    QString theme() const;
    void setTheme(const QString &name);
    QString logo() const;
    void setLogo(const QString &path);
    QString followSource() const;
    QString wallpaperHint() const { return m_wallpaperHint; }
    bool wallpaperAuto() const { return m_config.wallpaperAuto; }
    void setWallpaperAuto(bool on);
    QString wallpaperCommand() const { return m_config.wallpaperCommand; }
    void setWallpaperCommand(const QString &command);
    QString wallpaperFont() const { return m_config.wallpaperFont; }
    void setWallpaperFont(const QString &font);
    QString wallpaperClockFont() const { return m_config.wallpaperClockFont; }
    void setWallpaperClockFont(const QString &font);
    QString textFontFamily() const;
    QString fixedFontFamily() const;
    bool transitionEnabled() const { return m_config.transitionEnabled; }
    void setTransitionEnabled(bool on);
    double transitionSpeed() const { return m_config.transitionSpeed; }
    void setTransitionSpeed(double value);
    double transitionHold() const { return m_config.transitionHold; }
    void setTransitionHold(double value);
    bool bootEnabled() const { return m_config.bootEnabled; }
    void setBootEnabled(bool on);
    double bootSpeed() const { return m_config.bootSpeed; }
    void setBootSpeed(double value);
    QString bootTitle() const { return m_config.bootTitle; }
    void setBootTitle(const QString &title);
    QString bootLinesText() const;
    void setBootLinesText(const QString &text);
    QString bootLogo() const { return m_config.bootLogo; }
    void setBootLogo(const QString &path);
    QString bootGif() const { return m_config.bootGif; }
    void setBootGif(const QString &path);
    QString autoFallback() const { return m_config.autoFallback; }
    void setAutoFallback(const QString &name);
    QVariantList switchRules() const;
    QVariantList profileSwitchRules() const;
    QString focusHint() const;
    QString startPage() const;
    void setStartPage(const QString &name);
    bool settingsPage() const;
    void setSettingsPage(bool on);

    int brightness() const { return m_config.brightness; }
    void setBrightness(int value);
    int volumeStep() const { return m_config.volumeStep; }
    void setVolumeStep(int value);
    QString clockFormat() const { return m_config.clockFormat; }
    void setClockFormat(const QString &format);
    QString startProfile() const { return m_config.startProfile; }
    void setStartProfile(const QString &name);
    QStringList mediaPlayers() const { return m_config.mediaPlayers; }
    QStringList playerChoices() const { return m_playerChoices; }
    // False when the name cannot be saved. An empty lock list removes the key.
    Q_INVOKABLE bool setMediaPlayerEnabled(const QString &name, bool on);

    QStringList actionKindIds() const;
    QStringList actionKindLabels() const;
    int actionKindIndex() const;
    void setActionKindIndex(int index);
    QString actionKindId() const;
    QString actionValue() const;
    void setActionValue(const QString &value);
    QString appId() const;
    void setAppId(const QString &id);
    QString appArgs() const;
    void setAppArgs(const QString &args);
    QString appLabel() const;
    QVariantList applicationChoices() const { return m_applications; }
    Q_INVOKABLE void refreshApplications();
    QStringList pressKindIds() const;
    QStringList pressKindLabels() const;
    int pressKindIndex() const;
    void setPressKindIndex(int index);
    QString pressKindId() const;
    QStringList statIds() const;
    QStringList statTitles() const;
    int statIndex() const;
    void setStatIndex(int index);
    QStringList pageChoices() const;
    QStringList mediaChoices() const;
    int repeatMs() const;
    void setRepeatMs(int value);
    int stepMs() const;
    void setStepMs(int value);
    QString hold() const;
    void setHold(const QString &hold);
    bool keyEditable() const;
    bool confirm() const;
    void setConfirm(bool confirm);
    QString special() const;

    QString status() const { return m_status; }
    bool dirty() const { return m_dirty; }
    int revision() const { return m_revision; }
    int previewRevision() const { return m_previewRevision; }
    int animRevision() const { return m_animRevision; }
    int statRevision() const { return m_statRevision; }
    bool serviceRunning() const { return m_serviceRunning; }
    QString error() const { return m_config.error; }
    QVariantList addons() const { return m_addons; }
    QString addonStatus() const { return m_addonStatus; }
    QString addonDir() const { return m_config.addonDir; }
    bool followTheme() const;

    QImage keyImage(int index) const;
    QImage lcdImage() const;
    QImage themeImage(const QString &name) const;
    Q_INVOKABLE QString colourHex(const QString &name) const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE void save();
    Q_INVOKABLE void setImageUrl(const QUrl &url);
    Q_INVOKABLE void clearImage();
    Q_INVOKABLE void openFolder(int index);
    Q_INVOKABLE void closeFolder();
    Q_INVOKABLE void showFolderDepth(int depth);
    Q_INVOKABLE void bumpPreview();
    Q_INVOKABLE void refreshAddons();
    Q_INVOKABLE void installAddon(const QString &name);
    Q_INVOKABLE void removeAddon(const QString &name);
    Q_INVOKABLE void installAddonFile(const QUrl &url);
    Q_INVOKABLE void runAddon(const QString &name, const QString &command);
    Q_INVOKABLE void setAddonDir(const QString &path);
    Q_INVOKABLE void clearAddonDir();
    // Empty string means the change was made, or the name was blank. A returned
    // string is the message the GTK configurator would toast and nothing was saved.
    Q_INVOKABLE QString addPage(const QString &name);
    Q_INVOKABLE QString renamePage(const QString &name);
    Q_INVOKABLE QString deletePage();
    Q_INVOKABLE void movePage(int direction);
    Q_INVOKABLE void movePageTo(int from, int to);
    Q_INVOKABLE QString makeStartPage();
    Q_INVOKABLE QString newProfile(const QString &name);
    Q_INVOKABLE QString duplicateProfile(const QString &name);
    Q_INVOKABLE QString renameProfile(const QString &name);
    Q_INVOKABLE QString deleteProfile();
    Q_INVOKABLE QString makeStartProfile();
    // Empty string from peek means the file could not be read (the status line has the reason).
    Q_INVOKABLE QString peekStreamDeck(const QUrl &url);
    Q_INVOKABLE QString importStreamDeck(const QUrl &url, const QString &name);
    Q_INVOKABLE void clearKey();
    Q_INVOKABLE void beginRecord(const QString &field);
    Q_INVOKABLE void cancelRecord();
    Q_INVOKABLE void swapKeys(int from, int to);
    Q_INVOKABLE void ensureIcons();
    Q_INVOKABLE QString themeColour(const QString &field) const;
    Q_INVOKABLE bool themeTweaked(const QString &field) const;
    Q_INVOKABLE void setThemeColour(const QString &field, const QString &hex);
    Q_INVOKABLE void resetThemeColour(const QString &field);
    Q_INVOKABLE void setLogoUrl(const QUrl &url);
    Q_INVOKABLE void generateWallpaperTheme();
    Q_INVOKABLE void refreshWallpaperHint();
    Q_INVOKABLE void setBootLogoUrl(const QUrl &url);
    Q_INVOKABLE void setBootGifUrl(const QUrl &url);
    Q_INVOKABLE void replayBoot();
    Q_INVOKABLE void restartService();
    Q_INVOKABLE void openConfigFolder();
    Q_INVOKABLE void addSwitchRule();
    Q_INVOKABLE void removeSwitchRule(int index);
    Q_INVOKABLE void setSwitchRuleClass(int index, const QString &pattern);
    Q_INVOKABLE void setSwitchRuleProfile(int index, const QString &profile);
    bool eventFilter(QObject *watched, QEvent *event) override;
    Q_INVOKABLE QString iconGlyph(int index) const;
    Q_INVOKABLE QString iconLabel(int index) const;
    Q_INVOKABLE QString iconName(int index) const;

Q_SIGNALS:
    void changed();
    void playersChanged();
    void previewChanged();
    void animTicked();
    void statTicked();
    void serviceRunningChanged();
    void addonsChanged();
    void applicationsChanged();
    void iconsChanged();
    void recordFinished();

private:
    const KeySpec *currentKey() const;
    KeySpec *currentKey();
    void *rawKey();
    void *rawKeyAt(int index);
    QString slotId() const;
    QString editorKind(const KeySpec &key) const;
    QString pressKindOf(const KeySpec &key) const;
    QString activeValueKind(const KeySpec &key) const;
    void rememberKind(const QString &kind, bool press);
    void mirrorCurrent(KeySpec &key);
    QString firstProfileName() const;
    void touch();
    void setStatus(const QString &status);
    void setColour(const char *field, QString *slot, const QString &value);
    void refreshPreview();
    void followDeck();
    void watchConfig();
    bool statsVisible() const;
    void syncKeyAnimation();
    void tickAnimations();
    void pollService();
    bool runAddonTool(const QStringList &args, QString *output, QString *errorText);
    bool writeConfig(const QString &key, const QString &literal);
    bool writeMediaPlayers(const QStringList &players);
    void publishPlayerChoices();
    QList<KeySpec> visibleKeys() const;
    QString fail(const QString &text);
    void flushEdits();
    void replaceProfileRefs(const QString &oldName, const QString &newName);
    QString shownTheme(const Profile &profile) const;
    void rebindThemeColours(Profile &profile);
    bool writeSectionKey(const QString &table, const QString &key, const QString &literal, bool remove);
    bool writeAutoRules(const QList<SwitchRule> &rules);
    bool runPythonDeck(const QString &body, QString *output, int timeoutMs);
    QString createProfile(const QString &requested, const QString &source);
    struct Store;
    std::unique_ptr<Store> m_store;
    Config m_config;
    QString m_profile;
    int m_page = 0;
    int m_key = 0;
    QList<int> m_folderPath;
    bool m_dirty = false;
    int m_revision = 0;
    int m_previewRevision = 0;
    LcdState m_locks;
    QStringList m_discoveredPlayers;
    QStringList m_playerChoices;
    QString m_status;
    QTimer m_saveTimer;
    QTimer m_followTimer;
    QTimer m_watchTimer;
    QTimer m_animTimer;
    int m_animRevision = 0;
    int m_statRevision = 0;
    bool m_serviceRunning = false;
    QHash<int, int> m_animFrame;
    qint64 m_seenStamp = -1;
    QByteArray m_followed;
    bool m_holdStatus = false;
    // Kind chosen in the editor before a value is typed. GTK keeps this on the
    // slot so "Press a key" stays selected while its field is still empty.
    QString m_pickedSlot;
    QString m_pickedKind;
    QString m_pickedPress;
    struct IconEntry {
        QString glyph;
        QString name;
        QString label;
    };
    QList<IconEntry> m_icons;
    QList<int> m_iconRows;
    bool m_iconsLoaded = false;
    QString m_iconFilter;
    bool m_recording = false;
    QString m_recordField;
    QString m_wallpaperHint;
    int iconCount() const { return m_iconRows.size(); }
    QString iconFilter() const { return m_iconFilter; }
    void setIconFilter(const QString &text);
    void rebuildIconRows();
    QVariantList m_addons;
    QVariantList m_applications;
    QString m_addonStatus;
    void restartBackend(bool replay);
};

class KeyImageProvider : public QQuickImageProvider {
public:
    explicit KeyImageProvider(Controller *controller);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    Controller *m_controller;
};

} // namespace kdeck
