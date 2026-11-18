#pragma once
#include <QList>
#include <QString>

struct ProtocolDescriptor {
    QString id;
    QString name;
    QString description;
};

// Metadata stays separate from parser factories and user-owned binary layouts.
class ProtocolCatalog {
public:
    ProtocolCatalog();
    const QList<ProtocolDescriptor> &all() const { return m_protocols; }
    const ProtocolDescriptor *find(const QString &id) const;
private:
    QList<ProtocolDescriptor> m_protocols;
};
