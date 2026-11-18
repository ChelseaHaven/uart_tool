#include "binaryconfig.h"
#include <QRegularExpression>
#include <QSet>
#include <cmath>
namespace {
bool integer(const QVariantMap &m, const char *key, int &out, int low, int high, QString &error) {
    bool ok=false; const double value=m.value(key,out).toDouble(&ok);
    if(!ok || !std::isfinite(value) || std::floor(value)!=value || value<low || value>high) {
        error=QStringLiteral("%1 必须是 %2～%3 的整数").arg(key).arg(low).arg(high); return false;
    }
    out=int(value); return true;
}
bool order(const QString &s) { return s=="little" || s=="big"; }
}
int BinaryConfig::typeBytes(const QString &type) {
    if(type=="uint8" || type=="int8" || type=="bytes" || type=="padding") return 1;
    if(type=="uint16" || type=="int16") return 2;
    if(type=="uint32" || type=="int32" || type=="float32") return 4;
    if(type=="uint64" || type=="int64" || type=="float64") return 8;
    return 0;
}
int BinaryConfig::legacyChecksumBytes() const {
    if(checksum=="none") return 0;
    return checksum=="sum16" || checksum=="crc16modbus" ? 2 : 1;
}
int BinaryConfig::checksumBytes() const { return trailerBytes>=0?trailerBytes:legacyChecksumBytes(); }
QVariantMap BinaryConfig::toMap() const {
    QVariantList rows;
    for(const auto &f:fields) rows.append(QVariantMap{{"name",f.name},{"type",f.type},{"offset",f.offset},
        {"count",f.count},{"variable",f.variable},{"endian",f.endian}});
    QVariantList rules,layouts;
    for(const auto &rule:checks) rules.append(rule.toMap());
    for(const auto &message:messages) {
        QVariantList fields;
        for(const auto &f:message.fields) fields.append(QVariantMap{{"name",f.name},{"type",f.type},{"offset",f.offset},{"count",f.count},{"variable",f.variable},{"endian",f.endian}});
        layouts.append(QVariantMap{{"command","0x"+QString::number(message.command,16).toUpper()},{"name",message.name},{"payloadSize",message.payloadSize},{"fields",fields}});
    }
    return {{"commandMatches",commandMatches},{"tail",QString::fromLatin1(tail.toHex(' ').toUpper())},{"structure",structure},{"trailerBytes",trailerBytes},{"checks",rules},{"commandOffset",commandOffset},{"commandBytes",commandBytes},
        {"commandEndian",commandEndian},{"messages",layouts},{"commandValue","0x"+QString::number(commandValue,16).toUpper()},{"templateId",templateId},{"autoLayout",autoLayout},{"header",QString::fromLatin1(header.toHex(' ').toUpper())},{"mode",mode},
        {"frameSize",frameSize},{"maxFrameSize",maxFrameSize},{"lengthOffset",lengthOffset},
        {"lengthBytes",lengthBytes},{"lengthEndian",lengthEndian},{"lengthMeaning",lengthMeaning},
        {"payloadOffset",payloadOffset},{"lengthIncludesChecksum",lengthIncludesChecksum},
        {"checksum",checksum},{"checksumEndian",checksumEndian},{"checksumStart",checksumStart},
        {"checksumEnd",checksumEnd},{"fields",rows}};
}
QVariantMap BinaryConfig::singleCommandConfiguration(const QVariantMap &map) {
    auto result=map; const auto messages=map.value("messages").toList();
    if(!messages.isEmpty()) {
        auto message=messages.first().toMap();
        const auto selected=map.value("commandValue").toString();
        bool selectedOk=false; const auto selectedValue=selected.toULongLong(&selectedOk,selected.startsWith("0x",Qt::CaseInsensitive)?16:10);
        if(selectedOk) for(const auto &value:messages) {
            const auto row=value.toMap(); const auto command=row.value("command").toString(); bool ok=false;
            if(command.toULongLong(&ok,command.startsWith("0x",Qt::CaseInsensitive)?16:10)==selectedValue && ok) { message=row; break; }
        }
        QVariantList fields;
        for(const auto &value:map.value("fields").toList())
            if(value.toMap().value("offset").toInt()<map.value("payloadOffset").toInt()) fields.append(value);
        fields+=message.value("fields").toList(); result["fields"]=fields; result["commandValue"]=message.value("command");
        auto blocks=map.value("structure").toList();
        for(auto &value:blocks) { auto block=value.toMap(); if(block.value("kind")=="command") { block["value"]=message.value("command"); value=block; } }
        result["structure"]=blocks;
    }
    result.remove("messages"); return result;
}
bool BinaryConfig::fromMap(const QVariantMap &input, BinaryConfig &out, QString &error) {
    const auto &m=input;
    BinaryConfig c; error.clear();
    QString header=m.value("header").toString(); header.remove(QRegularExpression("\\s"));
    if(header.isEmpty() || header.size()>32 || header.size()%2 || !QRegularExpression("^[0-9a-fA-F]+$").match(header).hasMatch()) {
        error=QStringLiteral("帧头请输入 1～16 字节的 HEX，例如 AA 55"); return false;
    }
    c.header=QByteArray::fromHex(header.toLatin1()); c.mode=m.value("mode","fixed").toString();
    QString tail=m.value("tail").toString(); tail.remove(QRegularExpression("\\s"));
    if(tail.size()>32 || tail.size()%2 || (!tail.isEmpty() && !QRegularExpression("^[0-9a-fA-F]+$").match(tail).hasMatch())) {
        error=QStringLiteral("帧尾必须是最多 16 字节的 HEX"); return false;
    }
    c.tail=QByteArray::fromHex(tail.toLatin1()); c.structure=m.value("structure").toList();
    c.lengthEndian=m.value("lengthEndian","little").toString(); c.lengthMeaning=m.value("lengthMeaning","payload").toString();
    c.checksum=m.value("checksum","none").toString(); c.checksumEndian=m.value("checksumEndian","little").toString();
    if(!c.tail.isEmpty() && c.checksum!="none") { error=QStringLiteral("固定帧尾应配合独立校验规则使用"); return false; }
    c.lengthIncludesChecksum=m.value("lengthIncludesChecksum",false).toBool();
    c.templateId=m.value("templateId","custom").toString(); c.autoLayout=m.value("autoLayout",false).toBool();
    c.commandEndian=m.value("commandEndian","little").toString();
    if(!integer(m,"trailerBytes",c.trailerBytes,-1,16,error) || !integer(m,"commandOffset",c.commandOffset,-1,65535,error)
        || !integer(m,"commandBytes",c.commandBytes,1,4,error)) return false;
    if((c.mode!="fixed" && c.mode!="variable") || !order(c.lengthEndian) || !order(c.checksumEndian)
        || (c.lengthMeaning!="payload" && c.lengthMeaning!="frame")) { error=QStringLiteral("帧模式、长度规则或大小端无效"); return false; }
    if(!QStringList{"none","sum8","sum16","xor8","crc16modbus"}.contains(c.checksum)) { error=QStringLiteral("不支持的校验算法"); return false; }
    if(!integer(m,"frameSize",c.frameSize,1,65536,error) || !integer(m,"maxFrameSize",c.maxFrameSize,1,65536,error)
        || !integer(m,"lengthOffset",c.lengthOffset,0,65535,error) || !integer(m,"lengthBytes",c.lengthBytes,1,4,error)
        || !integer(m,"payloadOffset",c.payloadOffset,0,65535,error) || !integer(m,"checksumStart",c.checksumStart,0,65535,error)
        || !integer(m,"checksumEnd",c.checksumEnd,-1,65536,error)) return false;
    if(c.mode=="variable" && (c.lengthBytes==3 || c.lengthOffset<c.header.size()
        || c.lengthOffset+c.lengthBytes>c.maxFrameSize-c.checksumBytes()
        || c.payloadOffset<c.lengthOffset+c.lengthBytes || c.payloadOffset>c.maxFrameSize-c.checksumBytes())) {
        error=QStringLiteral("长度字段应在帧头后，宽度为 1/2/4 字节；数据区应在长度字段之后"); return false;
    }
    const int limit=(c.mode=="fixed" ? c.frameSize : c.maxFrameSize)-c.checksumBytes();
    if(c.tail.size()>c.checksumBytes()) { error=QStringLiteral("帧尾未包含在保留字节中"); return false; }
    if((c.mode=="fixed" && c.frameSize>c.maxFrameSize) || limit<c.header.size()) { error=QStringLiteral("帧长度不足或超过最大帧长"); return false; }
    if(c.checksum!="none" && (c.checksumBytes()<c.legacyChecksumBytes() || c.checksumStart>=limit || (c.checksumEnd!=-1 && (c.checksumEnd<=c.checksumStart || c.checksumEnd>limit)))) {
        error=QStringLiteral("校验覆盖范围必须在校验字节之前，结束位置不包含在范围内"); return false;
    }
    const auto rows=m.value("fields").toList();
    if(rows.isEmpty() || rows.size()>64) { error=QStringLiteral("请配置 1～64 个字段"); return false; }
    c.fields.clear(); QSet<QString> names; QVector<QPair<int,int>> occupied;
    for(qsizetype i=0;i<rows.size();++i) {
        const auto row=rows[i].toMap(); BinaryField f;
        f.name=row.value("name").toString().trimmed(); f.type=row.value("type").toString();
        f.endian=row.value("endian","little").toString(); f.variable=row.value("variable",false).toBool();
        if(f.name.isEmpty() || f.name.size()>64 || names.contains(f.name) || !typeBytes(f.type) || !order(f.endian)) {
            error=QStringLiteral("字段名称不能为空或重复；字段类型和大小端必须有效"); return false;
        }
        names.insert(f.name);
        if(!integer(row,"offset",f.offset,int(c.header.size()),65535,error) || !integer(row,"count",f.count,1,4096,error)) return false;
        if(f.variable && (c.mode!="variable" || i!=rows.size()-1)) { error=QStringLiteral("变长字段只能位于变长帧的最后一个字段"); return false; }
        const int end=f.variable ? limit : f.offset+typeBytes(f.type)*f.count;
        if(c.mode=="variable" && f.offset<c.lengthOffset+c.lengthBytes && end>c.lengthOffset) {
            error=QStringLiteral("数据字段与长度字段重叠，请调整数据字段偏移"); return false;
        }
        if(f.offset>=limit || end>limit) { error=QStringLiteral("字段 %1 超出数据范围或占用了校验字节").arg(f.name); return false; }
        if(f.variable && f.offset<c.payloadOffset) { error=QStringLiteral("变长字段不能早于数据区起点"); return false; }
        for(const auto &range:occupied) if(f.offset<range.second && end>range.first) { error=QStringLiteral("字段字节范围重叠"); return false; }
        occupied.append({f.offset,end}); c.fields.append(f);
    }
    const int bound=c.mode=="fixed"?c.frameSize:c.maxFrameSize;
    if(m.value("checks").toList().size()>8) { error=QStringLiteral("最多配置 8 条校验规则"); return false; }
    for(const auto &value:m.value("checks").toList()) {
        ChecksumRule rule;
        if(!ChecksumRule::fromMap(value.toMap(),rule,error)) return false;
        const int position=rule.offset<0?bound+rule.offset:rule.offset;
        const int start=rule.start<0?bound+rule.start:rule.start;
        const int end=rule.end<0?bound+rule.end:rule.end;
        if(position<0 || position+rule.bytes()>bound-c.tail.size() || start<0 || end>bound || start>=end
            || (start<position+rule.bytes() && end>position)
            || (rule.offset<0 && (c.trailerBytes<0 || -rule.offset>c.trailerBytes))
            || (rule.early && position+rule.bytes()>(c.mode=="variable"?c.payloadOffset:limit))) {
            error=QStringLiteral("校验范围越界、覆盖了自身，或提前校验超出了固定前缀"); return false;
        }
        c.checks.append(rule);
    }
    if(c.commandOffset>=0) {
        if(c.commandOffset<c.header.size() || c.commandBytes==3 || !order(c.commandEndian)
            || c.commandOffset+c.commandBytes>c.payloadOffset
            || (c.mode=="variable" && c.commandOffset<c.lengthOffset+c.lengthBytes && c.commandOffset+c.commandBytes>c.lengthOffset)) {
            error=QStringLiteral("命令字位置、宽度或字节序无效"); return false;
        }
        bool ok=false; const auto command=m.value("commandValue","0").toString().trimmed();
        const auto number=command.toULongLong(&ok,command.startsWith("0x",Qt::CaseInsensitive)?16:10);
        if(!ok || number>((quint64(1)<<(c.commandBytes*8))-1)) { error=QStringLiteral("命令字值超出所选字节数，可输入十进制或 0x 开头的十六进制"); return false; }
        c.commandValue=quint32(number);
    }
    const auto matches=m.value("commandMatches").toList();
    if(matches.size()>24) { error=QStringLiteral("命令字模块过多"); return false; }
    for(const auto &value:matches) {
        const auto match=value.toMap(); int offset=0,bytes=1;
        if(!integer(match,"offset",offset,int(c.header.size()),limit-1,error)
            || !integer(match,"bytes",bytes,1,4,error)) return false;
        bool ok=false; const auto text=match.value("value").toString();
        const auto number=text.toULongLong(&ok,text.startsWith("0x",Qt::CaseInsensitive)?16:10);
        if(bytes==3 || offset+bytes>limit || !order(match.value("endian").toString()) || !ok || number>=(quint64(1)<<(8*bytes))) {
            error=QStringLiteral("命令字匹配位置、宽度或数值无效"); return false;
        }
        c.commandMatches.append(QVariantMap{{"offset",offset},{"bytes",bytes},{"endian",match.value("endian")},{"value",text}});
    }
    const auto layouts=m.value("messages").toList();
    if(layouts.size()>16) { error=QStringLiteral("最多配置 16 个命令"); return false; }
    if(!layouts.isEmpty() && c.commandOffset<0) { error=QStringLiteral("请在帧结构中添加命令字"); return false; }
    QSet<quint32> commands;
    for(const auto &value:layouts) {
        const auto row=value.toMap(); MessageLayout message;
        bool ok=false; const auto command=row.value("command").toString().trimmed();
        const auto number=command.toULongLong(&ok,command.startsWith("0x",Qt::CaseInsensitive)?16:10);
        if(!ok || number>((quint64(1)<<(c.commandBytes*8))-1) || commands.contains(quint32(number))) { error=QStringLiteral("命令字无效、重复或超过所选宽度"); return false; }
        message.command=quint32(number); commands.insert(message.command); message.name=row.value("name",QStringLiteral("命令 %1").arg(command)).toString().trimmed();
        if(message.name.isEmpty() || message.name.size()>80 || !integer(row,"payloadSize",message.payloadSize,-1,65536,error)) { error=QStringLiteral("命令名称或数据长度无效"); return false; }
        auto branch=m; QVariantList combined;
        const auto dataFields=row.value("fields").toList();
        if(dataFields.isEmpty()) { error=QStringLiteral("每个命令至少需要一个数据字段"); return false; }
        for(const auto &field:dataFields) if(field.toMap().value("offset").toInt()<c.payloadOffset) {
            error=QStringLiteral("命令数据字段不能位于数据区之前"); return false;
        }
        for(const auto &value:m.value("fields").toList()) if(value.toMap().value("offset").toInt()<c.payloadOffset) combined.append(value);
        combined+=dataFields;
        branch["messages"]=QVariantList{}; branch["fields"]=combined; branch["commandValue"]=command;
        BinaryConfig parsed;
        if(!BinaryConfig::fromMap(branch,parsed,error)) return false;
        for(const auto &field:parsed.fields) {
            if(field.offset<c.payloadOffset) continue;
            if(message.payloadSize>=0 && field.offset+typeBytes(field.type)*field.count>c.payloadOffset+message.payloadSize) {
                error=QStringLiteral("命令的数据字段必须位于数据区内"); return false;
            }
            message.fields.append(field);
        }
        if(message.payloadSize>=0 && c.payloadOffset+message.payloadSize+c.checksumBytes()>bound) { error=QStringLiteral("命令数据超过最大帧长"); return false; }
        c.messages.append(message);
    }
    if(!c.messages.isEmpty() && !commands.contains(c.commandValue)) c.commandValue=c.messages.first().command;
    out=c; return true;
}
