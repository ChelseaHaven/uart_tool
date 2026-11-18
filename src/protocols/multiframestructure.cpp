#include "framestructure.h"
#include "binaryconfig.h"
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace {
QVariantMap compileSelected(const QVariantList &blocks,const QVariantMap &base,QString &error) {
    error.clear();
    auto fail=[&](const QString &s){error=s; return QVariantMap{};};
    if(blocks.size()<2 || blocks.size()>24) return fail(QStringLiteral("帧结构需要 2～24 个部分"));
    int first=-1,last=-1;
    for(int i=0;i<blocks.size();++i) if(blocks[i].toMap().value("kind")=="payload") { if(first<0) first=i; last=i; }
    if(first<0) return fail(QStringLiteral("请添加数据区"));
    QVariantMap c=base; c.remove("messages"); c["commandOffset"]=-1; c["commandMatches"]=QVariantList{};
    c["checksum"]="none"; c["tail"]=""; c["checks"]=QVariantList{}; c["structure"]=blocks;
    QVariantList fields,matches,rules; QSet<QString> singletons;
    struct Check { QVariantMap block; int offset; bool trailer; };
    QVector<Check> checks;
    int cursor=0,dataStart=0,dataEnd=0,dataNumber=0,commandNumber=0;
    bool variable=false;
    for(int i=0;i<blocks.size();++i) {
        const auto b=blocks[i].toMap(); const auto kind=b.value("kind").toString();
        if(i==first) dataStart=cursor;
        if(kind=="header" || kind=="tail" || kind=="length") {
            if(singletons.contains(kind)) return fail(QStringLiteral("帧头、长度字段和帧尾各只能有一个"));
            singletons.insert(kind);
        }
        if(kind=="header" || kind=="tail") {
            QString text=b.value("hex").toString(); text.remove(QRegularExpression("\\s"));
            if(text.isEmpty() || text.size()>32 || text.size()%2 || !QRegularExpression("^[0-9a-fA-F]+$").match(text).hasMatch()) return fail(QStringLiteral("帧头／帧尾请输入 1～16 字节 HEX"));
            if((kind=="header" && i!=0) || (kind=="tail" && i!=blocks.size()-1)) return fail(QStringLiteral("帧头须在最前，帧尾须在最后"));
            c[kind]=text; cursor+=text.size()/2;
        } else if(kind=="payload") {
            ++dataNumber; const auto rows=b.value("fields").toList();
            if(rows.isEmpty() || rows.size()>64) return fail(QStringLiteral("每个数据区需要 1～64 个字段"));
            for(int n=0;n<rows.size();++n) {
                auto f=rows[n].toMap(); const int width=BinaryConfig::typeBytes(f.value("type").toString());
                bool ok=false; const double count=f.value("count",1).toDouble(&ok);
                if(!ok || !std::isfinite(count) || count<1 || count>4096 || std::floor(count)!=count || !width) return fail(QStringLiteral("数据字段类型或数量无效"));
                const auto name=f.value("name").toString().trimmed();
                if(name.isEmpty()) return fail(QStringLiteral("字段名称不能为空"));
                f["name"]=QStringLiteral("数据区%1.%2").arg(dataNumber).arg(name); f["offset"]=cursor;
                if(f.value("variable").toBool()) {
                    if(variable || i!=last || n!=rows.size()-1) return fail(QStringLiteral("变长数组只能放在最后一个数据区的末尾；前面的数据区需固定长度"));
                    variable=true;
                }
                cursor+=width*int(count); fields.append(f);
            }
            if(i==last) dataEnd=cursor;
        } else if(kind=="command" || kind=="length" || kind=="padding") {
            bool ok=false; const double number=b.value("bytes",2).toDouble(&ok); const int width=int(number);
            if(!ok || !std::isfinite(number) || number!=width || width<1 || width>16 || (kind!="padding" && !QList<int>{1,2,4}.contains(width))) return fail(QStringLiteral("命令字和长度宽度须为 1／2／4 字节，保留字节须为 1～16"));
            if(kind!="padding") {
                const auto endian=b.value("endian","little").toString();
                if(endian!="little" && endian!="big") return fail(QStringLiteral("字节序无效"));
                if(kind=="length") {
                    if(i>=first) return fail(QStringLiteral("整帧长度字段须在第一个数据区前"));
                    c["lengthOffset"]=cursor; c["lengthBytes"]=width; c["lengthEndian"]=endian;
                    c["lengthMeaning"]=b.value("meaning","payload"); c["lengthIncludesChecksum"]=b.value("includesTrailer",false);
                } else {
                    if(variable || i>last) return fail(QStringLiteral("命令字应在其数据区前，不能位于变长数组之后"));
                    matches.append(QVariantMap{{"offset",cursor},{"bytes",width},{"endian",endian},{"value",b.value("value","0x1")}});
                    fields.append(QVariantMap{{"name",QStringLiteral("命令字%1").arg(++commandNumber)},{"type",QString("uint%1").arg(width*8)},{"offset",cursor},{"count",1},{"variable",false},{"endian",endian}});
                }
            }
            cursor+=width;
        } else if(kind=="field") {
            if(i>last || variable) return fail(QStringLiteral("公共字段须位于最后一个数据区结束之前"));
            const int width=BinaryConfig::typeBytes(b.value("type","uint8").toString());
            if(!width) return fail(QStringLiteral("公共字段类型无效"));
            fields.append(QVariantMap{{"name",b.value("name","sequence")},{"type",b.value("type","uint8")},{"offset",cursor},{"count",1},{"variable",false},{"endian",b.value("endian","little")}});
            cursor+=width;
        } else if(kind=="check") {
            ChecksumRule rule; if(!ChecksumRule::fromMap(b.value("rule").toMap(),rule,error)) return {};
            checks.append({b,cursor,i>last}); cursor+=rule.bytes();
        } else return fail(QStringLiteral("未知帧结构模块：%1").arg(kind));
        if(cursor>65536) return fail(QStringLiteral("帧长不能超过 65536 字节"));
    }
    if(!singletons.contains("header")) return fail(QStringLiteral("请保留帧头"));
    const int suffix=cursor-dataEnd;
    if(suffix>16) return fail(QStringLiteral("最后一个数据区后的总长度暂限 16 字节"));
    if(variable && !singletons.contains("length")) return fail(QStringLiteral("变长数组需要整帧长度字段"));
    for(const auto &check:checks) {
        auto rule=check.block.value("rule").toMap(); const int offset=check.trailer?check.offset-cursor:check.offset;
        const auto coverage=check.block.value("coverage","before").toString();
        if(!QStringList{"before","payload","custom"}.contains(coverage)) return fail(QStringLiteral("校验范围无效"));
        rule["offset"]=offset; rule["early"]=check.offset<dataStart;
        if(coverage!="custom") { rule["start"]=coverage=="payload"?dataStart:0; rule["end"]=offset; }
        rules.append(rule);
    }
    c["mode"]=singletons.contains("length")?"variable":"fixed"; c["frameSize"]=cursor;
    c["payloadOffset"]=dataStart; c["trailerBytes"]=suffix; c["checks"]=rules; c["fields"]=fields;
    c["commandMatches"]=matches; c["autoLayout"]=true; c["templateId"]="custom";
    BinaryConfig parsed; if(!BinaryConfig::fromMap(c,parsed,error)) return {};
    return parsed.toMap();
}
}
QVariantMap FrameStructure::compileMultiple(const QVariantList &blocks,const QVariantMap &base,QString &error) {
    auto result=compileSelected(blocks,base,error); if(result.isEmpty()) return {};
    // Validate every local choice without building an exponential cartesian product.
    // Each earlier data region has fixed size, so choices compose independently.
    for(int i=0;i<blocks.size();++i) {
        const auto command=blocks[i].toMap(); if(command.value("kind")!="command") continue;
        const auto messages=command.value("messages").toList(); QSet<quint64> seen;
        if(messages.isEmpty() || messages.size()>16) { error=QStringLiteral("每处命令字需要 1～16 个命令值"); return {}; }
        bool selected=false;
        for(const auto &value:messages) {
            const auto m=value.toMap(); const auto text=m.value("command").toString(); bool ok=false;
            const auto number=text.toULongLong(&ok,text.startsWith("0x",Qt::CaseInsensitive)?16:10);
            if(!ok || seen.contains(number) || m.value("name").toString().trimmed().isEmpty()) { error=QStringLiteral("命令值无效、重复或名称为空"); return {}; }
            seen.insert(number); selected|=number==command.value("value").toString().toULongLong(nullptr,command.value("value").toString().startsWith("0x",Qt::CaseInsensitive)?16:10);
            auto branch=blocks; auto b=command; b["value"]=text; branch[i]=b;
            for(int j=i+1;j<branch.size();++j) {
                auto data=branch[j].toMap(); if(data.value("kind")=="command") break;
                if(data.value("kind")!="payload") continue;
                const auto variants=data.value("variants").toMap();
                if(!variants.contains(text)) { error=QStringLiteral("命令 %1 缺少数据区字段配置").arg(text); return {}; }
                data["fields"]=variants.value(text); branch[j]=data;
            }
            auto candidate=compileSelected(branch,base,error); if(candidate.isEmpty()) return {};
            if(candidate.value("frameSize")!=result.value("frameSize")) {
                // A different prefix would shift subsequent command positions. Only the final
                // region can vary, and it must be delimited by the global length field.
                bool laterCommand=false;
                for(int j=i+1;j<blocks.size();++j) if(blocks[j].toMap().value("kind")=="command") laterCommand=true;
                if(laterCommand || result.value("mode")=="fixed") { error=QStringLiteral("前面的命令数据需保持相同字节数；不同长度的数据请放在最后并添加长度字段"); return {}; }
            }
        }
        if(!selected) { error=QStringLiteral("当前命令不在该模块的命令列表中"); return {}; }
    }
    return result;
}
