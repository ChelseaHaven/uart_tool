#pragma once
#include "sources/serial/serialsource.h"
#include <QVariantList>

// UI adapter for serial configuration only; it never owns received data.
class SerialConnectionModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList ports READ ports NOTIFY portsChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionChanged)
    Q_PROPERTY(QString activePort READ activePort NOTIFY connectionChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorChanged)
public:
    explicit SerialConnectionModel(SerialSource &source, QObject *parent = nullptr);
    QVariantList ports() const { return m_ports; }
    bool connected() const { return m_source.isOpen(); }
    QString activePort() const { return m_source.activePort(); }
    QString errorText() const { return m_source.errorText(); }
    Q_INVOKABLE void refreshPorts();
    Q_INVOKABLE bool openPort(const QString &path, int baud = 115200, int bits = 8,
                             int parity = 0, int stops = 1, int flow = 0);
    Q_INVOKABLE void disconnectPort();
signals:
    void portsChanged(); void connectionChanged(); void errorChanged();
private:
    SerialSource &m_source;
    QVariantList m_ports;
};
