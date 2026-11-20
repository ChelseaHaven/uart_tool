#pragma once

#include <QObject>
#include <QSettings>
#include <QUrl>
#include <QVariantMap>

// Stores only the component canvas layout, independently of live data and controls.
class ComponentLayoutStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit ComponentLayoutStore(QSettings &settings);

    QString lastError() const { return m_error; }
    Q_INVOKABLE QVariantMap lastLayout() const;
    Q_INVOKABLE bool saveLastLayout(const QVariantMap &layout);
    Q_INVOKABLE QVariantMap importLayout(const QUrl &file);
    Q_INVOKABLE bool exportLayout(const QVariantMap &layout, const QUrl &file);

signals:
    void errorChanged();

private:
    QSettings &m_settings;
    QString m_error;
    void setError(const QString &error);
};
