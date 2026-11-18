#include "plotting/numericstream.h"
#include "application/protocolsession.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include <limits>

class NumericDataTests : public QObject {
    Q_OBJECT
private slots:
    void parsedProtocolsOnly() {
        ProtocolSession protocol; NumericStream stream;
        connect(&protocol,&ProtocolSession::frameDecoded,&stream,&NumericStream::appendFrame);
        connect(&protocol,&ProtocolSession::sessionReset,&stream,&NumericStream::clear);
        QVERIFY(stream.channels().isEmpty()); QVERIFY(protocol.selectProtocol("csv"));
        protocol.feedBytes("1,2\n3,4\n"); QTRY_COMPARE(stream.channels().size(),2);
        const auto csvKey=stream.channels()[0].toMap()["key"].toString();
        QCOMPARE(stream.channels()[0].toMap()["name"].toString(),QString("CH1"));
        QCOMPARE(stream.points(csvKey,-60,stream.latestTime()).size(),2);
        QVERIFY(protocol.selectProtocol("keyvalue")); QVERIFY(stream.channels().isEmpty());
        protocol.feedBytes("bad line\n"); QVERIFY(stream.channels().isEmpty());
        protocol.feedBytes("awa:2.0,  aaa:3\n"); QTRY_COMPARE(stream.channels().size(),2);
        QCOMPARE(stream.channels()[0].toMap()["name"].toString(),QString("awa"));
        QVERIFY(protocol.selectProtocol("binary")); QVERIFY(stream.channels().isEmpty());
        protocol.feedBytes(QByteArray::fromHex("aa550000c03f")); QTRY_COMPARE(stream.channels().size(),1);
        QCOMPARE(stream.channels()[0].toMap()["name"].toString(),QString("value"));
        const auto key=stream.channels()[0].toMap()["key"].toString();
        QCOMPARE(stream.points(key,-60,stream.latestTime()).last().y(),1.5);
        protocol.reset(); QVERIFY(stream.channels().isEmpty()); QVERIFY(stream.points(key,-60,10).isEmpty());
    }
    void dictionaryFieldsUpdateIndependently() {
        ProtocolSession protocol;
        NumericStream stream;
        connect(&protocol,&ProtocolSession::frameDecoded,&stream,&NumericStream::appendFrame);
        connect(&protocol,&ProtocolSession::sessionReset,&stream,&NumericStream::clear);
        QStringList lastFrameNames;
        connect(&protocol,&ProtocolSession::frameDecoded,this,[&](const DecodedFrame &frame) {
            lastFrameNames.clear();
            for(const auto &field:frame.fields) lastFrameNames.append(field.name);
        });

        QVERIFY(protocol.selectProtocol("keyvalue"));
        protocol.feedBytes("speed:10,temp:25\n");
        QTRY_COMPARE(protocol.latestFields().size(),2);
        const auto speed=stream.channels().at(0).toMap().value("key").toString();
        const auto temp=stream.channels().at(1).toMap().value("key").toString();

        protocol.feedBytes("current:0.8\ntemp:26,speed:11\ncurrent:0.9\n");
        QTRY_COMPARE(protocol.latestFields().size(),3);
        QCOMPARE(lastFrameNames,QStringList{"current"});
        QCOMPARE(protocol.frameCount(),4ULL);
        const auto latest=protocol.latestFields();
        QCOMPARE(latest.at(0).toMap().value("name").toString(),QString("speed"));
        QCOMPARE(latest.at(0).toMap().value("value").toDouble(),11.0);
        QCOMPARE(latest.at(1).toMap().value("name").toString(),QString("temp"));
        QCOMPARE(latest.at(1).toMap().value("value").toDouble(),26.0);
        QCOMPARE(latest.at(2).toMap().value("value").toDouble(),0.9);
        QCOMPARE(stream.channels().size(),3);
        const auto speedPoints=stream.points(speed,-1,stream.latestTime());
        QCOMPARE(speedPoints.size(),2);
        QCOMPARE(speedPoints.first().y(),10.0);
        QCOMPARE(speedPoints.last().y(),11.0);
        QCOMPARE(stream.points(temp,-1,stream.latestTime()).size(),2);

        for(int i=0;i<NumericStream::MaxChannels;++i) {
            protocol.feedBytes("extra_"+QByteArray::number(i)+":1\n");
        }
        protocol.feedBytes("speed:12\n");
        QTRY_COMPARE(protocol.latestFields().at(0).toMap().value("value").toDouble(),12.0);
        QCOMPARE(protocol.latestFields().size(),NumericStream::MaxChannels);
        QCOMPARE(stream.channels().size(),NumericStream::MaxChannels);
        QCOMPARE(stream.points(speed,-1,stream.latestTime()).size(),3);
        protocol.reset();
        QVERIFY(protocol.latestFields().isEmpty());
        QVERIFY(stream.channels().isEmpty());
    }

    void xyPairsFromTheSameFrame() {
        ProtocolSession protocol;
        NumericStream stream;
        connect(&protocol,&ProtocolSession::frameDecoded,&stream,&NumericStream::appendFrame);
        connect(&protocol,&ProtocolSession::sessionReset,&stream,&NumericStream::clear);
        QVERIFY(protocol.selectProtocol("keyvalue"));
        protocol.feedBytes("x:1, y:10\nx:2, y:20\ny:30, x:3\n");
        const auto x=QStringLiteral("1:x:-1"), y=QStringLiteral("1:y:-1");
        QCOMPARE(stream.xyPoints(x,y),(QVector<QPointF>{{1,10},{2,20},{3,30}}));
        protocol.feedBytes("x:4\nx:5, y:50\nx:6, y:60\n");
        QCOMPARE(stream.xyPoints(x,y),(QVector<QPointF>{{1,10},{2,20},{3,30},{5,50},{6,60}}));
        protocol.reset();
        QVERIFY(stream.xyPoints(x,y).isEmpty());
    }
    void boundedAndNumericOnly() {
        NumericStream stream; DecodedFrame frame;
        frame.fields.append({"value","float64",2.0,8,1});
        for(int i=0;i<NumericStream::MaxPoints+200;++i) stream.appendFrame(frame);
        const auto key=stream.channels()[0].toMap()["key"].toString();
        QCOMPARE(stream.points(key,-60,stream.latestTime()).size(),NumericStream::MaxPoints);
        frame.fields={{"array","float32",QVariantList{1.0,-2.0,std::numeric_limits<double>::quiet_NaN()},12,3},
            {"raw","bytes",QByteArray("123"),3,3},{"text","float32",QString("2"),1,1}};
        stream.appendFrame(frame); QCOMPARE(stream.channels().size(),2); QVERIFY(!stream.hasChannel(key));
        QCOMPARE(stream.channels()[1].toMap()["name"].toString(),QString("array[1]"));
        frame.fields.clear(); for(int i=0;i<150;++i) frame.fields.append({QString::number(i),"int32",i,4,1});
        stream.appendFrame(frame); QCOMPARE(stream.channels().size(),NumericStream::MaxChannels);
    }
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); QTemporaryDir directory; if(!directory.isValid()) return 2;
    app.setOrganizationName("UartPlotTests"); app.setApplicationName("numeric_data_tests");
    qputenv("XDG_DATA_HOME",directory.path().toUtf8()); QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
    NumericDataTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "numeric_data_tests.moc"
