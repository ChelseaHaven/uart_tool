#pragma once
#include <QByteArray>
#include <QString>

struct ByteWriteResult {
    qint64 accepted=-1;
    QString error;
};
// Raw-byte output contract; independent of serial, QML and receive history.
class ByteSink {
public:
    virtual ~ByteSink()=default;
    virtual bool isWritable() const=0;
    virtual ByteWriteResult writeBytes(const QByteArray &bytes)=0;
};
