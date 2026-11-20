#pragma once
#include "componentlayoutstore.h"
#include <QObject>
#include <QSettings>
#include <QVariantMap>
#include <memory>

// User preferences are independent of device connections and receive history.
class AppSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(ComponentLayoutStore *componentLayouts READ componentLayouts CONSTANT)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
public:
    explicit AppSettings(const QString &settingsFile = {}, QObject *parent = nullptr);
    ComponentLayoutStore *componentLayouts() { return &m_componentLayouts; }
    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool value);
    Q_INVOKABLE QVariantMap loadDockLayout() const;
    Q_INVOKABLE void saveDockLayout(const QVariantMap &layout);
signals:
    void darkModeChanged();
private:
    std::unique_ptr<QSettings> m_store;
    ComponentLayoutStore m_componentLayouts;
    bool m_darkMode = false;
};
