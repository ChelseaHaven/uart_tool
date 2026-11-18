#pragma once
#include "core/bytesource.h"
#include "core/bytesink.h"
#include <QSerialPort>
#include <QSerialPortInfo>
#include <functional>

struct SerialSettings {
    QString path;
    int baud = 115200;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
};
struct SerialPortDescriptor {
    QString name, description, path;
    bool usb = false;
};

// Owns the actual device. No QML properties, display modes or receive history.
class SerialSource final : public ByteSource, public ByteSink {
    Q_OBJECT
public:
    using PortScanner = std::function<QList<QSerialPortInfo>()>;
    explicit SerialSource(PortScanner scanner = QSerialPortInfo::availablePorts, QObject *parent = nullptr);
    QList<SerialPortDescriptor> availablePorts() const;
    bool open(const SerialSettings &settings);
    void close() override;
    bool isOpen() const override { return m_serial.isOpen(); }
    bool isWritable() const override { return isOpen() && m_serial.isWritable(); }
    ByteWriteResult writeBytes(const QByteArray &bytes) override;
    QString activePort() const { return m_activePort; }
    QString errorText() const { return m_error; }
signals:
    void errorChanged();
private:
    QSerialPort m_serial;
    PortScanner m_scanner;
    QString m_activePort, m_error;
    bool m_opening = false, m_closing = false;
    void readAvailable();
    void setError(const QString &message);
};
