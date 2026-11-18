#include "checksumrule.h"
#include <cmath>
namespace {
quint32 reflected(quint32 value,int width) {
    quint32 result=0; for(int i=0;i<width;++i) { result=(result<<1)|(value&1); value>>=1; } return result;
}
bool unsignedParameter(const QVariant &v,quint32 &out) {
    bool ok=false; qulonglong n=0;
    if(v.metaType().id()==QMetaType::QString) {
        const auto s=v.toString().trimmed(); n=s.toULongLong(&ok,s.startsWith("0x",Qt::CaseInsensitive)?16:10);
    } else {
        const double x=v.toDouble(&ok); if(!ok || !std::isfinite(x) || x<0 || std::floor(x)!=x || x>4294967295.0) return false;
        n=qulonglong(x);
    }
    if(!ok || n>0xffffffffULL) return false; out=quint32(n); return true;
}
}
quint32 ChecksumRule::calculate(QByteArrayView data) const {
    const quint32 mask=quint32((quint64(1)<<width)-1);
    if(algorithm=="sum8" || algorithm=="sum16" || algorithm=="xor8") {
        quint32 result=0; for(char b:data) if(algorithm=="xor8") result^=quint8(b); else result+=quint8(b);
        return result & mask;
    }
    quint32 crc=initial;
    const quint32 p=reflectInput?reflected(polynomial,width):polynomial;
    for(char byte:data) {
        if(reflectInput) {
            crc^=quint8(byte);
            for(int i=0;i<8;++i) crc=(crc>>1)^((crc&1)?p:0);
        } else {
            crc^=quint32(quint8(byte))<<(width-8);
            for(int i=0;i<8;++i) crc=((crc<<1)^((crc & (quint32(1)<<(width-1)))?p:0)) & mask;
        }
    }
    if(reflectInput!=reflectOutput) crc=reflected(crc,width);
    return (crc^xorOut)&mask;
}
QVariantMap ChecksumRule::toMap() const {
    auto hex=[](quint32 n){return "0x"+QString::number(n,16).toUpper();};
    return {{"name",name},{"algorithm",algorithm},{"endian",endian},{"enabled",enabled},{"early",early},
        {"offset",offset},{"start",start},{"end",end},{"width",width},
        {"polynomial",hex(polynomial)},{"initial",hex(initial)},{"xorOut",hex(xorOut)},
        {"reflectInput",reflectInput},{"reflectOutput",reflectOutput}};
}
bool ChecksumRule::fromMap(const QVariantMap &m,ChecksumRule &out,QString &error) {
    ChecksumRule r; r.name=m.value("name",r.name).toString(); r.algorithm=m.value("algorithm",r.algorithm).toString();
    r.endian=m.value("endian",r.endian).toString(); r.enabled=m.value("enabled",true).toBool(); r.early=m.value("early",false).toBool();
    if(r.name.size()>64 || (r.endian!="little" && r.endian!="big")) { error=QStringLiteral("校验名称或字节序无效"); return false; }
    for(const auto &key:QStringList{"offset","start","end","width"}) {
        bool ok=false; const double x=m.value(key,key=="width"?16:key=="start"?0:-2).toDouble(&ok);
        if(!ok || !std::isfinite(x) || std::floor(x)!=x || x< -65536 || x>65536) { error=QStringLiteral("校验位置必须是整数"); return false; }
        if(key=="offset") r.offset=int(x); else if(key=="start") r.start=int(x); else if(key=="end") r.end=int(x); else r.width=int(x);
    }
    r.reflectInput=m.value("reflectInput",true).toBool(); r.reflectOutput=m.value("reflectOutput",true).toBool();
    // Old saved templates are read as explicit custom CRCs, not project presets.
    if(r.algorithm=="crc8dji") { r.algorithm="crc"; r.width=8; r.polynomial=0x31; r.initial=0xff; r.xorOut=0; r.reflectInput=r.reflectOutput=true; }
    else if(r.algorithm=="crc16dji") { r.algorithm="crc"; r.width=16; r.polynomial=0x1021; r.initial=0xffff; r.xorOut=0; r.reflectInput=r.reflectOutput=true; }
    else if(r.algorithm=="crc8smbus") { r.width=8; r.polynomial=0x07; r.initial=0; r.xorOut=0; r.reflectInput=r.reflectOutput=false; }
    else if(r.algorithm=="crc8maximdow") { r.width=8; r.polynomial=0x31; r.initial=0; r.xorOut=0; r.reflectInput=r.reflectOutput=true; }
    else if(r.algorithm=="crc16modbus") { r.width=16; r.polynomial=0x8005; r.initial=0xffff; r.xorOut=0; r.reflectInput=r.reflectOutput=true; }
    else if(r.algorithm=="crc16ccittfalse") { r.width=16; r.polynomial=0x1021; r.initial=0xffff; r.xorOut=0; r.reflectInput=r.reflectOutput=false; }
    else if(r.algorithm=="crc32isohdlc") { r.width=32; r.polynomial=0x04c11db7; r.initial=0xffffffff; r.xorOut=0xffffffff; r.reflectInput=r.reflectOutput=true; }
    else if(r.algorithm=="sum8" || r.algorithm=="xor8") r.width=8;
    else if(r.algorithm=="sum16") r.width=16;
    else if(r.algorithm=="crc") {
        if(!unsignedParameter(m.value("polynomial","0x1021"),r.polynomial) || !unsignedParameter(m.value("initial","0xffff"),r.initial)
            || !unsignedParameter(m.value("xorOut","0x0"),r.xorOut)) { error=QStringLiteral("CRC 参数请输入整数或 0x 开头的十六进制"); return false; }
    } else { error=QStringLiteral("不支持的校验算法"); return false; }
    if(r.width!=8 && r.width!=16 && r.width!=32) { error=QStringLiteral("CRC 位宽只能是 8、16 或 32"); return false; }
    const quint64 mask=(quint64(1)<<r.width)-1;
    if(r.algorithm=="crc" && (!r.polynomial || r.polynomial>mask || r.initial>mask || r.xorOut>mask)) { error=QStringLiteral("CRC 参数超出所选位宽"); return false; }
    if(r.early && (r.offset<0 || r.start<0 || r.end<0)) { error=QStringLiteral("提前校验只能使用帧起点的非负偏移"); return false; }
    out=r; return true;
}
