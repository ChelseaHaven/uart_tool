#include "application/receivesession.h"
#include "application/protocolsession.h"
#include <QtTest>
#include <QTemporaryDir>

// No serial device or GUI required: any future raw-byte transport uses this contract.
class MemoryByteSource final : public ByteSource {
public:
    bool isOpen() const override { return m_open; }
    void begin() { m_open = true; emit opened(); }
    void push(const QByteArray &bytes) { if(m_open) emit bytesReceived(bytes); }
    void close() override { if(m_open) { m_open = false; emit closed(); } }
private:
    bool m_open = false;
};
class ReceiveCoreTests : public QObject {
    Q_OBJECT
private slots:
    void protocolCatalogAndSelection() {
        ProtocolCatalog catalog;
        QCOMPARE(catalog.all().size(),4);
        const auto *none=catalog.find("none"); QVERIFY(none);
        QCOMPARE(none->name,QStringLiteral("原始数据")); QVERIFY(!none->description.isEmpty());
        QVERIFY(!catalog.find("missing"));
        ProtocolSession session;
        QCOMPARE(session.protocols().size(),4);
        QCOMPARE(session.selectedProtocolId(),QString("none"));
        QVERIFY(!session.selectedDescription().isEmpty());
        const auto status=session.statusText();
        QSignalSpy selection(&session,&ProtocolSession::selectionChanged);
        QSignalSpy resets(&session,&ProtocolSession::sessionReset);
        QVERIFY(!session.selectProtocol("unknown"));
        QVERIFY(!session.selectProtocol(""));
        QCOMPARE(session.selectedProtocolId(),QString("none"));
        QCOMPARE(session.statusText(),status); QCOMPARE(selection.size(),0); QCOMPARE(resets.size(),0);
        QVERIFY(session.selectProtocol("none")); QCOMPARE(selection.size(),0);
        session.reset(); QCOMPARE(resets.size(),1);
        QCOMPARE(session.selectedProtocolId(),QString("none")); QCOMPARE(session.statusText(),status);
    }
    void independentSourceLifecycle() {
        MemoryByteSource source;
        ReceiveSession session(source);
        session.setShowTimestamps(false);
        source.begin();
        const QByteArray text = QStringLiteral("数据源无关\n").toUtf8();
        source.push(text.left(2)); QTest::qWait(70);
        QCOMPARE(session.displayText(), QString());
        source.push(text.mid(2));
        QTRY_COMPARE(session.displayText(), QStringLiteral("[RX] 数据源无关\n"));
        QCOMPARE(session.receivedBytes(), qulonglong(text.size()));
        session.setDisplayPaused(true);
        source.push("next\n"); QTest::qWait(70);
        QCOMPARE(session.displayText(), QStringLiteral("[RX] 数据源无关\n"));
        session.setDisplayPaused(false);
        QVERIFY(session.displayText().endsWith("next\n"));
        source.push("last"); source.close();
        QVERIFY(session.displayText().endsWith("last"));
        source.begin(); QCOMPARE(session.receivedBytes(),0ULL);
        QVERIFY(session.displayText().isEmpty()); QVERIFY(!session.displayPaused());
    }
    void timestampedHistory() {
        ReceiveBuffer buffer;
        const auto epoch = QDateTime(QDate(2026,11,18),QTime(12,34,56)).toMSecsSinceEpoch()*1000;
        const auto first = QStringLiteral("[RX] [12:34:56.123456] ");
        const auto second = QStringLiteral("[RX] [12:34:56.123912] ");
        const auto third = QStringLiteral("[RX] [12:34:56.124000] ");
        const auto prefix = QStringLiteral("中").toUtf8();
        buffer.append(prefix.left(2),epoch+123456);
        QVERIFY(ByteFormatter::format(buffer,{false}).text.isEmpty());
        buffer.append(prefix.mid(2)+"\r",epoch+123912);
        buffer.append("\nnext\n",epoch+124000);
        QCOMPARE(ByteFormatter::format(buffer,{false}).text,
                 first+QStringLiteral("中\n")+third+"next\n");
        QCOMPARE(ByteFormatter::format(buffer,{true}).text,
                 first+"E4 B8\n"+second+"AD 0D\n"+third+"0A 6E 65 78 74 0A");

        buffer.append(QByteArray(ReceiveBuffer::Capacity,'x'),epoch+124000);
        QVERIFY(buffer.truncated());
        QCOMPARE(buffer.batches().size(),size_t(1));
        for (bool hex : {false,true}) {
            const auto display = ByteFormatter::format(buffer,{hex});
            QVERIFY(display.limited);
            QVERIFY(display.text.size()<=ReceiveBuffer::Capacity);
            QVERIFY(display.text.startsWith(third));
        }
        buffer.clear();
        QVERIFY(buffer.batches().empty());
        QVERIFY(ByteFormatter::format(buffer,{false}).text.isEmpty());
    }
    void interleavedTraffic() {
        using Direction = ReceiveBuffer::Direction;
        ReceiveBuffer buffer;
        const auto chinese = QStringLiteral("中").toUtf8();
        buffer.append(chinese.left(2),0,Direction::Rx);
        buffer.append("command",0,Direction::Tx);
        buffer.append(chinese.mid(2)+"\r",0,Direction::Rx);
        buffer.append("\r\n",0,Direction::Tx);
        buffer.append("\nreply\n",0,Direction::Rx);
        QCOMPARE(ByteFormatter::format(buffer,{false,false}).text,
                 QStringLiteral("[TX] command\n[RX] 中\n[TX] \n[RX] reply\n"));
        QCOMPARE(ByteFormatter::format(buffer,{false,false,false,true}).text,QString("[TX] command\n[TX] \n"));
        QCOMPARE(ByteFormatter::format(buffer,{false,false,true,false}).text,QStringLiteral("[RX] 中\n[RX] reply\n"));
        QVERIFY(ByteFormatter::format(buffer,{false,false,false,false}).text.isEmpty());
    }
    void independentFormattingAndLimits() {
        MemoryByteSource source; ReceiveSession session(source); session.setShowTimestamps(false); source.begin();
        source.push(QByteArray::fromHex("00ff")); session.setHexMode(true);
        QCOMPARE(session.displayText(),QString("[RX] 00 FF"));
        session.setDisplayPaused(true);
        source.push(QByteArray(ReceiveBuffer::Capacity+40,'A'));
        QTRY_VERIFY(session.cacheLimited());
        QCOMPARE(session.displayText(),QString("[RX] 00 FF"));
        QCOMPARE(session.receivedBytes(),qulonglong(ReceiveBuffer::Capacity+42));
        session.setDisplayPaused(false);
        QVERIFY(session.displayText().size()<=ReceiveBuffer::Capacity);
        session.clear(); QCOMPARE(session.receivedBytes(),0ULL);
        QVERIFY(!session.cacheLimited()); QVERIFY(session.displayText().isEmpty());
    }
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); QTemporaryDir temp; if(!temp.isValid()) return 2;
    qputenv("XDG_DATA_HOME",temp.path().toUtf8());
    ReceiveCoreTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "receive_core_tests.moc"
