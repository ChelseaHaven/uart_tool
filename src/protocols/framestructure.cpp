#include "framestructure.h"
#include "binaryconfig.h"
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace {
bool hex(const QVariant &value, QByteArray &out) {
    QString s=value.toString(); s.remove(QRegularExpression("\\s"));
    if(s.isEmpty() || s.size()>32 || s.size()%2 || !QRegularExpression("^[0-9a-fA-F]+$").match(s).hasMatch()) return false;
    out=QByteArray::fromHex(s.toLatin1()); return true;
}
bool width(const QVariantMap &b,int &value) {
    bool ok=false; const double number=b.value("bytes",2).toDouble(&ok);
    value=int(ok && number>=1 && number<=16 ? number : 0);
    return ok && std::isfinite(number) && number==value && value>0;
}
}
QVariantMap FrameStructure::compile(const QVariantList &blocks,const QVariantMap &base,QString &error) {
    int commands=0,payloads=0;
    for(const auto &value:blocks) { const auto kind=value.toMap().value("kind"); commands+=kind=="command"; payloads+=kind=="payload"; }
    if(commands>1 || payloads>1) return compileMultiple(blocks,base,error);
    error.clear();
    auto fail=[&](const QString &message){error=message; return QVariantMap{};};
    if(blocks.size()<2 || blocks.size()>24) return fail(QStringLiteral("帧结构需要 2～24 个部分"));
    const auto &source=base;
    QVariantMap c=source; c["commandMatches"]=QVariantList{}; QVariantList rules,metadata;
    struct Part { QVariantMap block; int offset,size; bool suffix; };
    QVector<Part> parts;
    QSet<QString> singleton;
    int prefix=0,suffix=0; bool payload=false;
    c["checksum"]="none"; c["commandOffset"]=-1; c.remove("messages"); c["tail"]="";
    for(qsizetype i=0;i<blocks.size();++i) {
        const auto b=blocks[i].toMap(); const auto kind=b.value("kind").toString(); int size=0;
        if(QStringList{"header","length","command","payload","tail"}.contains(kind)) {
            if(singleton.contains(kind)) return fail(QStringLiteral("%1 只能出现一次").arg(kind));
            singleton.insert(kind);
        }
        if(kind=="payload") {
            payload=true; c["payloadOffset"]=prefix; continue;
        }
        if(kind=="header" || kind=="tail") {
            QByteArray bytes;
            if(!hex(b.value("hex"),bytes)) return fail(QStringLiteral("帧头／帧尾请输入 1～16 字节 HEX"));
            if((kind=="header" && i!=0) || (kind=="tail" && (i!=blocks.size()-1 || !payload))) return fail(QStringLiteral("帧头须在最前，帧尾须在数据区之后的最后"));
            c[kind]=QString::fromLatin1(bytes.toHex(' ').toUpper()); size=int(bytes.size());
        } else if(kind=="check") {
            ChecksumRule rule;
            if(!ChecksumRule::fromMap(b.value("rule").toMap(),rule,error)) return {};
            size=rule.bytes();
        } else if(kind=="length" || kind=="command" || kind=="padding") {
            if(!width(b,size)) return fail(QStringLiteral("部分字节数必须为 1～16 的整数"));
            if(kind!="padding") {
                if(payload || !QList<int>{1,2,4}.contains(size)) return fail(QStringLiteral("长度和命令字须在数据区前，宽度为 1／2／4 字节"));
                const auto endian=b.value("endian","little").toString();
                if(endian!="little" && endian!="big") return fail(QStringLiteral("字节序无效"));
                c[kind+"Offset"]=prefix; c[kind+"Bytes"]=size; c[kind+"Endian"]=endian;
                if(kind=="command") c["commandValue"]=b.value("value",source.value("commandValue","0x1"));
                if(kind=="length") {
                    c["lengthMeaning"]=b.value("meaning","payload");
                    c["lengthIncludesChecksum"]=b.value("includesTrailer",false);
                }
            }
        } else if(kind=="field") {
            if(payload) return fail(QStringLiteral("公共字段应放在数据区前；数据字段请在数据区模块中添加"));
            size=BinaryConfig::typeBytes(b.value("type","uint8").toString());
            if(!size) return fail(QStringLiteral("公共字段类型无效"));
        } else return fail(QStringLiteral("未知帧结构部分：%1").arg(kind));
        parts.append({b,payload?suffix:prefix,size,payload});
        if(payload) suffix+=size; else prefix+=size;
    }
    if(!singleton.contains("header") || !payload) return fail(QStringLiteral("请保留帧头和一个数据区"));
    if(suffix>16) return fail(QStringLiteral("数据区后的总长度暂限 16 字节"));
    c["mode"]=singleton.contains("length")?"variable":"fixed"; c["trailerBytes"]=suffix;
    for(const auto &p:parts) {
        const auto kind=p.block.value("kind").toString();
        if(kind=="check") {
            auto rule=p.block.value("rule").toMap(); const int offset=p.suffix?p.offset-suffix:p.offset;
            const auto coverage=p.block.value("coverage","before").toString();
            if(!QStringList{"before","payload","custom"}.contains(coverage)) return fail(QStringLiteral("校验范围无效"));
            rule["offset"]=offset; rule["early"]=!p.suffix;
            if(coverage!="custom") { rule["start"]=coverage=="payload"?prefix:0; rule["end"]=offset; }
            rules.append(rule);
        } else if(kind=="field" || kind=="command") {
            metadata.append(QVariantMap{{"name",p.block.value("name",kind=="command"?"command":"sequence")},
                {"type",kind=="command"?QString("uint%1").arg(p.size*8):p.block.value("type","uint8")},
                {"offset",p.offset},{"count",1},{"variable",false},{"endian",p.block.value("endian","little")}});
        }
    }
    c["checks"]=rules;
    // Structure mode always lays payload fields out in order, independent of old absolute offsets.
    auto arrange=[&](const QVariantList &rows,int &size,bool &variable) {
        QVariantList result; size=0; variable=false;
        if(rows.isEmpty() || rows.size()>64) { error=QStringLiteral("数据区需要 1～64 个字段"); return result; }
        for(const auto &value:rows) {
            auto f=value.toMap(); f["offset"]=prefix+size;
            bool ok=false; const double count=f.value("count",1).toDouble(&ok);
            const int width=BinaryConfig::typeBytes(f.value("type").toString());
            if(!ok || !std::isfinite(count) || count<1 || count>4096 || std::floor(count)!=count || !width || size+width*count>65536) {
                error=QStringLiteral("字段类型或数量无效，数据区不能超过 65536 字节"); return QVariantList{};
            }
            size+=width*int(count);
            variable|=f.value("variable").toBool(); result.append(f);
        }
        return result;
    };
    int size=0; bool variable=false;
    auto messages=source.value("messages").toList();
    if(!messages.isEmpty()) {
        if(!singleton.contains("command")) return fail(QStringLiteral("多个命令需要保留命令字部分"));
        int fixedSize=-1;
        for(auto &value:messages) {
            auto message=value.toMap(); message["fields"]=arrange(message.value("fields").toList(),size,variable);
            if(!error.isEmpty()) return {};
            if(!singleton.contains("length")) {
                if(variable || (fixedSize>=0 && fixedSize!=size)) return fail(QStringLiteral("不同长度的命令数据需要长度字段"));
                fixedSize=size;
            }
            message["payloadSize"]=variable?-1:size; value=message;
        }
        c["messages"]=messages;
        metadata.append(QVariantMap{{"name","payload"},{"type","bytes"},{"offset",prefix},{"count",fixedSize>=0?fixedSize:1},{"variable",fixedSize<0},{"endian","little"}});
        if(fixedSize>=0) c["frameSize"]=prefix+fixedSize+suffix;
    } else {
        auto rows=source.value("fields").toList();
        if(!source.value("structure").toList().isEmpty() || source.value("commandOffset",-1).toInt()>=0) {
            QVariantList data;
            for(const auto &f:rows) if(f.toMap().value("offset").toInt()>=source.value("payloadOffset").toInt()) data.append(f);
            rows=data;
        }
        metadata+=arrange(rows,size,variable);
        if(!error.isEmpty()) return {};
        c["frameSize"]=prefix+size+suffix;
    }
    c["fields"]=metadata; c["autoLayout"]=true; c["templateId"]="custom"; c["structure"]=blocks;
    BinaryConfig parsed;
    if(!BinaryConfig::fromMap(c,parsed,error)) return {};
    return parsed.toMap();
}
