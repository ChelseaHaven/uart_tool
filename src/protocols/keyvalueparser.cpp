#include "keyvalueparser.h"
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <cmath>

namespace {
class KeyValueParser final : public ProtocolParser {
    static constexpr qsizetype MaxLineBytes=8192;
    QByteArray m_pending;
    bool m_skipLine=false;

    bool decode(const QByteArray &line,DecodedFrame &frame) {
        QStringDecoder decoder(QStringDecoder::Utf8,QStringDecoder::Flag::Stateless);
        const QString text=decoder.decode(line);
        if(decoder.hasError()) return false;
        const auto entries=text.split(',');
        if(entries.isEmpty() || entries.size()>MaxDictionaryFields) return false;
        static const QRegularExpression number(QStringLiteral("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$"));
        QSet<QString> names;
        for(const auto &entry:entries) {
            const auto colon=entry.indexOf(':');
            if(colon<0 || entry.indexOf(':',colon+1)>=0) return false;
            const auto name=entry.first(colon).trimmed();
            const auto token=entry.sliced(colon+1).trimmed();
            if(name.isEmpty() || name.size()>64 || names.contains(name) || !number.match(token).hasMatch()) return false;
            for(const auto character:name) if(character.isNull() || character.category()==QChar::Other_Control
                || character=='{' || character=='}' || character=='\'' || character=='"') return false;
            bool ok=false; const double value=token.toDouble(&ok);
            if(!ok || !std::isfinite(value)) return false;
            names.insert(name);
            frame.fields.append({name,"float64",value,int(token.toUtf8().size()),1});
        }
        frame.bytes=line;
        frame.sparseFields=true;
        return true;
    }
public:
    void feed(const QByteArray &bytes,const FrameSink &sink) override {
        for(char byte:bytes) {
            if(byte!='\n') {
                if(m_skipLine) { ++m_statistics.discardedBytes; continue; }
                if(m_pending.size()>=MaxLineBytes) {
                    m_statistics.discardedBytes+=quint64(m_pending.size()+1);
                    m_pending.clear(); m_skipLine=true;
                    reject(QStringLiteral("键值文本超过 8192 字节，已丢弃至换行处"));
                } else m_pending.append(byte);
                continue;
            }
            if(m_skipLine) { m_skipLine=false; continue; }
            const auto line=m_pending.trimmed(); m_pending.clear();
            if(line.isEmpty()) continue;
            DecodedFrame frame;
            if(decode(line,frame)) sink(std::move(frame));
            else {
                m_statistics.discardedBytes+=quint64(line.size());
                reject(QStringLiteral("键值文本格式错误：每行使用 名称:数值,名称:数值；名称不能重复，每行最多 128 项，仅支持有限数值"));
            }
        }
    }
};
}
std::unique_ptr<ProtocolParser> makeKeyValueParser() { return std::make_unique<KeyValueParser>(); }
