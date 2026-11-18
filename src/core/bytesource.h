#pragma once
#include <QObject>
#include <QByteArray>

// Only for raw byte transports (e.g. serial/TCP). Structured ROS messages do not
// need to implement this interface: adapt those to future typed channel models.
class ByteSource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual bool isOpen() const = 0;
    virtual void close() = 0;
signals:
    void bytesReceived(const QByteArray &bytes);
    void opened();
    void closed();
};
