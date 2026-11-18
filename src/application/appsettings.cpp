#include "appsettings.h"
AppSettings::AppSettings(const QString &settingsFile, QObject *parent) : QObject(parent) {
    m_store = settingsFile.isEmpty() ? std::make_unique<QSettings>()
                                   : std::make_unique<QSettings>(settingsFile, QSettings::IniFormat);
    m_darkMode = m_store->value("appearance/darkMode", false).toBool();
}
void AppSettings::setDarkMode(bool value) {
    if(value == m_darkMode) return;
    m_darkMode = value;
    m_store->setValue("appearance/darkMode", value);
    m_store->sync();
    emit darkModeChanged();
}
