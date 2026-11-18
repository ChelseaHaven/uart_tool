#include "serialconnectionmodel.h"

SerialConnectionModel::SerialConnectionModel(SerialSource &source, QObject *parent)
    : QObject(parent), m_source(source) {
    connect(&source, &ByteSource::opened, this, &SerialConnectionModel::connectionChanged);
    connect(&source, &ByteSource::closed, this, [this] {
        emit connectionChanged();
        refreshPorts();
    });
    connect(&source, &SerialSource::errorChanged, this, &SerialConnectionModel::errorChanged);
    refreshPorts();
}
void SerialConnectionModel::refreshPorts() {
    if (connected()) return;
    m_ports.clear();
    for (const auto &port : m_source.availablePorts()) {
        const QString label = port.description.isEmpty() ? port.name
                            : QStringLiteral("%1 · %2").arg(port.name, port.description);
        m_ports.append(QVariantMap{{"label", label}, {"path", port.path}});
    }
    emit portsChanged();
}
bool SerialConnectionModel::openPort(const QString &path, int baud, int bits, int parity, int stops, int flow) {
    SerialSettings settings;
    settings.path = path; settings.baud = baud;
    settings.dataBits = static_cast<QSerialPort::DataBits>(bits);
    settings.parity = static_cast<QSerialPort::Parity>(parity);
    settings.stopBits = static_cast<QSerialPort::StopBits>(stops);
    settings.flowControl = static_cast<QSerialPort::FlowControl>(flow);
    return m_source.open(settings);
}
void SerialConnectionModel::disconnectPort() { m_source.close(); }

