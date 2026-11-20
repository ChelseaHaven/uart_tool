#include "appsettings.h"
AppSettings::AppSettings(const QString &settingsFile, QObject *parent)
    : QObject(parent),
      m_store(settingsFile.isEmpty() ? std::make_unique<QSettings>()
                                    : std::make_unique<QSettings>(settingsFile, QSettings::IniFormat)),
      m_componentLayouts(*m_store) {
    m_darkMode = m_store->value("appearance/darkMode", false).toBool();
}
void AppSettings::setDarkMode(bool value) {
    if(value == m_darkMode) return;
    m_darkMode = value;
    m_store->setValue("appearance/darkMode", value);
    m_store->sync();
    emit darkModeChanged();
}

QVariantMap AppSettings::loadDockLayout() const {
    return m_store->value("workspace/dockLayout").toMap();
}

void AppSettings::saveDockLayout(const QVariantMap &layout) {
    if (layout.isEmpty())
        m_store->remove("workspace/dockLayout");
    else
        m_store->setValue("workspace/dockLayout", layout);
    m_store->sync();
}
