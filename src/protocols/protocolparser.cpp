#include "protocolparser.h"
#include "keyvalueparser.h"
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace {
quint64 unsignedValue(QByteArrayView bytes,bool little) {
    quint64 n=0;
    if(little) for(qsizetype i=bytes.size();i>0;--i) n=(n<<8)|quint8(bytes[i-1]);
    else for(char c:bytes) n=(n<<8)|quint8(c);
    return n;
}
QVariant scalar(QByteArrayView bytes,const QString &type,bool little) {
    const quint64 n=unsignedValue(bytes,little);
    if(type=="float32") { const quint32 bits=quint32(n); float f; std::memcpy(&f,&bits,4); return double(f); }
    if(type=="float64") { double f; std::memcpy(&f,&n,8); return f; }
    if(type.startsWith("int")) {
        quint64 extended=n;
        const int width=int(bytes.size())*8;
        if(width<64 && (n & (quint64(1)<<(width-1)))) extended |= (~quint64(0)<<width);
        qint64 signedValue; std::memcpy(&signedValue,&extended,8); return QVariant::fromValue(signedValue);
    }
    return QVariant::fromValue(n);
}
class CsvParser final : public ProtocolParser {
    QByteArray m_pending; bool m_skipLine=false;
public:
    void feed(const QByteArray &bytes,const FrameSink &sink) override {
        // Process one byte at a time so an endless line never grows the buffer.
        for(char byte:bytes) {
            if(byte!='\n') {
                if(m_skipLine) { ++m_statistics.discardedBytes; continue; }
                if(m_pending.size()>=8192) {
                    m_statistics.discardedBytes+=quint64(m_pending.size()+1); m_pending.clear(); m_skipLine=true;
                    reject(QStringLiteral("文本行超过 8192 字节，已丢弃至换行处"));
                } else m_pending.append(byte);
                continue;
            }
            if(m_skipLine) { m_skipLine=false; continue; }
            const QByteArray line=m_pending.trimmed(); m_pending.clear();
            if(line.isEmpty()) continue;
            const auto items=line.split(',');
            if(items.size()>128) { reject(QStringLiteral("每行最多 128 个数字")); continue; }
            DecodedFrame frame; frame.bytes=line;
            bool valid=true;
            for(qsizetype i=0;i<items.size();++i) {
                bool ok=false; const double value=QString::fromLatin1(items[i].trimmed()).toDouble(&ok);
                if(!ok || !std::isfinite(value)) { valid=false; break; }
                frame.fields.append({QStringLiteral("CH%1").arg(i+1),"float64",value,int(items[i].size()),1});
            }
            if(valid) sink(std::move(frame)); else reject(QStringLiteral("该行不是完整的逗号分隔有限数字列表"));
        }
    }
};
class BinaryParser final : public ProtocolParser {
    BinaryConfig m_config; QByteArray m_pending;
    bool checksumValid(QByteArrayView frame) const {
        const int width=m_config.legacyChecksumBytes(); if(!width) return true;
        const int end=m_config.checksumEnd<0 ? int(frame.size())-width : m_config.checksumEnd;
        if(end<=m_config.checksumStart || end>frame.size()-width) return false;
        const auto covered=frame.sliced(m_config.checksumStart,end-m_config.checksumStart);
        quint64 calculated=0;
        if(m_config.checksum=="crc16modbus") calculated=modbusCrc16(covered);
        else {
            for(char c:covered) if(m_config.checksum=="xor8") calculated^=quint8(c); else calculated+=quint8(c);
            calculated &= width==1 ? 0xff : 0xffff;
        }
        return calculated==unsignedValue(frame.last(width),m_config.checksumEndian=="little");
    }
    bool ruleValid(const ChecksumRule &rule,QByteArrayView frame) const {
        if(!rule.enabled) return true;
        const int position=rule.offset<0?int(frame.size())+rule.offset:rule.offset;
        const int start=rule.start<0?int(frame.size())+rule.start:rule.start;
        const int end=rule.end<0?int(frame.size())+rule.end:rule.end;
        if(position<0 || position+rule.bytes()>frame.size() || start<0 || end>frame.size() || start>=end) return false;
        return rule.calculate(frame.sliced(start,end-start))==unsignedValue(frame.sliced(position,rule.bytes()),rule.endian=="little");
    }
    bool decode(const QByteArray &bytes,DecodedFrame &frame) {
        const int dataEnd=int(bytes.size())-m_config.checksumBytes();
        QVector<BinaryField> fields=m_config.fields;
        for(const auto &f:fields) {
            const int width=BinaryConfig::typeBytes(f.type);
            int count=f.count;
            if(f.variable) {
                const int remaining=dataEnd-f.offset;
                if(remaining<0 || remaining%width) return false;
                count=remaining/width;
            }
            if((count>4096 && f.type!="bytes") || f.offset>dataEnd || qint64(f.offset)+qint64(count)*width>dataEnd) return false;
            if(f.type=="padding") continue;
            const auto view=QByteArrayView(bytes).sliced(f.offset,count*width);
            DecodedField field; field.name=f.name; field.type=f.type; field.byteCount=count*width; field.elementCount=count;
            if(f.type=="bytes") field.value=QByteArray(view.data(),view.size());
            else if(!f.variable && count==1) field.value=scalar(view,f.type,f.endian=="little");
            else {
                QVariantList values; values.reserve(count);
                for(int i=0;i<count;++i) values.append(scalar(view.sliced(i*width,width),f.type,f.endian=="little"));
                field.value=values;
            }
            frame.fields.append(std::move(field));
        }
        frame.bytes=bytes; return true;
    }
    void parse(const FrameSink &sink) {
        for(;;) {
            const qsizetype at=m_pending.indexOf(m_config.header);
            if(at<0) {
                const qsizetype keep=std::min(m_pending.size(),m_config.header.size()-1);
                m_statistics.discardedBytes+=quint64(m_pending.size()-keep);
                m_pending=m_pending.right(keep); return;
            }
            if(at>0) { m_statistics.discardedBytes+=quint64(at); m_pending.remove(0,at); }
            bool headerFailed=false;
            for(const auto &rule:m_config.checks) if(rule.enabled && rule.early) {
                const int required=std::max(rule.offset+rule.bytes(),rule.end);
                if(m_pending.size()<required) return;
                if(!ruleValid(rule,QByteArrayView(m_pending))) { headerFailed=true; reject(QStringLiteral("提前校验失败：%1").arg(rule.name)); break; }
            }
            if(headerFailed) { m_pending.remove(0,1); ++m_statistics.discardedBytes; continue; }
            quint64 size=quint64(m_config.frameSize);
            if(m_config.mode=="variable") {
                if(m_pending.size()<m_config.lengthOffset+m_config.lengthBytes) return;
                const quint64 length=unsignedValue(QByteArrayView(m_pending).sliced(m_config.lengthOffset,m_config.lengthBytes),m_config.lengthEndian=="little");
                size=length+(m_config.lengthMeaning=="payload" ? quint64(m_config.payloadOffset) : 0)
                    +(m_config.lengthIncludesChecksum ? 0 : quint64(m_config.checksumBytes()));
            }
            const int minimum=m_config.mode=="variable" ? m_config.payloadOffset+m_config.checksumBytes() : int(m_config.header.size())+m_config.checksumBytes();
            if(size<quint64(minimum) || size>quint64(m_config.maxFrameSize)) {
                reject(QStringLiteral("长度字段超出允许帧长，正在重新寻找帧头"));
                m_pending.remove(0,1); ++m_statistics.discardedBytes; continue;
            }
            if(quint64(m_pending.size())<size) return;
            const QByteArray candidate=m_pending.first(qsizetype(size));
            bool checksValid=candidate.endsWith(m_config.tail) && checksumValid(candidate);
            for(const auto &rule:m_config.checks) if(!ruleValid(rule,QByteArrayView(candidate))) checksValid=false;
            if(!checksValid) {
                reject(QStringLiteral("校验或固定帧尾不匹配，正在重新同步帧头")); m_pending.remove(0,1); ++m_statistics.discardedBytes; continue;
            }
            if(m_config.commandOffset>=0) {
                if(m_config.commandOffset+m_config.commandBytes>candidate.size()-m_config.checksumBytes()) {
                    reject(QStringLiteral("命令字超出实际帧长")); m_pending.remove(0,qsizetype(size)); continue;
                }
                const auto command=unsignedValue(QByteArrayView(candidate).sliced(m_config.commandOffset,m_config.commandBytes),m_config.commandEndian=="little");
                if(command!=m_config.commandValue) { m_pending.remove(0,qsizetype(size)); continue; }
            }
            bool commandsMatch=true;
            for(const auto &value:m_config.commandMatches) {
                const auto match=value.toMap(); const int offset=match.value("offset").toInt(),width=match.value("bytes").toInt();
                const auto text=match.value("value").toString();
                const auto expected=text.toULongLong(nullptr,text.startsWith("0x",Qt::CaseInsensitive)?16:10);
                if(offset+width>candidate.size()-m_config.checksumBytes()
                    || unsignedValue(QByteArrayView(candidate).sliced(offset,width),match.value("endian")=="little")!=expected) commandsMatch=false;
            }
            if(!commandsMatch) { m_pending.remove(0,qsizetype(size)); continue; }
            DecodedFrame frame;
            if(!decode(candidate,frame)) {
                reject(QStringLiteral("字段越界、数组长度不能整除类型宽度或超过 4096 项"));
                m_pending.remove(0,1); ++m_statistics.discardedBytes; continue;
            }
            m_pending.remove(0,qsizetype(size)); sink(std::move(frame));
        }
    }
public:
    explicit BinaryParser(BinaryConfig config):m_config(std::move(config)) {}
    void feed(const QByteArray &bytes,const FrameSink &sink) override {
        // Bounded even if a transport hands us a huge chunk or no valid frame ever arrives.
        for(qsizetype i=0;i<bytes.size();i+=4096) {
            m_pending.append(QByteArrayView(bytes).sliced(i,std::min<qsizetype>(4096,bytes.size()-i)));
            parse(sink);
        }
    }
};
}
quint16 modbusCrc16(QByteArrayView bytes) {
    quint16 crc=0xffff;
    for(char byte:bytes) {
        crc^=quint8(byte);
        for(int i=0;i<8;++i) crc=crc&1 ? quint16((crc>>1)^0xa001) : quint16(crc>>1);
    }
    return crc;
}
std::unique_ptr<ProtocolParser> makeProtocolParser(const QString &id,const BinaryConfig &config) {
    if(id=="keyvalue") return makeKeyValueParser();
    if(id=="csv") return std::make_unique<CsvParser>();
    if(id=="binary") {
        BinaryConfig selected=config;
        if(!config.messages.isEmpty()) { QString error; if(!BinaryConfig::fromMap(BinaryConfig::singleCommandConfiguration(config.toMap()),selected,error)) return {}; }
        return std::make_unique<BinaryParser>(selected);
    }
    return {};
}
