#pragma once
#include <QObject>
#include <QSettings>
#include <memory>

// User preferences are independent of device connections and receive history.
class AppSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
public:
    explicit AppSettings(const QString &settingsFile = {}, QObject *parent = nullptr);
    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool value);
signals:
    void darkModeChanged();
private:
    std::unique_ptr<QSettings> m_store;
    bool m_darkMode = false;
};
