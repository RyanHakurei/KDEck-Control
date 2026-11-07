#pragma once

#include "Types.hpp"

#include <QObject>
#include <QQuickImageProvider>
#include <QUrl>
#include <QVariant>

#include <memory>

namespace galleon {

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
    Q_PROPERTY(QString image READ image NOTIFY changed)
    Q_PROPERTY(QStringList actionKindIds READ actionKindIds CONSTANT)
    Q_PROPERTY(QStringList actionKindLabels READ actionKindLabels CONSTANT)
    Q_PROPERTY(int actionKindIndex READ actionKindIndex WRITE setActionKindIndex NOTIFY changed)
    Q_PROPERTY(QString actionValue READ actionValue WRITE setActionValue NOTIFY changed)
    Q_PROPERTY(bool confirm READ confirm WRITE setConfirm NOTIFY changed)
    Q_PROPERTY(QString special READ special NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit Controller(QObject *parent = nullptr);
    ~Controller() override;

    QStringList profiles() const;
    QString profile() const { return m_profile; }
    void setProfile(const QString &name);

    QStringList pages() const;
    int page() const { return m_page; }
    void setPage(int page);
    QString pageName() const;

    int keyIndex() const { return m_key; }
    void setKeyIndex(int index);

    QString label() const;
    void setLabel(const QString &label);
    QString icon() const;
    void setIcon(const QString &icon);
    QString image() const;

    QStringList actionKindIds() const;
    QStringList actionKindLabels() const;
    int actionKindIndex() const;
    void setActionKindIndex(int index);
    QString actionValue() const;
    void setActionValue(const QString &value);
    bool confirm() const;
    void setConfirm(bool confirm);
    QString special() const;

    QString status() const { return m_status; }
    bool dirty() const { return m_dirty; }
    int revision() const { return m_revision; }
    QString error() const { return m_config.error; }

    QImage keyImage(int index) const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE void save();
    Q_INVOKABLE void setImageUrl(const QUrl &url);
    Q_INVOKABLE void clearImage();

Q_SIGNALS:
    void changed();

private:
    const KeySpec *currentKey() const;
    KeySpec *currentKey();
    void *rawKey();
    void touch();
    void setStatus(const QString &status);
    struct Store;
    std::unique_ptr<Store> m_store;
    Config m_config;
    QString m_profile;
    int m_page = 0;
    int m_key = 0;
    bool m_dirty = false;
    int m_revision = 0;
    QString m_status;
};

class KeyImageProvider : public QQuickImageProvider {
public:
    explicit KeyImageProvider(Controller *controller);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    Controller *m_controller;
};

} // namespace galleon
