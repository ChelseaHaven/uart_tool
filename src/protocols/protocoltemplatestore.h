#pragma once
#include <QObject>
#include <QVariantMap>
#include <QUrl>

// File persistence only; does not select protocols or modify a running parser.
class ProtocolTemplateStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(QVariantList builtInEntries READ builtInEntries CONSTANT)
    Q_PROPERTY(QString directory READ directory CONSTANT)
    Q_PROPERTY(QUrl directoryUrl READ directoryUrl CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit ProtocolTemplateStore(QObject *parent=nullptr, const QString &directory={});
    QVariantList entries() const { return m_entries; }
    QVariantList builtInEntries() const;
    QString nameForId(const QString &id) const;
    QString directory() const { return m_directory; }
    QUrl directoryUrl() const { return QUrl::fromLocalFile(m_directory); }
    QString lastError() const { return m_error; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap loadTemplate(const QString &id);
    Q_INVOKABLE QString saveTemplate(const QString &name,const QVariantMap &configuration,const QString &id={});
    Q_INVOKABLE QString importTemplate(const QUrl &file);
    Q_INVOKABLE bool exportConfiguration(const QString &name,const QVariantMap &configuration,const QUrl &file);
    Q_INVOKABLE bool deleteTemplate(const QString &id);
signals:
    void entriesChanged(); void errorChanged();
private:
    QString m_directory,m_error;
    QVariantList m_entries;
    void setError(const QString &error);
    QVariantMap read(const QString &file,QString &error) const;
    QVariantMap normalize(const QVariantMap &configuration,QString &error) const;
    bool write(const QString &path,const QString &id,const QString &name,const QVariantMap &configuration);
    QString pathFor(const QString &id) const;
    bool validName(const QString &name,const QString &exceptId={});
};
