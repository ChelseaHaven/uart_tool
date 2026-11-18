#include "protocols/protocolparser.h"
#include "application/protocolsession.h"
#include "protocols/framestructure.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QDir>
#include <QSettings>
#include <cstring>
namespace {
QByteArray numberBytes(quint64 value,int width,bool little=true) {
    QByteArray bytes(width,0);
    for(int i=0;i<width;++i) { bytes[little?i:width-1-i]=char(value&255); value>>=8; }
    return bytes;
}
QByteArray floatBytes(float value) { quint32 bits; std::memcpy(&bits,&value,4); return numberBytes(bits,4); }
BinaryConfig valid(const QVariantMap &map) {
    BinaryConfig config; QString error;
    if(!BinaryConfig::fromMap(map,config,error)) qFatal("Invalid test config: %s",qPrintable(error));
    return config;
}
QByteArray makeFrame(const BinaryConfig &c,const QByteArray &payload) {
    QByteArray bytes;
    if(c.mode=="fixed") { bytes=c.header; bytes+=payload; }
    else {
        bytes=QByteArray(c.payloadOffset,0); bytes.replace(0,c.header.size(),c.header); bytes+=payload;
        const int total=int(bytes.size())+c.checksumBytes();
        const int length=(c.lengthMeaning=="frame"?total:total-c.payloadOffset)-(c.lengthIncludesChecksum?0:c.checksumBytes());
        bytes.replace(c.lengthOffset,c.lengthBytes,numberBytes(quint64(length),c.lengthBytes,c.lengthEndian=="little"));
    }
    if(c.checksumBytes()) {
        const int end=c.checksumEnd<0?int(bytes.size()):c.checksumEnd;
        const auto covered=QByteArrayView(bytes).sliced(c.checksumStart,end-c.checksumStart);
        quint64 checksum=0;
        if(c.checksum=="crc16modbus") checksum=modbusCrc16(covered);
        else for(char byte:covered) if(c.checksum=="xor8") checksum^=quint8(byte); else checksum+=quint8(byte);
        bytes+=numberBytes(checksum,c.checksumBytes(),c.checksumEndian=="little");
    }
    return bytes;
}
QVariantMap variableConfig(const QString &type="float32") {
    auto map=BinaryConfig().toMap(); map["mode"]="variable"; map["checksum"]="crc16modbus";
    map["fields"]=QVariantList{QVariantMap{{"name","samples"},{"type",type},{"offset",4},{"count",1},{"variable",true},{"endian","little"}}};
    return map;
}
}
class ProtocolParserTests : public QObject {
    Q_OBJECT
private slots:
    void init() { QSettings store; store.clear(); store.sync(); QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively(); }
    void selectedCommandOnly() {
        ProtocolSession session; QVERIFY(session.applyBinaryConfiguration(BinaryConfig::preset("robot_tennis")));
        QVERIFY(session.selectProtocol("binary")); QCOMPARE(session.binaryChannels().size(),2);
        QCOMPARE(session.selectedBinaryChannelId(),QString("0x101"));
        const auto wheels=QByteArray::fromHex("a51000114a01010000803f0000004000004040000080403500");
        const auto imu=QByteArray::fromHex("a52400110a02010000803f0000004000004040000080400000a0400000c0400000e04000000041000010417984");
        session.feedBytes(wheels+imu); QTRY_COMPARE(session.latestFields().size(),6);
        QCOMPARE(session.latestFields().last().toMap()["value"].toDouble(),4.0); QCOMPARE(session.frameCount(),1ULL);
        const auto displayed=session.latestFields(); QSignalSpy changes(&session,&ProtocolSession::latestFrameChanged);
        session.feedBytes(imu+imu); QTest::qWait(70);
        QCOMPARE(session.latestFields(),displayed); QCOMPARE(changes.size(),0); QCOMPARE(session.frameCount(),1ULL);
        QVERIFY(session.selectBinaryChannel("0x102")); QVERIFY(session.latestFields().isEmpty()); QCOMPARE(session.frameCount(),0ULL);
        session.feedBytes(wheels); QTest::qWait(60); QVERIFY(session.latestFields().isEmpty());
        session.feedBytes(imu); QTRY_COMPARE(session.latestFields().size(),11);
        QCOMPARE(session.latestFields().last().toMap()["value"].toDouble(),9.0); QCOMPARE(session.frameCount(),1ULL);
        QVERIFY(session.selectBinaryChannel("0x101")); QVERIFY(session.latestFields().isEmpty());
        session.feedBytes(wheels); QTRY_COMPARE(session.latestFields().size(),6); QCOMPARE(session.latestFields(),displayed);
        QVERIFY(!session.selectBinaryChannel("unknown")); QCOMPARE(session.latestFields(),displayed);
        QCOMPARE(session.invalidFrames(),0ULL);
        session.reset(); QVERIFY(session.latestFields().isEmpty());
        QVERIFY(session.selectBinaryChannel("0x102")); QVERIFY(session.latestFields().isEmpty()); QCOMPARE(session.frameCount(),0ULL);
    }
    void genericFrameStructures() {
        QString error;
        auto block=[](const char *kind){return QVariantMap{{"kind",kind}};};
        auto header=block("header"); header["hex"]="5A C3";
        auto sequence=block("field"); sequence["name"]="seq"; sequence["type"]="uint8";
        auto headCheck=block("check"); headCheck["rule"]=QVariantMap{{"name","prefix XOR"},{"algorithm","xor8"}};
        auto tailCheck=block("check"); tailCheck["rule"]=QVariantMap{{"name","data CRC32"},{"algorithm","crc32isohdlc"},{"endian","big"}};
        tailCheck["coverage"]="payload";
        auto tail=block("tail"); tail["hex"]="0D 0A";
        QVariantList blocks{header,sequence,headCheck,block("payload"),tailCheck,tail};
        auto base=BinaryConfig::preset("simple_fixed");
        base["fields"]=QVariantList{QVariantMap{{"name","values"},{"type","int16"},{"count",2},{"endian","big"}}};
        auto map=FrameStructure::compile(blocks,base,error); QVERIFY2(!map.isEmpty(),qPrintable(error));
        auto c=valid(map);
        QCOMPARE(c.payloadOffset,4); QCOMPARE(c.frameSize,14); QCOMPARE(c.trailerBytes,6);
        QCOMPARE(c.checks[0].offset,3); QCOMPARE(c.checks[1].offset,-6); QCOMPARE(c.checks[1].start,4);
        QCOMPARE(c.tail,QByteArray::fromHex("0d0a")); QCOMPARE(c.structure,blocks);
        auto encode=[](const BinaryConfig &c,QByteArray bytes) {
            for(const auto &rule:c.checks) {
                const int at=rule.offset<0?int(bytes.size())+rule.offset:rule.offset;
                const int end=rule.end<0?int(bytes.size())+rule.end:rule.end;
                bytes.replace(at,rule.bytes(),numberBytes(rule.calculate(QByteArrayView(bytes).sliced(rule.start,end-rule.start)),rule.bytes(),rule.endian=="little"));
            }
            return bytes;
        };
        const auto wire=encode(c,QByteArray::fromHex("5ac307000102fffe000000000d0a"));
        auto parser=makeProtocolParser("binary",c); QVector<DecodedFrame> frames;
        auto sink=[&](DecodedFrame f){frames.append(std::move(f));};
        for(char b:wire) parser->feed(QByteArray(1,b),sink);
        QCOMPARE(frames.size(),1); QCOMPARE(frames[0].fields[0].value.toUInt(),7U);
        const auto values=frames[0].fields[1].value.toList(); QCOMPARE(values[0].toInt(),258); QCOMPARE(values[1].toInt(),-2);
        auto damaged=wire; damaged[13]=0; parser->feed(damaged+wire,sink); QCOMPARE(frames.size(),2);
        damaged=wire; damaged[5]^=1; parser->feed(damaged+wire,sink); QCOMPARE(frames.size(),3);
        // Recompiling a saved structure does not duplicate the generated public fields.
        auto again=FrameStructure::compile(blocks,map,error); QVERIFY2(!again.isEmpty(),qPrintable(error)); QCOMPARE(again,map);
        auto disabled=blocks; auto b=disabled[4].toMap(); auto r=b["rule"].toMap(); r["enabled"]=false; b["rule"]=r; disabled[4]=b;
        const auto unchecked=valid(FrameStructure::compile(disabled,map,error)); QCOMPARE(unchecked.frameSize,14);
        frames.clear(); parser=makeProtocolParser("binary",unchecked); parser->feed(damaged,sink); QCOMPARE(frames.size(),1);
        // A different layout: command before a big-endian byte length, variable uint16 array,
        // then SUM8 and a literal terminator. No engineering-specific code is involved.
        auto command=block("command"); command["bytes"]=1; command["value"]="9"; command["endian"]="big";
        auto length=block("length"); length["bytes"]=2; length["endian"]="big";
        auto sum=block("check"); sum["rule"]=QVariantMap{{"name","sum"},{"algorithm","sum8"}};
        blocks={header,command,length,block("payload"),sum,tail};
        base=BinaryConfig::preset("simple_variable");
        base["fields"]=QVariantList{QVariantMap{{"name","samples"},{"type","uint16"},{"count",1},{"variable",true},{"endian","big"}}};
        map=FrameStructure::compile(blocks,base,error); QVERIFY2(!map.isEmpty(),qPrintable(error)); c=valid(map);
        QCOMPARE(c.commandOffset,2); QCOMPARE(c.lengthOffset,3); QCOMPARE(c.payloadOffset,5); QCOMPARE(c.trailerBytes,3);
        const auto packet=encode(c,QByteArray::fromHex("5ac309000400010002000d0a"));
        frames.clear(); parser=makeProtocolParser("binary",c); parser->feed(packet+packet,sink); QCOMPARE(frames.size(),2);
        QCOMPARE(frames.last().fields.last().value.toList(),QVariantList({QVariant::fromValue(quint64(1)),QVariant::fromValue(quint64(2))}));
        // Message branches use exactly the same structure compiler.
        base=map;
        base["messages"]=QVariantList{QVariantMap{{"command","9"},{"name","sensor"},{"payloadSize",4},{"fields",QVariantList{QVariantMap{{"name","reading"},{"type","uint32"},{"count",1},{"endian","big"}}}}}};
        map=FrameStructure::compile(blocks,base,error); QVERIFY2(!map.isEmpty(),qPrintable(error));
        frames.clear(); parser=makeProtocolParser("binary",valid(map)); parser->feed(packet,sink);
        QCOMPARE(frames.size(),1); QCOMPARE(frames[0].fields.last().value.toUInt(),65538U);
        ProtocolSession session; QVERIFY(session.applyBinaryConfiguration(map)); ProtocolSession restored;
        QCOMPARE(restored.binaryConfiguration(),map);
        auto fixedBlocks=blocks; fixedBlocks.removeAt(2); // commands do not inherently require a length field
        auto fixed=FrameStructure::compile(fixedBlocks,map,error); QVERIFY2(!fixed.isEmpty(),qPrintable(error));
        QCOMPARE(fixed["mode"].toString(),QString("fixed")); QCOMPARE(fixed["frameSize"].toInt(),10);
        frames.clear(); const auto fixedConfig=valid(fixed); parser=makeProtocolParser("binary",fixedConfig);
        parser->feed(encode(fixedConfig,QByteArray::fromHex("5ac30900010002000d0a")),sink);
        QCOMPARE(frames.size(),1); QCOMPARE(frames[0].fields.last().value.toUInt(),65538U);
        auto invalid=blocks; invalid.insert(2,length); QVERIFY(FrameStructure::compile(invalid,base,error).isEmpty());
        invalid=blocks; invalid.move(2,4); QVERIFY(FrameStructure::compile(invalid,base,error).isEmpty());
        invalid=blocks; auto bad=invalid[4].toMap(); bad["coverage"]="unknown"; invalid[4]=bad;
        QVERIFY(FrameStructure::compile(invalid,base,error).isEmpty());
        ChecksumRule crc;
        QVERIFY(ChecksumRule::fromMap({{"algorithm","crc16ccittfalse"}},crc,error)); QCOMPARE(crc.calculate(QByteArrayView("123456789",9)),0x29b1U);
        QVERIFY(ChecksumRule::fromMap({{"algorithm","crc32isohdlc"}},crc,error)); QCOMPARE(crc.calculate(QByteArrayView("123456789",9)),0xcbf43926U);
    }
    void robotPresetAndGenericCrc() {
        const auto c=valid(BinaryConfig::preset("robot_tennis"));
        QCOMPARE(c.checksumBytes(),2); QCOMPARE(c.checks.size(),2); QCOMPARE(c.messages.size(),2);
        QCOMPARE(c.checks[0].calculate(QByteArrayView("123456789",9)),quint32(0x0b));
        QCOMPARE(c.checks[1].calculate(QByteArrayView("123456789",9)),quint32(0x6f91));
        const auto wheels=QByteArray::fromHex("a51000114a01010000803f0000004000004040000080403500");
        const auto imu=QByteArray::fromHex("a52400110a02010000803f0000004000004040000080400000a0400000c0400000e04000000041000010417984");
        auto parser=makeProtocolParser("binary",c); QVector<DecodedFrame> frames;
        auto sink=[&](DecodedFrame f){frames.append(std::move(f));};
        parser->feed(wheels.first(4),sink); QVERIFY(frames.isEmpty());
        parser->feed(wheels.sliced(4)+imu,sink); QCOMPARE(frames.size(),1);
        QCOMPARE(frames[0].fields.size(),6); QCOMPARE(frames[0].fields.last().value.toDouble(),4.0);
        auto map=c.toMap(); map["commandValue"]="0x102"; frames.clear(); parser=makeProtocolParser("binary",valid(map));
        parser->feed(wheels+imu,sink); QCOMPARE(frames.size(),1); QCOMPARE(frames[0].fields.size(),11);
        QCOMPARE(frames[0].fields.last().name,QString("mag_z")); QCOMPARE(frames[0].fields.last().value.toDouble(),9.0);
        auto bad=imu; bad[44]^=1; parser->feed(bad+imu,sink); QCOMPARE(frames.size(),2); QVERIFY(parser->statistics().invalidFrames>0);
        auto unknown=wheels; unknown[5]=0x34; unknown[6]=0x12;
        const auto crc=c.checks[1].calculate(QByteArrayView(unknown).first(unknown.size()-2));
        unknown[unknown.size()-2]=char(crc&255); unknown[unknown.size()-1]=char(crc>>8);
        const auto errors=parser->statistics().invalidFrames; parser->feed(unknown,sink);
        QCOMPARE(frames.size(),2); QCOMPARE(parser->statistics().invalidFrames,errors);
        auto custom=c.checks[1].toMap(); custom["algorithm"]="crc"; ChecksumRule rule; QString error;
        QVERIFY(ChecksumRule::fromMap(custom,rule,error)); QCOMPARE(rule.calculate(QByteArrayView("123456789",9)),quint32(0x6f91));
        custom["width"]=32; custom["polynomial"]="0x04c11db7"; custom["initial"]="0xffffffff"; custom["xorOut"]="0xffffffff";
        QVERIFY(ChecksumRule::fromMap(custom,rule,error)); QCOMPARE(rule.calculate(QByteArrayView("123456789",9)),quint32(0xcbf43926));
        custom["width"]=8; QVERIFY(!ChecksumRule::fromMap(custom,rule,error));
    }
    void crcKnownVector() { QCOMPARE(modbusCrc16(QByteArrayView("123456789",9)),quint16(0x4b37)); }
    void standardCrcPresets() {
        ChecksumRule rule; QString error;
        QVERIFY(ChecksumRule::fromMap({{"algorithm","crc8smbus"}},rule,error));
        QCOMPARE(rule.width,8); QCOMPARE(rule.initial,0U); QVERIFY(!rule.reflectInput);
        QCOMPARE(rule.calculate(QByteArrayView("123456789",9)),0xf4U);
        QVERIFY(ChecksumRule::fromMap({{"algorithm","crc8maximdow"}},rule,error));
        QCOMPARE(rule.width,8); QCOMPARE(rule.initial,0U); QVERIFY(rule.reflectInput);
        QCOMPARE(rule.calculate(QByteArrayView("123456789",9)),0xa1U);
        QVERIFY(ChecksumRule::fromMap({{"algorithm","crc8dji"}},rule,error));
        QCOMPARE(rule.algorithm,QString("crc")); QCOMPARE(rule.calculate(QByteArrayView("123456789",9)),0x0bU);
        QVERIFY(ChecksumRule::fromMap(rule.toMap(),rule,error)); QCOMPARE(rule.calculate(QByteArrayView("123456789",9)),0x0bU);
        QVERIFY(ChecksumRule::fromMap({{"algorithm","crc16dji"}},rule,error));
        QCOMPARE(rule.algorithm,QString("crc")); QCOMPARE(rule.calculate(QByteArrayView("123456789",9)),0x6f91U);
        const auto robot=valid(BinaryConfig::preset("robot_tennis"));
        QCOMPARE(robot.checks[0].algorithm,QString("crc")); QCOMPARE(robot.checks[1].algorithm,QString("crc"));
    }
    void reservedPayloadBytes() {
        auto map=BinaryConfig().toMap(); map["frameSize"]=8;
        map["fields"]=QVariantList{QVariantMap{{"name","reserved"},{"type","padding"},{"offset",2},{"count",2}},
            QVariantMap{{"name","value"},{"type","float32"},{"offset",4},{"count",1}}};
        auto parser=makeProtocolParser("binary",valid(map)); QVector<DecodedFrame> frames;
        parser->feed(QByteArray::fromHex("aa55ffff0000c03f"),[&](DecodedFrame f){frames.append(std::move(f));});
        QCOMPARE(frames.size(),1); QCOMPARE(frames[0].fields.size(),1); QCOMPARE(frames[0].fields[0].value.toDouble(),1.5);
    }
    void keyValueText() {
        auto parser=makeProtocolParser("keyvalue",BinaryConfig()); QVERIFY(parser);
        QVector<DecodedFrame> frames; auto sink=[&](DecodedFrame f){frames.append(std::move(f));};
        parser->feed("awa:2.0,   aaa",sink); QVERIFY(frames.isEmpty());
        parser->feed(":3\n",sink); QCOMPARE(frames.size(),1); QCOMPARE(frames[0].fields.size(),2);
        QCOMPARE(frames[0].fields[0].name,QString("awa")); QCOMPARE(frames[0].fields[0].value.toDouble(),2.0);
        QCOMPARE(frames[0].fields[1].name,QString("aaa")); QCOMPARE(frames[0].fields[1].value.toDouble(),3.0);
        const auto unicode=QStringLiteral("  温度 : -2.5,   电流: 1.2e-3  \r\n").toUtf8();
        for(char byte:unicode) parser->feed(QByteArray(1,byte),sink);
        QCOMPARE(frames.size(),2); QCOMPARE(frames.last().fields[0].name,QStringLiteral("温度"));
        QCOMPARE(frames.last().fields[0].value.toDouble(),-2.5); QCOMPARE(frames.last().fields[1].value.toDouble(),0.0012);
        parser->feed("\n  \r\na:1,a:2\nname:NaN\nname:inf\nempty:\n:3\nname:1:2\nname:text\n",sink);
        QCOMPARE(frames.size(),2); QCOMPARE(parser->statistics().invalidFrames,7ULL);
        parser->feed(QByteArray("name:1")+QByteArray::fromHex("e4")+"\n",sink); QCOMPARE(frames.size(),2);
        parser->feed(QByteArray(20000,'a')+"\nfirst:1,    second:2\nthird:.5\n",sink);
        QCOMPARE(frames.size(),4); QCOMPARE(frames.last().fields[0].value.toDouble(),0.5);
        QVERIFY(parser->statistics().discardedBytes>=20000);
    }
    void csvBoundaries() {
        auto parser=makeProtocolParser("csv",BinaryConfig()); QVector<DecodedFrame> frames;
        auto sink=[&](DecodedFrame f){frames.append(std::move(f));};
        parser->feed("1.5,-2",sink); QCOMPARE(frames.size(),0);
        parser->feed(",3e2\r\n\n4\nnoise\nNaN\n1,,2\n",sink);
        QCOMPARE(frames.size(),2); QCOMPARE(frames[0].fields.size(),3);
        QCOMPARE(frames[0].fields[0].value.toDouble(),1.5);
        QCOMPARE(frames[0].fields[1].value.toDouble(),-2.0);
        QCOMPARE(frames[0].fields[2].value.toDouble(),300.0);
        QCOMPARE(parser->statistics().invalidFrames,3ULL);
        parser->feed(QByteArray(20000,'1')+"\n5,6\n",sink);
        QCOMPARE(frames.size(),3); QCOMPARE(frames.last().fields.size(),2);
        QVERIFY(parser->statistics().discardedBytes>=20000);
    }
    void binaryFragmentsAndTypes() {
        BinaryConfig c; QVector<DecodedFrame> frames;
        auto sink=[&](DecodedFrame f){frames.append(std::move(f));};
        auto parser=makeProtocolParser("binary",c);
        const auto bytes=makeFrame(c,floatBytes(1.5f));
        parser->feed("noise",sink);
        for(char b:bytes) parser->feed(QByteArray(1,b),sink);
        QCOMPARE(frames.size(),1); QCOMPARE(frames[0].fields[0].value.toDouble(),1.5);
        parser->feed(bytes+bytes,sink); QCOMPARE(frames.size(),3);
        auto map=c.toMap(); auto field=map["fields"].toList()[0].toMap(); field["endian"]="big"; map["fields"]=QVariantList{field};
        c=valid(map); parser=makeProtocolParser("binary",c); frames.clear();
        parser->feed(makeFrame(c,QByteArray::fromHex("3fc00000")),sink);
        QCOMPARE(frames[0].fields[0].value.toDouble(),1.5);
        map=c.toMap(); map["frameSize"]=13;
        map["fields"]=QVariantList{
            QVariantMap{{"name","signed"},{"type","int16"},{"offset",2},{"count",1},{"endian","little"}},
            QVariantMap{{"name","large"},{"type","uint64"},{"offset",4},{"count",1},{"endian","big"}},
            QVariantMap{{"name","small"},{"type","int8"},{"offset",12},{"count",1}}};
        c=valid(map); frames.clear(); parser=makeProtocolParser("binary",c);
        parser->feed(makeFrame(c,QByteArray::fromHex("80ffffffffffffffffffff")),sink);
        QCOMPARE(frames.size(),1); QCOMPARE(frames[0].fields[0].value.toLongLong(),-128LL);
        QCOMPARE(frames[0].fields[1].value.toULongLong(),std::numeric_limits<qulonglong>::max());
        QCOMPARE(frames[0].fields[2].value.toLongLong(),-1LL);
    }
    void variableArraysAndLengths() {
        for(const QString &meaning:QStringList{"payload","frame"}) for(bool includes:{false,true}) for(int width:{1,2,4}) {
            auto map=variableConfig(); map["lengthMeaning"]=meaning; map["lengthIncludesChecksum"]=includes;
            map["lengthBytes"]=width; map["lengthEndian"]="big"; map["payloadOffset"]=2+width;
            auto fields=map["fields"].toList(); auto f=fields[0].toMap(); f["offset"]=2+width; map["fields"]=QVariantList{f};
            const auto c=valid(map); auto parser=makeProtocolParser("binary",c); QVector<DecodedFrame> frames;
            auto sink=[&](DecodedFrame frame){frames.append(std::move(frame));};
            const auto bytes=makeFrame(c,floatBytes(1.5f)+floatBytes(-2.0f));
            parser->feed(bytes.first(3),sink); QCOMPARE(frames.size(),0);
            parser->feed(bytes.sliced(3),sink); QCOMPARE(frames.size(),1);
            const auto values=frames[0].fields[0].value.toList(); QCOMPARE(values.size(),2);
            QCOMPARE(values[0].toDouble(),1.5); QCOMPARE(values[1].toDouble(),-2.0);
        }
        const auto c=valid(variableConfig("bytes")); auto parser=makeProtocolParser("binary",c);
        QVector<DecodedFrame> frames; auto sink=[&](DecodedFrame f){frames.append(std::move(f));};
        const auto raw=QByteArray::fromHex("01aa55ff00"); parser->feed(makeFrame(c,raw),sink);
        QCOMPARE(frames.size(),1); QCOMPARE(frames[0].fields[0].value.toByteArray(),raw);
        parser->feed(makeFrame(c,{}),sink); QCOMPARE(frames.size(),2); QCOMPARE(frames.last().fields[0].elementCount,0);
    }
    void checksumsAndRecovery() {
        for(const QString &algorithm:QStringList{"none","sum8","sum16","xor8","crc16modbus"}) for(const QString &order:QStringList{"little","big"}) {
            auto map=BinaryConfig().toMap(); map["checksum"]=algorithm; map["checksumEndian"]=order;
            const int width=algorithm=="none"?0:algorithm=="sum8" || algorithm=="xor8"?1:2;
            map["frameSize"]=6+width; map["checksumStart"]=2;
            const auto c=valid(map); auto parser=makeProtocolParser("binary",c); int count=0;
            auto sink=[&](DecodedFrame){++count;}; const auto good=makeFrame(c,floatBytes(1.0f));
            if(width) { auto bad=good; bad[4]^=1; parser->feed(bad,sink); QCOMPARE(count,0); QVERIFY(parser->statistics().invalidFrames>0); }
            parser->feed(good,sink); QCOMPARE(count,1);
        }
        const auto c=valid(variableConfig()); auto parser=makeProtocolParser("binary",c); int count=0;
        auto sink=[&](DecodedFrame){++count;};
        parser->feed(QByteArray::fromHex("aa55ffff"),sink); QVERIFY(parser->statistics().invalidFrames>0);
        parser->feed(makeFrame(c,QByteArray("abc")),sink); QCOMPARE(count,0);
        parser->feed(makeFrame(c,floatBytes(3.0f)),sink); QCOMPARE(count,1);
    }
    void validationAndPersistence() {
        BinaryConfig c; QString error;
        auto bad=BinaryConfig().toMap(); bad["header"]="A"; QVERIFY(!BinaryConfig::fromMap(bad,c,error));
        bad=BinaryConfig().toMap(); bad["frameSize"]=2; QVERIFY(!BinaryConfig::fromMap(bad,c,error));
        auto unusedFixedSize=variableConfig(); unusedFixedSize["frameSize"]=65536;
        QVERIFY(BinaryConfig::fromMap(unusedFixedSize,c,error));
        bad=variableConfig(); bad["lengthBytes"]=3; QVERIFY(!BinaryConfig::fromMap(bad,c,error));
        bad=BinaryConfig().toMap(); bad["checksum"]="script"; QVERIFY(!BinaryConfig::fromMap(bad,c,error));
        bad=BinaryConfig().toMap(); auto f=bad["fields"].toList()[0].toMap(); f["count"]=1.5; bad["fields"]=QVariantList{f}; QVERIFY(!BinaryConfig::fromMap(bad,c,error));
        ProtocolSession session; QVERIFY(session.selectProtocol("csv")); session.feedBytes("1,2\n");
        QTRY_COMPARE(session.latestFields().size(),2); QCOMPARE(session.frameCount(),1ULL);
        const auto before=session.binaryConfiguration(); QVERIFY(!session.applyBinaryConfiguration(bad)); QCOMPARE(session.binaryConfiguration(),before);
        QCOMPARE(session.frameCount(),1ULL);
        const auto config=variableConfig(); QVERIFY(session.applyBinaryConfiguration(config));
        QCOMPARE(session.frameCount(),1ULL); // Editing an inactive binary protocol doesn't clear CSV.
        ProtocolSession restored; QCOMPARE(restored.selectedProtocolId(),QString("none")); QCOMPARE(restored.binaryConfiguration(),session.binaryConfiguration());
        QVERIFY(session.selectProtocol("binary")); QCOMPARE(session.frameCount(),0ULL);
        session.feedBytes(makeFrame(valid(config),floatBytes(1.5f)+floatBytes(2.5f)));
        QTRY_COMPARE(session.latestFields().size(),1); QCOMPARE(session.frameCount(),1ULL);
        QCOMPARE(session.latestFields()[0].toMap()["elementCount"].toInt(),2);
        session.reset(); QCOMPARE(session.frameCount(),0ULL); QVERIFY(session.latestFields().isEmpty());
    }
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); app.setOrganizationName("UartProtocolTests"); app.setApplicationName("protocol_tests");
    QTemporaryDir temp; if(!temp.isValid()) return 2;
    qputenv("XDG_DATA_HOME",temp.path().toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
    ProtocolParserTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "protocol_parser_tests.moc"
