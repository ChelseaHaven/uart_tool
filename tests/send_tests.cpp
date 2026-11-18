#include "application/sendsession.h"
#include "sources/serial/serialsource.h"
#include <QtTest>
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#endif

class FakeSink final : public ByteSink {
public:
    bool open=true;
    QList<QByteArray> writes;
    bool isWritable() const override { return open; }
    ByteWriteResult writeBytes(const QByteArray &bytes) override { writes.append(bytes); return {bytes.size(),{}}; }
};
class SendTests : public QObject {
    Q_OBJECT
private slots:
    void encodesAndRejectsInvalidInput() {
        FakeSink sink; SendSession session(sink);
        QVERIFY(session.send(QStringLiteral(" 你好 "),false,"crlf"));
        QCOMPARE(sink.writes.last(),QStringLiteral(" 你好 \r\n").toUtf8());
        QVERIFY(session.send("00 7f\tFF",true,"none")); QCOMPARE(sink.writes.last(),QByteArray::fromHex("007fff"));
        QVERIFY(session.send("",false,"lf")); QCOMPARE(sink.writes.last(),QByteArray("\n"));
        QVERIFY(session.send("AA",true,"cr")); QCOMPARE(sink.writes.last(),QByteArray::fromHex("aa0d"));
        const auto count=sink.writes.size();
        QVERIFY(!session.send("A",true,"none")); QVERIFY(session.failed());
        QVERIFY(!session.send("AA GG",true,"none"));
        QVERIFY(!session.send("",false,"none")); QVERIFY(!session.send("abc",false,"unknown"));
        QVERIFY(!session.send(QString(SendSession::MaxPayload+1,'x'),false,"none"));
        QCOMPARE(sink.writes.size(),count);
        sink.open=false; QVERIFY(!session.send("abc",false,"none")); QCOMPARE(sink.writes.size(),count);
    }
    void serialDuplex() {
#ifndef Q_OS_UNIX
        QSKIP("Linux pseudo-terminal transport check");
#else
        struct Master { int fd=posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK); ~Master(){ if(fd>=0) ::close(fd); } } master;
        QVERIFY(master.fd>=0); QVERIFY(grantpt(master.fd)==0); QVERIFY(unlockpt(master.fd)==0);
        SerialSource source; SendSession session(source); SerialSettings settings;
        settings.path=QString::fromLocal8Bit(ptsname(master.fd)); QVERIFY(source.open(settings)); QVERIFY(session.connected());
        QByteArray received;
        connect(&source,&ByteSource::bytesReceived,this,[&](const QByteArray &bytes){ received.append(bytes); });
        QVERIFY(session.send(QStringLiteral("你好"),false,"crlf")); QVERIFY(session.send("00 FF 80",true,"none"));
        QByteArray actual; auto collect=[&] {
            char buffer[4096]; qint64 n;
            while((n=::read(master.fd,buffer,sizeof(buffer)))>0) actual.append(buffer,n);
            return actual;
        };
        const auto expected=QStringLiteral("你好\r\n").toUtf8()+QByteArray::fromHex("00ff80");
        QTRY_COMPARE_WITH_TIMEOUT(collect(),expected,2000);
        const QByteArray incoming="still receiving\n";
        QCOMPARE(::write(master.fd,incoming.constData(),size_t(incoming.size())),ssize_t(incoming.size()));
        QTRY_COMPARE_WITH_TIMEOUT(received,incoming,2000);
        source.close(); QVERIFY(!session.connected()); QVERIFY(!session.send("x",false,"none"));
#endif
    }
};
QTEST_GUILESS_MAIN(SendTests)
#include "send_tests.moc"
