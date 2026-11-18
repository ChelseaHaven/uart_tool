#pragma once
#include "binaryconfig.h"
#include <QVariant>
#include <functional>
#include <memory>
struct DecodedField { QString name, type; QVariant value; int byteCount=0, elementCount=1; };
struct DecodedFrame {
    QVector<DecodedField> fields;
    QByteArray bytes;
    QString messageName;
    // Consumers that keep live state update only the fields present in a sparse frame.
    bool sparseFields = false;
};
struct ParserStatistics { quint64 invalidFrames=0, discardedBytes=0; QString lastError; };
class ProtocolParser {
public:
    using FrameSink=std::function<void(DecodedFrame)>;
    virtual ~ProtocolParser()=default;
    virtual void feed(const QByteArray &bytes,const FrameSink &sink)=0;
    const ParserStatistics &statistics() const { return m_statistics; }
protected:
    ParserStatistics m_statistics;
    void reject(const QString &error) { ++m_statistics.invalidFrames; m_statistics.lastError=error; }
};
std::unique_ptr<ProtocolParser> makeProtocolParser(const QString &id,const BinaryConfig &config);
quint16 modbusCrc16(QByteArrayView bytes);
