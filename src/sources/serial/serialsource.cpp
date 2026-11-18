#include "serialsource.h"
#include <algorithm>
#include <utility>

SerialSource::SerialSource(PortScanner scanner, QObject *parent)
    : ByteSource(parent), m_scanner(std::move(scanner)) {
    m_serial.setReadBufferSize(64 * 1024);
    connect(&m_serial, &QSerialPort::readyRead, this, &SerialSource::readAvailable);
    connect(&m_serial, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (m_opening || m_closing || error == QSerialPort::NoError || !isOpen()) return;
        setError(QStringLiteral("串口连接中断：%1").arg(m_serial.errorString()));
        close();
    });
}
void SerialSource::setError(const QString &message) {
    if (m_error == message) return;
    m_error = message;
    emit errorChanged();
}
QList<SerialPortDescriptor> SerialSource::availablePorts() const {
    QList<SerialPortDescriptor> result;
    for (const auto &port : m_scanner())
        result.append({port.portName(), port.description(), port.systemLocation(),
                       port.hasVendorIdentifier() || port.hasProductIdentifier()});
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.path < b.path; });
    return result;
}
bool SerialSource::open(const SerialSettings &settings) {
    const auto &path = settings.path;
    const int baud = settings.baud, bits = settings.dataBits, parity = settings.parity;
    const int stops = settings.stopBits, flow = settings.flowControl;
    if (isOpen()) { setError(QStringLiteral("串口已连接，请先断开再修改参数。")); return false; }
    if (path.isEmpty() || baud <= 0 || bits < 5 || bits > 8 ||
        (parity != 0 && parity != 2 && parity != 3 && parity != 4 && parity != 5) ||
        (stops != 1 && stops != 2) || flow < 0 || flow > 2) {
        setError(QStringLiteral("串口参数无效，请检查端口和波特率。")); return false;
    }
    m_opening = true;
    m_serial.setPortName(path);
    const bool parameters = m_serial.setBaudRate(baud)
        && m_serial.setDataBits(settings.dataBits)
        && m_serial.setParity(settings.parity)
        && m_serial.setStopBits(settings.stopBits)
        && m_serial.setFlowControl(settings.flowControl);
    const bool success = parameters && m_serial.open(QIODevice::ReadWrite);
    m_opening = false;
    if (!success) {
        setError(QStringLiteral("打开失败：%1。请检查设备是否存在、是否被占用及访问权限。")
                 .arg(m_serial.errorString()));
        return false;
    }
    m_activePort = path; setError({});
    emit opened(); return true;
}
void SerialSource::close() {
    if (!isOpen()) return;
    m_closing = true;
    readAvailable();
    m_serial.close();
    m_closing = false;
    emit closed();
}
void SerialSource::readAvailable() {
    const QByteArray bytes = m_serial.readAll();
    if (!bytes.isEmpty()) emit bytesReceived(bytes);
}

ByteWriteResult SerialSource::writeBytes(const QByteArray &bytes) {
    if(!isWritable()) return {-1,QStringLiteral("串口未连接或不可写。")};
    if(bytes.size()>64*1024) return {-1,QStringLiteral("单次发送最多 64 KiB。")};
    if(m_serial.bytesToWrite()+bytes.size()>256*1024)
        return {-1,QStringLiteral("发送缓冲区已满，请稍后再试。")};
    const auto accepted=m_serial.write(bytes);
    return {accepted,accepted<0?QStringLiteral("发送失败：%1").arg(m_serial.errorString()):QString()};
}
