#include "application/applicationservices.h"
#include "presentation/logdocumentsettings.h"
#include "presentation/dockwindowcontroller.h"
#include "core/byteformatter.h"
#include "plotting/timeseriesplot.h"
#include "plotting/xyplot.h"
#include <QtTest>
#include <QSignalSpy>
#include <QFile>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QQuickTextDocument>
#include <QQuickStyle>
#include <QClipboard>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextLayout>
#include <QContextMenuEvent>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QDir>
#include <QSettings>
#include <QImage>
#include <QPainter>
#include <QScreen>
#include <QWheelEvent>
#include <cmath>
#include <limits>
static int templateIndex(ProtocolSession &session,const QString &id) {
    const auto entries=session.templateStore()->entries();
    int index=0;
    for(const auto &entry:entries) {
        const auto value=entry.toMap(); if(value["id"].toString()=="generic") continue;
        if(value["id"].toString()==id) return index;
        ++index;
    }
    return -1;
}
static QQuickItem *visualItem(QQuickItem *root,const QString &name) {
    if(root->objectName()==name) return root;
    for(auto *child:root->childItems()) if(auto *found=visualItem(child,name)) return found;
    return nullptr;
}
static QImage grabItem(QQuickItem *item) {
    const auto result=item->grabToImage(); if(!result) return {};
    QSignalSpy ready(result.data(),&QQuickItemGrabResult::ready);
    if(result->image().isNull()) ready.wait(2000);
    return result->image();
}
static QRectF contentDockRect(QQuickWindow *window) {
    auto *area=window->findChild<QQuickItem *>("dockWorkspace");
    QRectF result(area->mapToGlobal(QPointF()),area->size());
    auto *sidebar=window->findChild<QQuickItem *>("connectionDock");
    if(sidebar && !sidebar->property("floating").toBool()) {
        const QRectF bounds(sidebar->mapToGlobal(QPointF()),sidebar->size());
        const auto gap=sidebar->property("sidebarDividerWidth").toInt();
        if(sidebar->property("sidebarEdge").toString()=="right") result.setRight(bounds.left()-gap);
        else result.setLeft(bounds.right()+gap);
    }
    return result;
}
static bool acceptUnsavedIfVisible(QObject *editor) {
    auto *dialog=editor->findChild<QObject *>("unsavedBinaryTemplateDialog");
    if(!dialog || !dialog->property("visible").toBool()) return true;
    auto *button=dialog->findChild<QQuickItem *>("saveTemplateAndApply");
    return button && QMetaObject::invokeMethod(button,"clicked");
}
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
struct Pty {
    int fd = -1; QString path;
    Pty() {
        fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd >= 0 && grantpt(fd) == 0 && unlockpt(fd) == 0) path = QString::fromLocal8Bit(ptsname(fd));
    }
    ~Pty() { closeMaster(); }
    void closeMaster() { if(fd >= 0) { ::close(fd); fd = -1; } }
    bool send(const QByteArray &bytes) {
        qsizetype offset = 0; QElapsedTimer deadline; deadline.start();
        while(offset < bytes.size() && deadline.elapsed() < 10000) {
            const auto n = ::write(fd, bytes.constData()+offset, size_t(bytes.size()-offset));
            if(n > 0) offset += n;
            else if(errno != EAGAIN && errno != EINTR) return false;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            if(n <= 0) QTest::qWait(1);
        }
        return offset == bytes.size();
    }
};
#endif
class ReceiverTests : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QTest::failOnWarning(QRegularExpression(".*(TypeError|ReferenceError|Binding loop).*"));
        // main() redirects settings to a temporary INI directory for every test run.
        QSettings settings; settings.clear(); settings.sync();
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    }
    void preferencesPersist() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path=directory.filePath("settings.ini");
        { AppSettings settings(path); QVERIFY(!settings.darkMode()); settings.setDarkMode(true); }
        { AppSettings settings(path); QVERIFY(settings.darkMode()); settings.setDarkMode(false); }
        { AppSettings settings(path); QVERIFY(!settings.darkMode()); }
    }
    void bufferFormats() {
        ReceiveBuffer b;
        const QByteArray chinese = QStringLiteral("你好\r\n").toUtf8();
        b.append(chinese.left(2)); QCOMPARE(ByteFormatter::format(b,{false,false}).text, QString());
        b.append(chinese.mid(2)); QCOMPARE(ByteFormatter::format(b,{false,false}).text, QStringLiteral("[RX] 你好\n"));
        b.append(QByteArray::fromHex("00ff"));
        QVERIFY(ByteFormatter::format(b,{true,false}).text.endsWith("00 FF"));
        QVERIFY(ByteFormatter::format(b,{false,false}).text.contains(QChar(0x2400)));
        QVERIFY(ByteFormatter::format(b,{false,false}).text.contains(QChar::ReplacementCharacter));
        b.clear(); QCOMPARE(b.size(),0); QVERIFY(!b.truncated());
    }
    void bufferBounds() {
        ReceiveBuffer b; b.append(QByteArray(ReceiveBuffer::Capacity,'A'));
        QCOMPARE(b.size(),ReceiveBuffer::Capacity); QVERIFY(!b.truncated());
        b.append("B"); QVERIFY(b.truncated()); QCOMPARE(b.size(),ReceiveBuffer::Capacity);
        QVERIFY(ByteFormatter::format(b,{false,false}).text.endsWith('B'));
        QVERIFY(ByteFormatter::format(b,{true,false}).text.size() <= ReceiveBuffer::Capacity);
        QVERIFY(ByteFormatter::format(b,{true,false}).limited);
        b.append(QByteArray(2*ReceiveBuffer::Capacity,'Z'));
        QCOMPARE(b.size(),ReceiveBuffer::Capacity);
        QCOMPARE(ByteFormatter::format(b,{false,false}).text,"[RX] "+QString(ReceiveBuffer::Capacity-5,'Z'));
    }
    void invalidParameters() {
        ApplicationServices services;
        auto &c = services.receiveSession(); auto &connection = services.serialConnection();
        QVERIFY(!connection.openPort("",115200)); QVERIFY(!connection.connected()); QVERIFY(!connection.errorText().isEmpty());
        QVERIFY(!connection.openPort("dummy",0)); QVERIFY(!connection.openPort("dummy",115200,9));
        QVERIFY(!connection.openPort("dummy",115200,8,42));
        QVERIFY(!connection.openPort("/this-port-does-not-exist")); QVERIFY(!connection.connected());
        QVERIFY(connection.errorText().contains(QStringLiteral("打开失败")));
        connection.disconnectPort(); c.clear(); QCOMPARE(c.receivedBytes(),0ULL);
    }
    void serialEndToEnd() {
#ifndef Q_OS_UNIX
        QSKIP("Linux pseudo-terminal test");
#else
        Pty p; QVERIFY2(!p.path.isEmpty(),"Cannot create a pseudo-terminal");
        ApplicationServices services;
        auto &c = services.receiveSession(); auto &connection = services.serialConnection(); QString mirrored;
        c.setShowTimestamps(false);
        connect(&c,&ReceiveSession::displayUpdate,this,[&](const QString &text,int removed,bool reset) {
            if(reset) mirrored=text; else { mirrored.remove(0,removed); mirrored.append(text); }
        });
        QVERIFY2(connection.openPort(p.path),qPrintable(connection.errorText()));
        QVERIFY(connection.connected());
        QVERIFY(!connection.openPort(p.path)); QVERIFY(connection.connected());
        const QByteArray utf8=QStringLiteral("分批中文\r\n").toUtf8();
        QVERIFY(p.send(utf8.left(2))); QTRY_COMPARE(c.receivedBytes(),2ULL);
        QTest::qWait(70); QCOMPARE(c.displayText(),QString());
        QVERIFY(p.send(utf8.mid(2))); QTRY_COMPARE(c.displayText(),QStringLiteral("[RX] 分批中文\n"));
        QCOMPARE(mirrored,c.displayText());
        c.setHexMode(true);
        QVERIFY(c.displayText().startsWith("[RX] E5 88\n[RX] 86"));
        QCOMPARE(mirrored,c.displayText());
        c.setHexMode(false); QCOMPARE(c.displayText(),QStringLiteral("[RX] 分批中文\n"));
        c.setDisplayPaused(true); const QString frozen=c.displayText();
        QVERIFY(p.send(QByteArray::fromHex("00ff"))); QTRY_COMPARE(c.receivedBytes(),qulonglong(utf8.size()+2));
        QTest::qWait(80); QCOMPARE(c.displayText(),frozen);
        c.setHexMode(true); QCOMPARE(c.displayText(),frozen);
        c.setDisplayPaused(false); QVERIFY(c.displayText().endsWith("00 FF")); QCOMPARE(mirrored,c.displayText());
        c.setDisplayPaused(true); c.clear(); QCOMPARE(c.receivedBytes(),0ULL); QCOMPARE(c.displayText(),QString());
        QVERIFY(c.displayPaused()); QVERIFY(!c.cacheLimited());
        c.setHexMode(false); c.setDisplayPaused(false);
        QTimer heartbeat; heartbeat.setInterval(10); int ticks=0;
        connect(&heartbeat,&QTimer::timeout,this,[&]{ ++ticks; }); heartbeat.start();
        const QByteArray chunk=QByteArray("0123456789ABCDEF\n").repeated(1024);
        qulonglong sent=0; QElapsedTimer stress; stress.start();
        while(stress.elapsed()<2200 || sent<2*1024*1024) {
            QVERIFY(p.send(chunk)); sent+=qulonglong(chunk.size()); QTest::qWait(1);
        }
        QTRY_COMPARE_WITH_TIMEOUT(c.receivedBytes(),sent,10000);
        QTRY_VERIFY(c.cacheLimited()); QTest::qWait(100);
        QVERIFY(c.displayText().size()<=ReceiveBuffer::Capacity); QCOMPARE(mirrored,c.displayText());
        QVERIFY2(ticks>20,"Event loop stopped responding during receive stress");
        qInfo()<<"Stress bytes:"<<sent<<"elapsed ms:"<<stress.elapsed()<<"heartbeat ticks:"<<ticks;
        c.setHexMode(true); QVERIFY(c.displayText().size()<=ReceiveBuffer::Capacity); QCOMPARE(mirrored,c.displayText());
        connection.disconnectPort(); QVERIFY(!connection.connected());
        QVERIFY(connection.openPort(p.path)); QCOMPARE(c.receivedBytes(),0ULL); QVERIFY(connection.errorText().isEmpty());
        p.closeMaster(); QTRY_VERIFY_WITH_TIMEOUT(!connection.connected(),3000); QVERIFY(!connection.errorText().isEmpty());
#endif
    }
    void allPortsListed() {
        SerialSource source;
        SerialConnectionModel model(source);
        const auto available=source.availablePorts(); QCOMPARE(model.ports().size(),available.size());
        for(int i=0;i<available.size();++i) QCOMPARE(model.ports()[i].toMap()["path"].toString(),available[i].path);
    }
    void themeSwitchUi() {
        ApplicationServices services([] { return QList<QSerialPortInfo>(); });
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&services.serialConnection());
        engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
        engine.rootContext()->setContextProperty("protocolSession",&services.protocolSession());
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml"))); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(!services.settings().darkMode()); const auto light=window->color();
        auto *settings=window->findChild<QQuickItem *>("settingsButton"); QVERIFY(settings); QVERIFY(QMetaObject::invokeMethod(settings,"clicked"));
        auto *choice=window->findChild<QQuickItem *>("themeModeSelector"); QVERIFY(choice); QCOMPARE(choice->property("currentIndex").toInt(),0);
        choice->setProperty("currentIndex",1); QVERIFY(QMetaObject::invokeMethod(choice,"activated",Q_ARG(int,1)));
        QVERIFY(services.settings().darkMode()); QTRY_VERIFY(window->color().lightnessF()<0.2);
        AppSettings restored; QVERIFY(restored.darkMode());
        const auto directory=qEnvironmentVariable("UART_THEME_SNAPSHOT_DIR");
        auto snapshot=[&](const QString &name) { QTest::qWait(60); return directory.isEmpty() || window->grabWindow().save(directory+"/"+name+".png"); };
        auto *backText=settings->property("contentItem").value<QQuickItem *>(); QVERIFY(backText);
        QVERIFY(backText->property("color").value<QColor>().lightnessF()>0.6);
        QVERIFY(snapshot("uart-settings-dark"));
        QVERIFY(QMetaObject::invokeMethod(settings,"clicked")); QVERIFY(snapshot("uart-receiver-dark"));
        QVERIFY(services.protocolSession().selectProtocol("binary"));
        auto *open=window->findChild<QQuickItem *>("editBinaryProtocol"); QVERIFY(open); QVERIFY(QMetaObject::invokeMethod(open,"clicked"));
        auto *editor=window->findChild<QObject *>("binaryProtocolEditor"); QVERIFY(editor); QTRY_VERIFY(editor->property("visible").toBool());
        auto *create=editor->findChild<QQuickItem *>("newBinaryTemplate"); QVERIFY(create); QVERIFY(QMetaObject::invokeMethod(create,"clicked"));
        auto *header=visualItem(window->contentItem(),"structureHex0"); QVERIFY(header); QVERIFY(header->property("color").value<QColor>().lightnessF()>0.6);
        auto *apply=editor->findChild<QQuickItem *>("saveBinaryProtocol"); QVERIFY(apply);
        auto *applyText=apply->property("contentItem").value<QQuickItem *>(); QVERIFY(applyText);
        QCOMPARE(applyText->property("color").value<QColor>(),QColor("white"));
        QVERIFY(snapshot("uart-editor-dark"));
        QVERIFY(QMetaObject::invokeMethod(editor,"close"));
        QVERIFY(QMetaObject::invokeMethod(settings,"clicked"));
        choice->setProperty("currentIndex",0); QVERIFY(QMetaObject::invokeMethod(choice,"activated",Q_ARG(int,0)));
        QVERIFY(!services.settings().darkMode()); QTRY_COMPARE(window->color(),light);
        QVERIFY(snapshot("uart-settings-light"));
        AppSettings restoredLight; QVERIFY(!restoredLight.darkMode());
    }
    void noPortUi() {
        ApplicationServices services([] { return QList<QSerialPortInfo>(); });
        auto &c = services.receiveSession(); auto &connection = services.serialConnection();
        QVERIFY(connection.ports().isEmpty()); connection.refreshPorts(); QVERIFY(connection.ports().isEmpty());
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("appSettings", &services.settings());
        engine.rootContext()->setContextProperty("serialConnection", &connection);
        engine.rootContext()->setContextProperty("receiveSession", &c);
        engine.rootContext()->setContextProperty("protocolSession", &services.protocolSession());
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *button=engine.rootObjects().first()->findChild<QQuickItem *>("connectionButton");
        QVERIFY(button); QVERIFY(!button->isEnabled());
        auto *root=engine.rootObjects().first();
        auto *settingsButton=root->findChild<QQuickItem *>("settingsButton"); QVERIFY(settingsButton);
        auto *settingsPage=root->findChild<QQuickItem *>("settingsPage"); QVERIFY(settingsPage);
        QVERIFY(!settingsPage->isVisible());
        QVERIFY(QMetaObject::invokeMethod(settingsButton,"clicked")); QTRY_VERIFY(settingsPage->isVisible());
        const QString snapshot=qEnvironmentVariable("UART_SETTINGS_SNAPSHOT");
        if(!snapshot.isEmpty()) {
            QTest::qWait(80);
            auto *window=qobject_cast<QQuickWindow *>(root); QVERIFY(window);
            QVERIFY(window->grabWindow().save(snapshot));
        }
        QVERIFY(!root->findChild<QQuickItem *>("showAllPortsToggle"));
        QVERIFY(connection.ports().isEmpty()); QVERIFY(!button->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(settingsButton,"clicked")); QTRY_VERIFY(!settingsPage->isVisible());
    }
    void continuousUiReceive() {
#ifndef Q_OS_UNIX
        QSKIP("Linux pseudo-terminal UI stress test");
#else
        Pty p; QVERIFY(!p.path.isEmpty()); ApplicationServices services;
        auto &c = services.receiveSession(); auto &connection = services.serialConnection();
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("appSettings", &services.settings());
        engine.rootContext()->setContextProperty("serialConnection", &connection);
        engine.rootContext()->setContextProperty("receiveSession", &c);
        engine.rootContext()->setContextProperty("protocolSession", &services.protocolSession());
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *output=engine.rootObjects().first()->findChild<QQuickItem *>("receiveText"); QVERIFY(output);
        QVERIFY(connection.openPort(p.path));
        auto memory=[] {
            QFile f("/proc/self/status"); if(!f.open(QIODevice::ReadOnly)) return qint64(0);
            const auto lines=f.readAll().split('\n');
            for(const auto &line:lines) if(line.startsWith("VmRSS:")) return line.simplified().split(' ').value(1).toLongLong();
            return qint64(0);
        };
        QTimer heartbeat; heartbeat.setInterval(20); int ticks=0;
        connect(&heartbeat,&QTimer::timeout,this,[&]{++ticks;}); heartbeat.start();
        qulonglong sent=0; qint64 baseline=0,peak=0; QElapsedTimer timer; timer.start();
        const auto payload=QByteArray("sample: 1.0, 2.0, 3.0\n").repeated(512);
        for(int i=0;i<300;++i) {
            if(i==150) c.setHexMode(true);
            QVERIFY(p.send(payload)); sent+=qulonglong(payload.size()); QTest::qWait(100);
            if(i==80) baseline=memory();
            if(i>=80) peak=std::max(peak,memory());
        }
        QTRY_COMPARE_WITH_TIMEOUT(c.receivedBytes(),sent,5000); QTest::qWait(150);
        QVERIFY(c.cacheLimited()); QVERIFY(c.displayText().size()<=ReceiveBuffer::Capacity);
        QCOMPARE(output->property("text").toString(),c.displayText());
        QVERIFY(ticks>100);
        if(baseline) QVERIFY2(peak-baseline<64*1024,"RSS continued growing beyond the bounded-history allowance");
        qInfo()<<"UI soak bytes:"<<sent<<"elapsed ms:"<<timer.elapsed()<<"RSS baseline/peak KiB:"<<baseline<<peak<<"heartbeat:"<<ticks;
#endif
    }
    void serialSendUi() {
#ifndef Q_OS_UNIX
        QSKIP("Linux pseudo-terminal send UI check");
#else
        Pty p; QVERIFY(!p.path.isEmpty()); ApplicationServices services; QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("sendSession",&services.sendSession());
        engine.rootContext()->setContextProperty("numericStream",&services.numericStream());
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&services.serialConnection());
        engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
        engine.rootContext()->setContextProperty("protocolSession",&services.protocolSession());
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml"))); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); QTRY_VERIFY(window->isExposed()); QTest::qWait(60);
        auto *receive=visualItem(window->contentItem(),"receiveWorkspace"); QVERIFY(receive);
        QVERIFY(!visualItem(receive,"sendPanel"));
        auto *menuTab=window->findChild<QQuickItem *>("componentMenuTab"); QVERIFY(menuTab);
        QVERIFY(QMetaObject::invokeMethod(menuTab,"clicked"));
        auto *tile=visualItem(window->contentItem(),"quickSendComponent"); QVERIFY(tile);
        auto *workspace=window->findChild<QQuickItem *>("componentWorkspace"); QVERIFY(workspace);
        QTRY_VERIFY(tile->isVisible() && tile->width()>100);
        const auto dragStart=tile->mapToScene(QPointF(65,35)).toPoint();
        const auto drop=workspace->mapToScene(QPointF(14,70)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,dragStart);
        QTest::mouseMove(window,dragStart+QPoint(25,10),1);
        QTest::mouseMove(window,drop,1);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,drop);
        QQuickItem *card=nullptr; QTRY_VERIFY((card=visualItem(window->contentItem(),"quickSendCard_1")));
        auto *button=visualItem(card,"sendButton"); auto *input=visualItem(card,"sendInput");
        auto *mode=visualItem(card,"sendMode"); auto *ending=visualItem(card,"sendEnding");
        QVERIFY(button && input && mode && ending);
        QCOMPARE(mode->property("currentText").toString(),QStringLiteral("文本"));
        QCOMPARE(ending->property("currentValue").toString(),QStringLiteral("lf"));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        auto *moveHandle=visualItem(card,"componentMoveHandle"); QVERIFY(moveHandle);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,moveHandle->mapToScene(QPointF(15,15)).toPoint());
        QTest::keyClick(window,Qt::Key_C,Qt::ControlModifier);
        const auto inputPoint=input->mapToScene(QPointF(25,20)).toPoint();
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,inputPoint);
        QTRY_VERIFY(input->hasActiveFocus());
        QTRY_COMPARE(window->cursor().shape(),Qt::IBeamCursor);
        input->setProperty("text","payload");
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
        QTest::keyClick(window,Qt::Key_C,Qt::ControlModifier);
        QCOMPARE(QGuiApplication::clipboard()->text(),QString("payload"));
        input->setProperty("text","");
        QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
        QCOMPARE(input->property("text").toString(),QString("payload"));
        QCOMPARE(workspace->property("widgetCount").toInt(),1);

        input->setProperty("text","");
        QTest::mouseClick(window,Qt::RightButton,Qt::NoModifier,inputPoint);
        auto *textMenu=input->findChild<QObject *>("textEditingMenu"); QVERIFY(textMenu);
        QTRY_VERIFY(textMenu->property("opened").toBool());
        auto *textMenuContent=textMenu->property("contentItem").value<QQuickItem *>(); QVERIFY(textMenuContent);
        auto *pasteText=visualItem(textMenuContent,"pasteInputText"); QVERIFY(pasteText && pasteText->isEnabled());
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,
                          pasteText->mapToScene(QPointF(pasteText->width()/2,pasteText->height()/2)).toPoint());
        QCOMPARE(input->property("text").toString(),QString("payload"));
        QCOMPARE(workspace->property("widgetCount").toInt(),1);
        QTRY_VERIFY(!textMenu->property("visible").toBool());
        QTest::mouseClick(window,Qt::RightButton,Qt::NoModifier,inputPoint);
        QTRY_VERIFY(textMenu->property("opened").toBool());
        QTest::keyClick(window,Qt::Key_Escape);
        QTRY_VERIFY(!textMenu->property("visible").toBool());
        QVERIFY(!button->isEnabled()); QVERIFY(services.serialConnection().openPort(p.path));
        input->setProperty("text",QStringLiteral("你好")); ending->setProperty("currentIndex",2); QTRY_VERIFY(button->isEnabled());
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint());
        QByteArray actual; auto collect=[&] { char bytes[4096]; ssize_t n; while((n=::read(p.fd,bytes,sizeof(bytes)))>0) actual.append(bytes,n); return actual; };
        QByteArray expected=QStringLiteral("你好\r\n").toUtf8(); QTRY_COMPARE_WITH_TIMEOUT(collect(),expected,2000);
        QCOMPARE(input->property("text").toString(),QStringLiteral("你好"));
        mode->setProperty("currentIndex",1); ending->setProperty("currentIndex",0); input->setProperty("text","00 FF 7f");
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        input->forceActiveFocus();
        QTRY_VERIFY(input->hasActiveFocus());
        QTest::keyClick(window,Qt::Key_Return,Qt::ControlModifier);
        expected.append(QByteArray::fromHex("00ff7f")); QTRY_COMPARE_WITH_TIMEOUT(collect(),expected,2000);
        QCOMPARE(input->property("text").toString(),QString("00 FF 7f"));
        input->setProperty("text","0G"); QVERIFY(QMetaObject::invokeMethod(button,"clicked")); QVERIFY(services.sendSession().failed()); QCOMPARE(collect(),expected);
        QCOMPARE(services.receiveSession().receivedBytes(),0ULL);
        QVERIFY(p.send("ok\n")); QTRY_COMPARE(services.receiveSession().receivedBytes(),3ULL);
        auto &log=services.receiveSession();
        log.setShowTimestamps(false);
        log.setHexMode(true);
        auto *logView=visualItem(receive,"receiveText"); QVERIFY(logView);
        auto *rxFilter=visualItem(receive,"receiveRxFilter"); QVERIFY(rxFilter);
        auto *txFilter=visualItem(receive,"receiveTxFilter"); QVERIFY(txFilter);
        const QString allTraffic="[TX] E4 BD A0 E5 A5 BD 0D 0A\n[TX] 00 FF 7F\n[RX] 6F 6B 0A";
        QTRY_COMPARE(logView->property("text").toString(),allTraffic);
        auto *document=logView->property("textDocument").value<QQuickTextDocument *>()->textDocument();
        const auto colorAt = [&](int block) {
            const auto ranges=document->findBlockByNumber(block).layout()->formats();
            return ranges.isEmpty()?QColor():ranges.front().format.foreground().color();
        };
        QTRY_COMPARE(colorAt(0),QColor("#885110"));
        QTRY_COMPARE(colorAt(2),QColor("#167c8a"));
        const auto trafficSnapshot=qEnvironmentVariable("UART_TRAFFIC_SNAPSHOT");
        if (!trafficSnapshot.isEmpty()) { QTest::qWait(60); QVERIFY(window->grabWindow().save(trafficSnapshot)); }
        const auto toggle = [&](QQuickItem *filter) {
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,filter->mapToScene(QPointF(12,filter->height()/2)).toPoint());
        };
        toggle(txFilter);
        QTRY_COMPARE(logView->property("text").toString(),QString("[RX] 6F 6B 0A"));
        toggle(rxFilter);
        QTRY_VERIFY(logView->property("text").toString().isEmpty());
        toggle(txFilter);
        QTRY_COMPARE(logView->property("text").toString(),QString("[TX] E4 BD A0 E5 A5 BD 0D 0A\n[TX] 00 FF 7F"));
        toggle(rxFilter);
        QTRY_COMPARE(logView->property("text").toString(),allTraffic);
        QCOMPARE(log.receivedBytes(),3ULL);
        log.setHexMode(false);
        QTRY_VERIFY(logView->property("text").toString().startsWith(QStringLiteral("[TX] 你好\n")));

        mode->setProperty("currentIndex",2);
        ending->setProperty("currentIndex",1);
        auto *rename=visualItem(card,"renameQuickSend"); QVERIFY(rename);
        window->requestActivate();
        QTRY_VERIFY_WITH_TIMEOUT(window->isActive(),1500);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,rename->mapToScene(QPointF(20,12)).toPoint());
        QQuickItem *name=nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((name=visualItem(card,"quickSendName")) && name->hasActiveFocus(),1500);
        QVERIFY(std::abs(name->mapToScene(QPointF()).y()-rename->mapToScene(QPointF()).y())<=1);
        QVERIFY(name->mapToScene(QPointF(name->width(),0)).x()<rename->mapToScene(QPointF()).x());
        name->setProperty("text",QStringLiteral("轮速 PID"));
        QTest::keyClick(window,Qt::Key_Return);
        QCOMPARE(card->property("title").toString(),QStringLiteral("轮速 PID"));
        QTRY_VERIFY_WITH_TIMEOUT(!visualItem(card,"quickSendName"),1500);

        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,rename->mapToScene(QPointF(20,12)).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT((name=visualItem(card,"quickSendName")) && name->hasActiveFocus(),1500);
        QCOMPARE(name->property("selectedText").toString(),QStringLiteral("轮速 PID"));
        name->setProperty("text","ignored");
        QTest::keyClick(window,Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!visualItem(card,"quickSendName"),1500);
        QCOMPARE(card->property("title").toString(),QStringLiteral("轮速 PID"));

        auto *add=visualItem(card,"addSendParameter"); QVERIFY(add);
        QVERIFY(QMetaObject::invokeMethod(add,"clicked"));
        QVERIFY(QMetaObject::invokeMethod(add,"clicked"));
        auto edit=[&](const QString &objectName,const QString &text) {
            auto *field=visualItem(card,objectName); QVERIFY(field);
            field->setProperty("text",text);
            QVERIFY(QMetaObject::invokeMethod(field,"textEdited"));
        };
        edit("parameterName_0","kp");
        edit("parameterValue_0","1.0");
        edit("parameterName_1","ki");
        edit("parameterValue_1","0.05");
        edit("parameterName_2","kd");
        edit("parameterValue_2","0.01");
        auto *kp=visualItem(card,"parameterValue_0"); QVERIFY(kp);
        auto *ki=visualItem(card,"parameterValue_1"); QVERIFY(ki);
        QTest::qWait(60);
        const QPointF fieldCenter(kp->width()/2,kp->height()/2);
        QTest::mouseClick(window,Qt::RightButton,Qt::NoModifier,kp->mapToScene(fieldCenter).toPoint());
        auto *valueMenu=kp->findChild<QObject *>("textEditingMenu"); QVERIFY(valueMenu);
        QTRY_VERIFY(valueMenu->property("opened").toBool());
        auto *valueMenuContent=valueMenu->property("contentItem").value<QQuickItem *>(); QVERIFY(valueMenuContent);
        auto *stepAction=visualItem(valueMenuContent,"extraInputAction"); QVERIFY(stepAction && stepAction->isVisible());
        QSignalSpy stepTriggered(stepAction,SIGNAL(triggered()));
        QTest::qWait(60);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,
                          stepAction->mapToScene(QPointF(stepAction->width()/2,stepAction->height()/2)).toPoint());
        QTRY_COMPARE_WITH_TIMEOUT(stepTriggered.count(),1,1500);
        auto *step=kp->findChild<QQuickItem *>("parameterStepInput"); QVERIFY(step);
        QTRY_VERIFY_WITH_TIMEOUT(step->isVisible(),1500);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,step->mapToScene(QPointF(20,12)).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(step->hasActiveFocus(),1500);
        step->setProperty("text","0.2");
        QTest::keyClick(window,Qt::Key_Return);
        QCOMPARE(kp->property("stepText").toString(),QStringLiteral("0.2"));
        QCOMPARE(ki->property("stepText").toString(),QStringLiteral("0.1"));
        QTRY_VERIFY(!step->isVisible());
        const auto wheelAt=[&](QQuickItem *field) {
            const QPointF center(field->width()/2,field->height()/2);
            QWheelEvent wheel(field->mapToScene(center),field->mapToGlobal(center),{},QPoint(0,120),
                              Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QCoreApplication::sendEvent(window,&wheel);
        };
        wheelAt(kp);
        QCOMPARE(kp->property("text").toString(),QStringLiteral("1.0"));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,kp->mapToScene(fieldCenter).toPoint());
        QVERIFY(kp->hasActiveFocus());
        QVERIFY(kp->property("wheelArmed").toBool());
        wheelAt(ki);
        QCOMPARE(ki->property("text").toString(),QStringLiteral("0.05"));
        wheelAt(kp);
        QTRY_COMPARE(kp->property("text").toString(),QStringLiteral("1.2"));
        QCOMPARE(collect(),expected); // Adjusting a value must never send data.

        auto *firstUp=visualItem(card,"moveSendParameterUp_0"); QVERIFY(firstUp && !firstUp->isEnabled());
        auto *secondUp=visualItem(card,"moveSendParameterUp_1"); QVERIFY(secondUp && secondUp->isEnabled());
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,
                          secondUp->mapToScene(QPointF(secondUp->width()/2,secondUp->height()/2)).toPoint());
        auto *movedValue=visualItem(card,"parameterValue_1"); QVERIFY(movedValue);
        QCOMPARE(movedValue->property("stepText").toString(),QStringLiteral("0.2"));

        const QByteArray command="ki:0.05,kp:1.2,kd:0.01";
        QTRY_VERIFY(button->isEnabled());
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,
                          button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint());
        expected.append(command+'\n');
        QTRY_COMPARE_WITH_TIMEOUT(collect(),expected,2000);
        QCOMPARE(input->property("text").toString(),QString("0G"));

        auto enableTimed=[&](QQuickItem *scope) {
            auto *toggle=visualItem(scope,"timedSendToggle"); QVERIFY(toggle);
            auto *interval=visualItem(scope,"timedSendInterval"); QVERIFY(interval);
            interval->setProperty("text","40");
            toggle->setProperty("checked",true);
        };
        auto clickSend=[&](QQuickItem *send) {
            const QPointF center(send->width()/2,send->height()/2);
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,send->mapToScene(center).toPoint());
        };
        auto verifyStopped=[&](QQuickItem *send) {
            QCOMPARE(send->property("text").toString(),QStringLiteral("发送"));
            QTest::qWait(25); // Allow queued writes to reach the peer.
            const auto stopped=collect();
            QTest::qWait(120);
            QCOMPARE(collect(),stopped);
        };

        enableTimed(card);
        QCOMPARE(collect(),expected);
        mode->setProperty("currentIndex",0);
        input->setProperty("text","tick");
        ending->setProperty("currentIndex",1);
        clickSend(button);
        QCOMPARE(button->property("text").toString(),QStringLiteral("停止"));
        QTRY_VERIFY_WITH_TIMEOUT(collect().mid(expected.size()).startsWith("tick\ntick\n"),1500);
        clickSend(button);
        verifyStopped(button);

        mode->setProperty("currentIndex",2);
        const auto start=collect().size();
        clickSend(button);
        QCOMPARE(button->property("text").toString(),QStringLiteral("停止"));
        QTRY_VERIFY_WITH_TIMEOUT(collect().mid(start).startsWith(command+'\n'+command+'\n'),1500);
        edit("parameterValue_1","1.4");
        QTRY_VERIFY_WITH_TIMEOUT(collect().mid(start).contains("ki:0.05,kp:1.4,kd:0.01\n"),1500);
        services.serialConnection().disconnectPort();
        verifyStopped(button);
        QVERIFY(!button->isEnabled());

        auto *resize=visualItem(card,"componentResize_r"); QVERIFY(resize);
        const auto resizePoint=resize->mapToScene(QPointF(resize->width()/2,resize->height()/2)).toPoint();
        const auto narrowPoint=resizePoint-QPoint(120,0);
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,resizePoint);
        QTest::mouseMove(window,narrowPoint,1);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,narrowPoint);
        QTRY_COMPARE(card->width(),384.0);
        QVERIFY(qAbs(add->mapToScene(QPointF(0,add->height()/2)).y()
                    -button->mapToScene(QPointF(0,button->height()/2)).y())<1.0);

        const auto snapshot=qEnvironmentVariable("UART_QUICK_SEND_SNAPSHOT");
        if(!snapshot.isEmpty()) { QTest::qWait(60); QVERIFY(window->grabWindow().save(snapshot)); }
        for(int i=0;i<5;++i) QVERIFY(QMetaObject::invokeMethod(add,"clicked"));
        auto *rows=visualItem(card,"sendParameterRows"); QVERIFY(rows);
        auto *last=visualItem(card,"parameterValue_7"); QVERIFY(last);
        QTRY_VERIFY_WITH_TIMEOUT(rows->property("contentHeight").toReal()>rows->height(),1500);
        QTest::qWait(60); // Let the add-row scroll and layout settle before delivering a wheel event.
        rows->setProperty("contentY",rows->property("contentHeight").toReal()-rows->height());
        const auto beforeScroll=rows->property("contentY").toReal();
        QTest::mouseMove(window,last->mapToScene(QPointF(last->width()/2,last->height()/2)).toPoint());
        QWheelEvent scroll(last->mapToScene(QPointF(last->width()/2,last->height()/2)),
                           last->mapToGlobal(QPointF(last->width()/2,last->height()/2)),
                           QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        scroll.setTimestamp(QTest::lastMouseTimestamp+120);
        QCoreApplication::sendEvent(window,&scroll);
        QTRY_VERIFY_WITH_TIMEOUT(rows->property("contentY").toReal()<beforeScroll,1500);
        QVERIFY(last->property("text").toString().isEmpty());
        window->close();
#endif
    }
    void componentLayoutRoundTrip() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const auto file=QUrl::fromLocalFile(temporary.filePath("shared.json"));
        const auto invalid=QUrl::fromLocalFile(temporary.filePath("invalid.json"));
        {
            QFile broken(invalid.toLocalFile()); QVERIFY(broken.open(QIODevice::WriteOnly));
            QVERIFY(broken.write("{\"format\":\"uart-tool.component-layout\",\"version\":2,\"components\":[]}")>0);
        }
        const QVariantMap send{{"mode",2},{"text","AA 01 FF\n"},{"ending","crlf"},{"repeat",true},{"intervalMs","250"},
            {"parameters",QVariantList{
                QVariantMap{{"name","kp"},{"value","1.25"},{"step","0.05"}},
                QVariantMap{{"name","ki"},{"value","-2e-2"},{"step","0.1"}}}}};
        const QVariantMap layout{{"format","uart-tool.component-layout"},{"version",1},
            {"activeIndex",2},{"maximizedIndex",2},{"scrollX",0},{"scrollY",0},
            {"stackOrder",QVariantList{1,0,2}},
            {"components",QVariantList{
                QVariantMap{{"type","time-plot"},{"x",15},{"y",28},{"width",400},{"height",230},
                            {"referenceEnabled",false},{"referenceValue",2.5}},
                QVariantMap{{"type","xy-plot"},{"x",85},{"y",99},{"width",330},{"height",220}},
                QVariantMap{{"type","quick-send"},{"x",175},{"y",188},{"width",410},{"height",280},
                            {"name",QStringLiteral("轮速参数")},{"send",send}}}}};
        auto configure=[](QQmlApplicationEngine &engine,ApplicationServices &services) {
            engine.rootContext()->setContextProperty("appSettings",&services.settings());
            engine.rootContext()->setContextProperty("serialConnection",&services.serialConnection());
            engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
            engine.rootContext()->setContextProperty("protocolSession",&services.protocolSession());
            engine.rootContext()->setContextProperty("numericStream",&services.numericStream());
            engine.rootContext()->setContextProperty("sendSession",&services.sendSession());
            engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml")));
        };
        auto checkWidgets=[](QQuickWindow *window) {
            auto *workspace=window->findChild<QQuickItem *>("componentWorkspace"); QVERIFY(workspace);
            QCOMPARE(workspace->property("widgetCount").toInt(),3);
            auto *time=visualItem(workspace,"timePlotCard_1"); QVERIFY(time);
            auto *xy=visualItem(workspace,"xyPlotCard_2"); QVERIFY(xy);
            auto *quick=visualItem(workspace,"quickSendCard_3"); QVERIFY(quick);
            for (auto *card : {time,xy,quick}) QVERIFY(!card->property("selected").toBool());
            QVERIFY(xy->parentItem()->z()<time->parentItem()->z());
            QVERIFY(time->parentItem()->z()<quick->parentItem()->z());
            QVERIFY(quick->property("maximized").toBool());
            QCOMPARE(quick->property("title").toString(),QStringLiteral("轮速参数"));
            QVERIFY(!time->property("referenceEnabled").toBool());
            QCOMPARE(time->property("referenceValue").toDouble(),2.5);
            QCOMPARE(time->property("selectedKeys").toStringList(),QStringList());
            QVERIFY(xy->property("xKey").toString().isEmpty());
            QVERIFY(xy->property("yKey").toString().isEmpty());
            QCOMPARE(visualItem(quick,"sendMode")->property("currentIndex").toInt(),2);
            QCOMPARE(visualItem(quick,"sendInput")->property("text").toString(),QString("AA 01 FF\n"));
            QCOMPARE(visualItem(quick,"sendEnding")->property("currentValue").toString(),QString("crlf"));
            QVERIFY(visualItem(quick,"timedSendToggle")->property("checked").toBool());
            QCOMPARE(visualItem(quick,"timedSendInterval")->property("text").toString(),QString("250"));
            QCOMPARE(visualItem(quick,"parameterValue_0")->property("text").toString(),QString("1.25"));
            QCOMPARE(visualItem(quick,"parameterValue_0")->property("stepText").toString(),QString("0.05"));
            QCOMPARE(visualItem(quick,"parameterName_1")->property("text").toString(),QString("ki"));
            QCOMPARE(visualItem(quick,"parameterValue_1")->property("text").toString(),QString("-2e-2"));
            QCOMPARE(visualItem(quick,"sendButton")->property("text").toString(),QStringLiteral("发送"));
        };

        {
            ApplicationServices services;
            auto *store=services.settings().componentLayouts();
            QVERIFY(store->lastLayout().isEmpty());
            QVERIFY(store->exportLayout(layout,file));
            QQmlApplicationEngine engine;
            configure(engine,services);
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
            auto *workspace=window->findChild<QQuickItem *>("componentWorkspace"); QVERIFY(workspace);
            QCOMPARE(workspace->property("widgetCount").toInt(),0);
            QVERIFY(QMetaObject::invokeMethod(workspace,"importLayoutFrom",Q_ARG(QVariant,QVariant::fromValue(file))));
            checkWidgets(window);
            QVERIFY(QMetaObject::invokeMethod(workspace,"importLayoutFrom",Q_ARG(QVariant,QVariant::fromValue(invalid))));
            QVERIFY(!store->lastError().isEmpty());
            checkWidgets(window);
            auto *time=visualItem(workspace,"timePlotCard_1"); QVERIFY(time);
            auto *xy=visualItem(workspace,"xyPlotCard_2"); QVERIFY(xy);
            QVERIFY(time->setProperty("selectedKeys",QStringList{"1:x:-1"}));
            QVERIFY(xy->setProperty("xKey","1:x:-1"));
            QVERIFY(xy->setProperty("yKey","1:y:-1"));
            auto *quick=visualItem(workspace,"quickSendCard_3"); QVERIFY(quick);
            QVERIFY(QMetaObject::invokeMethod(quick,"activated"));
            QVERIFY(quick->property("selected").toBool());
            QVERIFY(QMetaObject::invokeMethod(workspace,"exportLayoutTo",Q_ARG(QVariant,QVariant::fromValue(file))));
            const auto exported=store->importLayout(file);
            QVERIFY(!exported.isEmpty());
            QVERIFY(!exported.contains("activeIndex"));
            QVERIFY(!exported.value("components").toList().first().toMap().contains("selectedKeys"));
            QVERIFY(window->close());
            QCOMPARE(store->lastLayout(),store->importLayout(file));
        }
        {
            ApplicationServices services;
            QQmlApplicationEngine engine;
            configure(engine,services);
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
            checkWidgets(window);
            auto *workspace=window->findChild<QQuickItem *>("componentWorkspace"); QVERIFY(workspace);
            auto *quick=visualItem(workspace,"quickSendCard_3"); QVERIFY(quick);
            QVERIFY(QMetaObject::invokeMethod(quick,"maximizeRequested"));
            QCOMPARE(quick->width(),410.0);
            QCOMPARE(quick->height(),280.0);
            QCOMPARE(quick->parentItem()->position(),QPointF(175,188));
            QVERIFY(window->close());
        }
    }
    void chartComponentUi() {
        ApplicationServices services; auto &protocol=services.protocolSession(); auto &series=services.numericStream();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("numericStream",&series);
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&services.serialConnection());
        engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
        engine.rootContext()->setContextProperty("protocolSession",&protocol);
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml"))); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QTRY_VERIFY(window->isExposed()); QTest::qWait(60);
        auto *tab=window->findChild<QQuickItem *>("componentMenuTab"); QVERIFY(tab); QVERIFY(QMetaObject::invokeMethod(tab,"clicked"));
        auto *menu=window->findChild<QQuickItem *>("componentMenu"); QVERIFY(menu); QTRY_VERIFY(menu->isVisible());
        auto *workspace=window->findChild<QQuickItem *>("componentWorkspace"); QVERIFY(workspace); QCOMPARE(workspace->property("widgetCount").toInt(),0);
        QTRY_VERIFY(workspace->width()>100 && workspace->height()>100); QTest::qWait(60);
        auto *tile=window->findChild<QQuickItem *>("timePlotComponent"); QVERIFY(tile);
        const auto source=tile->mapToScene(QPointF(70,40)).toPoint();
        const auto target=workspace->mapToScene(QPointF(32,70)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,source);
        QTest::mouseMove(window,source+QPoint(25,10),1);
        QTest::mouseMove(window,target,1);
        QTRY_VERIFY(workspace->property("dragHighlight").toBool());
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,target);
        QTRY_COMPARE(workspace->property("widgetCount").toInt(),1);
        QQuickItem *card=nullptr; QTRY_VERIFY((card=visualItem(window->contentItem(),"timePlotCard_1")));
        const auto placed=card->mapToScene(QPointF());
        QVERIFY(std::abs(placed.x()-target.x()+16)<=1 && std::abs(placed.y()-target.y()+16)<=1);
        // A workspace component has reusable move/resize/maximize chrome.
        auto *geometry=card->parentItem(); QVERIFY(geometry);
        auto *moveHandle=visualItem(card,"componentMoveHandle"); QVERIFY(moveHandle);
        const auto beforePosition=geometry->position();
        const auto moveStart=moveHandle->mapToScene(QPointF(24,15)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,moveStart);
        QTest::mouseMove(window,moveStart+QPoint(0,18),1);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,moveStart+QPoint(0,18));
        QTRY_VERIFY(geometry->y()>beforePosition.y()+10);
        auto *resize=visualItem(card,"componentResize_rb"); QVERIFY(resize);
        const auto oldSize=geometry->size(); const auto resizeStart=resize->mapToScene(QPointF(4,4)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,resizeStart);
        QTest::mouseMove(window,resizeStart+QPoint(30,20),1);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,resizeStart+QPoint(30,20));
        QTRY_VERIFY(geometry->width()>oldSize.width()+20); QTRY_VERIFY(geometry->height()>oldSize.height()+10);
        const auto restoredSize=geometry->size();
        QVERIFY(QMetaObject::invokeMethod(card,"maximizeRequested")); QTRY_VERIFY(card->property("maximized").toBool());
        QVERIFY(QMetaObject::invokeMethod(card,"maximizeRequested")); QTRY_VERIFY(!card->property("maximized").toBool());
        QCOMPARE(geometry->size(),restoredSize);
        auto *plot=visualItem(window->contentItem(),"timePlot_1"); QVERIFY(plot);
        auto *choose=visualItem(window->contentItem(),"plotParameters_1"); QVERIFY(choose && choose->isEnabled());
        const auto focusPoint=plot->mapToScene(QPointF(plot->width()/2,plot->height()/2)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,focusPoint);
        QTRY_VERIFY_WITH_TIMEOUT(plot->property("focusActive").toBool(),1800);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,focusPoint);
        QVERIFY(!plot->property("focusActive").toBool());
        QVERIFY(plot->property("referenceEnabled").toBool());
        QCOMPARE(plot->property("referenceValue").toDouble(),0.0);
        const auto withReference=grabItem(plot); QVERIFY(!withReference.isNull());
        QVERIFY(QMetaObject::invokeMethod(choose,"clicked"));
        auto *popup=card->findChild<QObject *>("plotParametersPopup_1"); QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        auto *popupContent=popup->property("contentItem").value<QQuickItem *>(); QVERIFY(popupContent);
        auto *referenceToggle=visualItem(popupContent,"plotReferenceToggle_1"); QVERIFY(referenceToggle);
        auto *referenceInput=visualItem(popupContent,"plotReferenceValue_1"); QVERIFY(referenceInput);
        const auto clickControl=[](QQuickItem *item) {
            const auto center=item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint();
            QTest::mouseClick(item->window(),Qt::LeftButton,Qt::NoModifier,center);
        };
        clickControl(referenceToggle);
        QTRY_VERIFY(!plot->property("referenceEnabled").toBool());
        QVERIFY(!referenceInput->isEnabled());
        const auto withoutReference=grabItem(plot); QVERIFY(!withoutReference.isNull());
        QVERIFY(withReference!=withoutReference);
        clickControl(referenceToggle);
        QTRY_VERIFY(referenceInput->isEnabled());
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        clickControl(referenceInput);
        QTRY_VERIFY_WITH_TIMEOUT(referenceInput->hasActiveFocus(),1500);
        QVERIFY(referenceInput->setProperty("text","-2.5"));
        QTest::keyClick(referenceInput->window(),Qt::Key_Return);
        QTRY_COMPARE(plot->property("referenceValue").toDouble(),-2.5);
        QVERIFY(plot->property("referenceEnabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(popup,"close"));
        QTRY_VERIFY(!popup->property("visible").toBool());

        QVERIFY(protocol.selectProtocol("keyvalue"));
        protocol.feedBytes("awa:2.0, aaa:3\n"); QTRY_COMPARE(series.channels().size(),2);
        QVERIFY(QMetaObject::invokeMethod(choose,"clicked"));
        QTRY_VERIFY(popup->property("opened").toBool());
        QTest::qWait(100); // Allow the popup/viewport visual parenting to settle.
        for(int i=0;i<2;++i) {
            QQuickItem *choice=nullptr; QTRY_VERIFY((choice=visualItem(popupContent,"plotChannel_1_"+QString::number(i))) && choice->width()>0 && choice->window());
            QTest::mouseClick(choice->window(),Qt::LeftButton,Qt::NoModifier,choice->mapToScene(QPointF(choice->width()/2,choice->height()/2)).toPoint());
        }
        QVERIFY(QMetaObject::invokeMethod(popup,"close"));
        services.settings().setDarkMode(true);
        for(int i=0;i<75;++i) {
            const auto line=QString("awa:%1, aaa:%2\n").arg(std::sin(i*0.13)*5).arg(std::cos(i*0.12)*2).toUtf8();
            protocol.feedBytes(line); QTest::qWait(30);
        }
        QCOMPARE(plot->property("channels").toStringList().size(),2);
        const auto selected=plot->property("channels").toStringList();
        const auto beforeSparse=series.points(selected.at(0),-1,series.latestTime()).size();
        protocol.feedBytes("current:0.8\n");
        QTRY_COMPARE(series.channels().size(),3);
        QTRY_COMPARE(protocol.latestFields().size(),3);
        QCOMPARE(plot->property("channels").toStringList(),selected);
        QCOMPARE(series.points(selected.at(0),-1,series.latestTime()).size(),beforeSparse);
        // Wheel routing follows the hovered plot/axis; manual view survives new samples.
        auto wheelAt=[&](QPointF position) {
            const auto scene=plot->mapToScene(position), global=plot->mapToGlobal(position);
            QWheelEvent event(scene,global,QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QCoreApplication::sendEvent(plot->window(),&event);
        };
        wheelAt({plot->width()/2,plot->height()/2});
        QVERIFY(plot->property("timeSpan").toDouble()<10); QVERIFY(!plot->property("following").toBool()); QVERIFY(!plot->property("autoRange").toBool());
        protocol.feedBytes("awa:4, aaa:2\n"); QTest::qWait(60); QVERIFY(!plot->property("following").toBool());
        QVERIFY(QMetaObject::invokeMethod(plot,"resetView"));
        wheelAt({plot->width()/2,plot->height()-5});
        QVERIFY(plot->property("timeSpan").toDouble()<10); QVERIFY(plot->property("autoRange").toBool());
        QVERIFY(QMetaObject::invokeMethod(plot,"resetView"));
        wheelAt({10,plot->height()/2});
        QCOMPARE(plot->property("timeSpan").toDouble(),10.); QVERIFY(plot->property("following").toBool()); QVERIFY(!plot->property("autoRange").toBool());
        QVERIFY(QMetaObject::invokeMethod(plot,"resetView"));
        const auto dragPlot=[&](QPointF start,Qt::MouseButton button=Qt::LeftButton) {
            const auto scene=plot->mapToScene(start).toPoint();
            const auto end=scene+QPoint(30,20);
            QTest::mousePress(window,button,Qt::NoModifier,scene);
            QTest::mouseMove(window,end,1);
            QTest::mouseRelease(window,button,Qt::NoModifier,end);
        };
        const auto span=plot->property("timeSpan").toDouble();
        dragPlot({plot->width()/2,plot->height()/2});
        QCOMPARE(plot->property("timeSpan").toDouble(),span);
        QVERIFY(!plot->property("following").toBool());
        QVERIFY(!plot->property("autoRange").toBool());
        QVERIFY(!plot->property("focusActive").toBool());
        QVERIFY(QMetaObject::invokeMethod(plot,"resetView"));
        dragPlot({plot->width()/2,plot->height()-12});
        QVERIFY(!plot->property("following").toBool());
        QVERIFY(plot->property("autoRange").toBool());
        QVERIFY(QMetaObject::invokeMethod(plot,"resetView"));
        dragPlot({25,plot->height()/2});
        QVERIFY(plot->property("following").toBool());
        QVERIFY(!plot->property("autoRange").toBool());
        QVERIFY(QMetaObject::invokeMethod(plot,"resetView"));
        dragPlot({plot->width()/2,plot->height()/2},Qt::MiddleButton);
        QVERIFY(plot->property("following").toBool());
        QVERIFY(plot->property("autoRange").toBool());
        const auto snapshot=qEnvironmentVariable("UART_CHART_SNAPSHOT"); if(!snapshot.isEmpty()) { QTest::qWait(70); QVERIFY(window->grabWindow().save(snapshot)); }
        auto *componentDock=window->findChild<QQuickItem *>("componentsDock"); QVERIFY(componentDock); QVERIFY(QMetaObject::invokeMethod(componentDock,"undock"));
        auto *floating=window->findChild<QQuickWindow *>("componentsFloatingWindow"); QVERIFY(floating); QTRY_COMPARE(workspace->window(),floating);
        QVERIFY(QTest::qWaitForWindowExposed(floating));
        auto *receiver=window->findChild<QQuickItem *>("receiveWorkspace"); QVERIFY(receiver); QCOMPARE(receiver->window(),window);
        const auto targetGlobal=workspace->mapToGlobal(QPointF(workspace->width()/2,workspace->height()/2)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,tile->mapToScene(QPointF(70,40)).toPoint());
        QTest::mouseMove(window,tile->mapToScene(QPointF(95,50)).toPoint(),1);
        QTest::mouseMove(window,window->mapFromGlobal(targetGlobal),1); QTRY_VERIFY(workspace->property("dragHighlight").toBool());
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,window->mapFromGlobal(targetGlobal)); QTRY_COMPARE(workspace->property("widgetCount").toInt(),2);
        QQuickItem *secondCard=nullptr; QTRY_VERIFY((secondCard=visualItem(floating->contentItem(),"timePlotCard_2")));
        const auto secondPosition=secondCard->mapToGlobal(QPointF());
        QVERIFY(std::abs(secondPosition.x()-targetGlobal.x()+16)<=1 && std::abs(secondPosition.y()-targetGlobal.y()+16)<=1);
        auto *componentTracker=componentDock->findChild<DockWindowController *>("componentsDockController"); QVERIFY(componentTracker);
        auto *dockArea=window->findChild<QQuickItem *>("dockWorkspace"); QVERIFY(dockArea);
        const auto dockTarget=QPointF(contentDockRect(window).center().x(),contentDockRect(window).bottom()-10);
        componentTracker->updateDrag(dockTarget); QVERIFY(componentTracker->dockReady());
        componentTracker->endDrag(dockTarget); QTRY_COMPARE(workspace->window(),window);
        QTRY_VERIFY(!componentDock->property("floating").toBool());
        QCOMPARE(workspace->property("widgetCount").toInt(),2); QCOMPARE(series.channels().size(),3);
        QVERIFY(protocol.selectProtocol("none")); QVERIFY(series.channels().isEmpty()); QTRY_VERIFY(plot->property("channels").toStringList().isEmpty()); QVERIFY(choose->isEnabled());
        window->close();
    }
    void adaptivePlotGrid() {
        TimeSeriesPlot time;
        XYPlot xy;
        time.setReferenceEnabled(false);
        xy.setXReferenceEnabled(false);
        xy.setYReferenceEnabled(false);
        const auto gridLines=[](QQuickPaintedItem &plot,QSize size) {
            plot.setWidth(size.width());
            plot.setHeight(size.height());
            QImage image(size,QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            plot.paint(&painter);
            painter.end();

            const auto count=[&](int length,auto pixelAt) {
                int lines=0;
                bool previous=false;
                for (int i=0;i<length;++i) {
                    const auto color=pixelAt(i);
                    const bool green=color.alpha()>0 && color.green()>color.red();
                    if (green && !previous) ++lines;
                    previous=green;
                }
                return lines;
            };
            return QSize(count(size.width(),[&](int x) { return image.pixelColor(x,20); }),
                         count(size.height(),[&](int y) { return image.pixelColor(80,y); }));
        };
        for (auto *plot : {static_cast<QQuickPaintedItem *>(&time),static_cast<QQuickPaintedItem *>(&xy)}) {
            QVERIFY(plot->setProperty("gridColor",QColor(Qt::green)));
            QVERIFY(plot->setProperty("textColor",QColor(Qt::black)));
            const auto small=gridLines(*plot,{230,130});
            const auto large=gridLines(*plot,{720,420});
            QVERIFY(small.width()>=2 && small.height()>=2);
            QVERIFY(large.width()>small.width() && large.height()>small.height());
        }
        QCOMPARE(gridLines(time,{300,140}).height(),5);
        QCOMPARE(gridLines(xy,{300,140}).height(),5);
        for (int i=0;i<3;++i) {
            const QSize size(390,304+28*i);
            QCOMPARE(gridLines(time,size).height(),6+i);
            QCOMPARE(gridLines(xy,size).height(),6+i);
        }
    }
    void xyPlotUi() {
        ApplicationServices services;
        auto &protocol=services.protocolSession();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("numericStream",&services.numericStream());
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&services.serialConnection());
        engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
        engine.rootContext()->setContextProperty("protocolSession",&protocol);
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QTRY_VERIFY(window->isExposed());
        auto *tab=window->findChild<QQuickItem *>("componentMenuTab"); QVERIFY(tab);
        QVERIFY(QMetaObject::invokeMethod(tab,"clicked"));
        auto *tile=visualItem(window->contentItem(),"xyPlotComponent");
        auto *workspace=window->findChild<QQuickItem *>("componentWorkspace"); QVERIFY(workspace);
        QTRY_VERIFY(tile && tile->isVisible() && tile->width()>100 && workspace->width()>100);
        const auto source=tile->mapToScene(QPointF(70,40)).toPoint();
        const auto target=workspace->mapToScene(QPointF(12,100)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,source);
        QTest::mouseMove(window,source+QPoint(25,10),1);
        QTest::mouseMove(window,target,1);
        QTRY_VERIFY(workspace->property("dragHighlight").toBool());
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,target);

        QQuickItem *plot=nullptr; QTRY_VERIFY((plot=visualItem(window->contentItem(),"xyPlot_1")));
        auto *parameters=visualItem(window->contentItem(),"xyParameters_1"); QVERIFY(parameters);
        auto *card=visualItem(window->contentItem(),"xyPlotCard_1"); QVERIFY(card);
        QCOMPARE(card->width(),card->height());
        QVERIFY(parameters->isEnabled());
        QVERIFY(plot->property("xReferenceEnabled").toBool());
        QVERIFY(plot->property("yReferenceEnabled").toBool());
        QCOMPARE(plot->property("xReferenceValue").toDouble(),0.0);
        QCOMPARE(plot->property("yReferenceValue").toDouble(),0.0);
        const auto center=[](QQuickItem *item) {
            return item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint();
        };
        const auto initial=grabItem(plot); QVERIFY(!initial.isNull());
        const auto point=center(plot);
        const auto drag=[&](const QPoint &start,const QPoint &end,Qt::MouseButton button=Qt::LeftButton) {
            QTest::mousePress(window,button,Qt::NoModifier,start);
            QTest::mouseMove(window,end,1);
            QTest::mouseRelease(window,button,Qt::NoModifier,end);
        };
        drag(point,point+QPoint(35,25),Qt::MiddleButton);
        QCOMPARE(grabItem(plot),initial);
        drag(point,point+QPoint(35,25));
        const auto dragged=grabItem(plot); QVERIFY(!dragged.isNull() && dragged!=initial);
        QVERIFY(!plot->property("focusActive").toBool());
        QVERIFY(QMetaObject::invokeMethod(plot,"resetView"));
        QCOMPARE(grabItem(plot),initial);
        const auto xAxis=plot->mapToScene(QPointF(plot->width()/2,plot->height()-12)).toPoint();
        drag(xAxis,xAxis+QPoint(35,0));
        const auto panned=grabItem(plot); QVERIFY(!panned.isNull() && panned!=initial);
        const auto global=window->mapToGlobal(point);
        QWheelEvent wheel(point,global,QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(window,&wheel);
        const auto zoomed=grabItem(plot); QVERIFY(!zoomed.isNull() && zoomed!=panned);

        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,point);
        QTest::mouseMove(window,point+QPoint(2,1),1);
        QTRY_VERIFY_WITH_TIMEOUT(plot->property("focusActive").toBool(),1800);
        const auto crosshair=grabItem(plot); QVERIFY(!crosshair.isNull() && crosshair!=zoomed);
        QTest::mouseMove(window,point+QPoint(25,20),1);
        QVERIFY(plot->property("focusActive").toBool());
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,point+QPoint(25,20));
        QVERIFY(!plot->property("focusActive").toBool());
        QCOMPARE(grabItem(plot),zoomed);

        auto *parameterPopup=card->findChild<QObject *>("xyParametersPopup_1"); QVERIFY(parameterPopup);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,center(parameters));
        QTRY_VERIFY(parameterPopup->property("opened").toBool());
        auto *content=parameterPopup->property("contentItem").value<QQuickItem *>(); QVERIFY(content);
        auto *focusToggle=visualItem(content,"xyFocusToggle_1"); QVERIFY(focusToggle);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,center(focusToggle));
        QTRY_VERIFY(!plot->property("focusEnabled").toBool());
        auto *xInput=visualItem(content,"plotReferenceValue_x_1"); QVERIFY(xInput);
        QVERIFY(xInput->setProperty("text","1.5"));
        QVERIFY(QMetaObject::invokeMethod(xInput,"editingFinished"));
        QCOMPARE(plot->property("xReferenceValue").toDouble(),1.5);
        auto *yToggle=visualItem(content,"plotReferenceToggle_y_1"); QVERIFY(yToggle);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,center(yToggle));
        QTRY_VERIFY(!plot->property("yReferenceEnabled").toBool());
        QVERIFY(protocol.selectProtocol("csv"));
        protocol.feedBytes("1,10\n2,20\n3,10\n");
        auto chooseField=[&](const QString &axis,int index) {
            auto *selector=visualItem(content,"xyAxis_1_"+axis); QVERIFY(selector);
            QTRY_VERIFY(selector->isEnabled());
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,center(selector));
            auto *fieldPopup=selector->property("popup").value<QObject *>(); QVERIFY(fieldPopup);
            QTRY_VERIFY(fieldPopup->property("opened").toBool());
            auto *list=fieldPopup->property("contentItem").value<QQuickItem *>(); QVERIFY(list);
            QQuickItem *option=nullptr;
            QTRY_VERIFY(QMetaObject::invokeMethod(list,"itemAtIndex",Q_RETURN_ARG(QQuickItem *,option),
                                                  Q_ARG(int,index)) && option);
            QTest::mouseClick(option->window(),Qt::LeftButton,Qt::NoModifier,center(option));
        };
        chooseField("x",0);
        QTRY_COMPARE_WITH_TIMEOUT(plot->property("xChannel").toString(),QStringLiteral("3:CH1:-1"),1500);
        chooseField("y",1);
        QTRY_COMPARE_WITH_TIMEOUT(plot->property("yChannel").toString(),QStringLiteral("3:CH2:-1"),1500);
        QTRY_COMPARE(plot->property("pointCount").toInt(),3);

        QVERIFY(QMetaObject::invokeMethod(parameterPopup,"close"));
        QTRY_VERIFY(!parameterPopup->property("visible").toBool());
        const auto snapshot=qEnvironmentVariable("UART_XY_SNAPSHOT");
        if(!snapshot.isEmpty()) { QTest::qWait(60); QVERIFY(window->grabWindow().save(snapshot)); }

        protocol.setDisplayPaused(true);
        protocol.feedBytes("4,40\n");
        QTest::qWait(75);
        QCOMPARE(plot->property("pointCount").toInt(),3);
        protocol.setDisplayPaused(false);
        QTRY_COMPARE(plot->property("pointCount").toInt(),4);
        QVERIFY(protocol.selectProtocol("none"));
        QTRY_COMPARE(plot->property("pointCount").toInt(),0);
        QVERIFY(parameters->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(workspace,"addWidget",
            Q_ARG(QVariant,QVariant("time-plot")),Q_ARG(QVariant,QPointF(24,0))));
        auto *lowerCard=visualItem(window->contentItem(),"timePlotCard_2"); QVERIFY(lowerCard);
        auto *lowerPlot=visualItem(lowerCard,"timePlot_2"); QVERIFY(lowerPlot);
        QVERIFY(QMetaObject::invokeMethod(card,"activated"));
        QVERIFY(card->property("selected").toBool());
        const auto overlap=center(parameters);
        QTRY_VERIFY(lowerPlot->contains(lowerPlot->mapFromScene(overlap)));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,overlap);
        QVERIFY(card->property("selected").toBool());
        QVERIFY(!lowerCard->property("selected").toBool());

        QTest::keyClick(window,Qt::Key_Escape);
        const auto exposedPlot=lowerPlot->mapToScene(QPointF(lowerPlot->width()-28,lowerPlot->height()/2)).toPoint();
        QVERIFY(!card->contains(card->mapFromScene(exposedPlot)));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,exposedPlot);
        QVERIFY(lowerCard->property("selected").toBool());

        const auto exposedXY=plot->mapToScene(QPointF(plot->width()/2,plot->height()-34)).toPoint();
        QVERIFY(!lowerCard->contains(lowerCard->mapFromScene(exposedXY)));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,exposedXY);
        QVERIFY(card->property("selected").toBool());
        QVERIFY(card->parentItem()->z()>lowerCard->parentItem()->z());

        QVERIFY(QMetaObject::invokeMethod(workspace,"addWidget",
            Q_ARG(QVariant,QVariant("time-plot")),Q_ARG(QVariant,QPointF(24,0))));
        auto *newCard=visualItem(window->contentItem(),"timePlotCard_3"); QVERIFY(newCard);
        QVERIFY(newCard->property("selected").toBool());
        QVERIFY(newCard->parentItem()->z()>card->parentItem()->z());
        QVERIFY(card->parentItem()->z()>lowerCard->parentItem()->z());
        auto *canvas=visualItem(workspace,"componentCanvas"); QVERIFY(canvas);
        const auto blank=canvas->mapToScene(QPointF(canvas->width()-24,canvas->height()-24)).toPoint();
        for (auto *item : {card,lowerCard,newCard})
            QVERIFY(!item->contains(item->mapFromScene(blank)));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,blank);
        QVERIFY(!newCard->property("selected").toBool());
        QVERIFY(newCard->parentItem()->z()>card->parentItem()->z());
        QVERIFY(card->parentItem()->z()>lowerCard->parentItem()->z());
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,exposedXY);
        QVERIFY(card->property("selected").toBool());
        QTest::keyClick(window,Qt::Key_C,Qt::ControlModifier);
        QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
        QTRY_COMPARE(workspace->property("widgetCount").toInt(),4);
        auto *duplicate=visualItem(window->contentItem(),"xyPlotCard_4"); QVERIFY(duplicate);
        QVERIFY(duplicate->property("selected").toBool());
        QCOMPARE(duplicate->size(),card->size());
        QCOMPARE(duplicate->property("xReferenceValue").toDouble(),1.5);
        QCOMPARE(duplicate->property("yReferenceEnabled").toBool(),false);
        QTest::keyClick(window,Qt::Key_Delete);
        QTRY_COMPARE(workspace->property("widgetCount").toInt(),3);

        auto rightClickAction=[&](QQuickItem *item,const QPoint &point,const QString &action) {
            QTest::mouseClick(window,Qt::RightButton,Qt::NoModifier,point);
            auto *menu=item->findChild<QObject *>("componentContextMenu"); QVERIFY(menu);
            QTRY_VERIFY(menu->property("opened").toBool());
            auto *menuContent=menu->property("contentItem").value<QQuickItem *>(); QVERIFY(menuContent);
            auto *entry=visualItem(menuContent,action); QVERIFY(entry && entry->isEnabled());
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,center(entry));
        };
        rightClickAction(card,exposedXY,"copyComponentAction");
        rightClickAction(card,exposedXY,"pasteComponentAction");
        QTRY_COMPARE(workspace->property("widgetCount").toInt(),4);
        auto *pasted=visualItem(window->contentItem(),"xyPlotCard_5"); QVERIFY(pasted);
        rightClickAction(pasted,center(pasted),"deleteComponentAction");
        QTRY_COMPARE(workspace->property("widgetCount").toInt(),3);
        const auto pastePoint=canvas->mapToScene(QPointF(76,canvas->height()-125)).toPoint();
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,pastePoint);
        QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
        QTRY_COMPARE(workspace->property("widgetCount").toInt(),4);
        auto *keyboardPaste=visualItem(window->contentItem(),"xyPlotCard_6"); QVERIFY(keyboardPaste);
        QCOMPARE(keyboardPaste->mapToScene(QPointF()).toPoint(),pastePoint-QPoint(16,16));
        QTest::keyClick(window,Qt::Key_Delete);
        QTRY_COMPARE(workspace->property("widgetCount").toInt(),3);

        QTest::mouseClick(window,Qt::RightButton,Qt::NoModifier,pastePoint);
        auto *canvasMenu=workspace->findChild<QObject *>("componentCanvasMenu"); QVERIFY(canvasMenu);
        QTRY_VERIFY(canvasMenu->property("opened").toBool());
        auto *canvasMenuContent=canvasMenu->property("contentItem").value<QQuickItem *>(); QVERIFY(canvasMenuContent);
        auto *pasteAction=visualItem(canvasMenuContent,"pasteOnCanvasAction"); QVERIFY(pasteAction && pasteAction->isEnabled());
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,center(pasteAction));
        QTRY_COMPARE(workspace->property("widgetCount").toInt(),4);
        auto *menuPaste=visualItem(window->contentItem(),"xyPlotCard_7"); QVERIFY(menuPaste);
        QCOMPARE(menuPaste->mapToScene(QPointF()).toPoint(),pastePoint-QPoint(16,16));
        window->close();
    }
    void mainTitleBarUi() {
        ApplicationServices services;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&services.serialConnection());
        engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
        engine.rootContext()->setContextProperty("protocolSession",&services.protocolSession());
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(window->flags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto *dragArea=window->findChild<QQuickItem *>("mainTitleDragArea");
        auto *maximize=window->findChild<QQuickItem *>("mainMaximizeButton");
        auto *minimize=window->findChild<QQuickItem *>("mainMinimizeButton");
        auto *close=window->findChild<QQuickItem *>("mainCloseButton");
        auto *grip=visualItem(window->contentItem(),"mainWindowResize_rb");
        QVERIFY(dragArea && maximize && minimize && close && grip);
        auto click=[&](QQuickItem *item) {
            const auto point=item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint();
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point);
        };

        QVERIFY(grip->isVisible());
        click(maximize);
        QTRY_COMPARE(window->visibility(),QWindow::Maximized);
        QVERIFY(!grip->isVisible());
        click(maximize);
        QTRY_COMPARE(window->visibility(),QWindow::Windowed);
        QVERIFY(grip->isVisible());

        const auto titlePoint=dragArea->mapToScene(QPointF(dragArea->width()/2,dragArea->height()/2)).toPoint();
        QTest::mouseDClick(window,Qt::LeftButton,Qt::NoModifier,titlePoint);
        QTRY_COMPARE(window->visibility(),QWindow::Maximized);
        QSignalSpy minimizeClicks(minimize,SIGNAL(clicked()));
        click(minimize);
        QCOMPARE(minimizeClicks.count(),1);
        if (QGuiApplication::platformName().startsWith("wayland")) {
            // xdg-shell does not report minimized state or offer a restore request.
            window->hide();
        } else {
            QTRY_COMPARE(window->visibility(),QWindow::Minimized);
        }
        window->showNormal();
        QTRY_COMPARE(window->visibility(),QWindow::Windowed);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        click(close);
        QTRY_VERIFY(!window->isVisible());
    }
    void halfDockingUi() {
        ApplicationServices services; QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("numericStream",&services.numericStream());
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&services.serialConnection());
        engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
        engine.rootContext()->setContextProperty("protocolSession",&services.protocolSession());
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml"))); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *serial=window->findChild<QQuickItem *>("connectionDock");
        auto *components=window->findChild<QQuickItem *>("componentsDock");
        auto *receive=window->findChild<QQuickItem *>("receiveDock");
        auto *area=window->findChild<QQuickItem *>("dockWorkspace");
        auto *tracker=components->findChild<DockWindowController *>("componentsDockController");
        auto *overlay=window->findChild<QQuickItem *>("dockOverlay");
        QVERIFY(serial && components && receive && area && tracker && overlay);
        QVERIFY(!window->findChild<QQuickItem *>("componentsEmptySlot"));
        QTRY_VERIFY(window->isExposed()); QTest::qWait(80);
        // Startup has three columns; arrange a vertical pair before collapsing it.
        QVERIFY(serial->mapToScene(QPointF()).x()<components->mapToScene(QPointF()).x());
        QVERIFY(components->mapToScene(QPointF()).x()<receive->mapToScene(QPointF()).x());
        QCOMPARE(components->height(),area->height()); QCOMPARE(receive->height(),area->height());
        const auto originalSize=window->size();
        const auto horizontalRatio=[&] { return components->width()/(components->width()+receive->width()); };
        const auto initialRatio=horizontalRatio();
        const auto horizontalDivider=components->mapToScene(QPointF(components->width()+6,components->height()/2)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,horizontalDivider);
        QTest::mouseMove(window,horizontalDivider-QPoint(50,0),1);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,horizontalDivider-QPoint(50,0));
        QTRY_VERIFY(std::abs(horizontalRatio()-initialRatio)>0.025);
        const auto adjustedHorizontalRatio=horizontalRatio();
        const auto horizontalExtent=components->width()+receive->width();
        window->resize(originalSize+QSize(450,200));
        QTRY_COMPARE(components->width()+receive->width(),horizontalExtent+450);
        QTRY_VERIFY(std::abs(horizontalRatio()-adjustedHorizontalRatio)<0.002);
        window->resize(originalSize);
        QTRY_COMPARE(components->width()+receive->width(),horizontalExtent);
        QTRY_VERIFY(std::abs(horizontalRatio()-adjustedHorizontalRatio)<0.002);
        QVERIFY(QMetaObject::invokeMethod(receive,"undock")); QTest::qWait(40);
        auto *receiveTracker=receive->findChild<DockWindowController *>("receiveDockController"); QVERIFY(receiveTracker);
        const auto bottom=QPointF(contentDockRect(window).center().x(),contentDockRect(window).bottom()-8);
        receiveTracker->updateDrag(bottom); receiveTracker->endDrag(bottom); QTest::qWait(40);
        const auto verticalRatio=[&] { return components->height()/(components->height()+receive->height()); };
        const auto verticalDivider=components->mapToScene(QPointF(components->width()/2,components->height()+6)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,verticalDivider);
        QTest::mouseMove(window,verticalDivider+QPoint(0,65),1);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,verticalDivider+QPoint(0,65));
        QTRY_VERIFY(verticalRatio()>0.55);
        const auto adjustedVerticalRatio=verticalRatio();
        const auto verticalExtent=components->height()+receive->height();
        window->resize(originalSize+QSize(450,200));
        QTRY_COMPARE(components->height()+receive->height(),verticalExtent+200);
        QTRY_VERIFY(std::abs(verticalRatio()-adjustedVerticalRatio)<0.002);
        window->resize(originalSize);
        QTRY_COMPARE(components->height()+receive->height(),verticalExtent);
        QTRY_VERIFY(std::abs(verticalRatio()-adjustedVerticalRatio)<0.002);
        // The receive split may shrink to its title and expand without losing content.
        auto *receiveContent=window->findChild<QQuickItem *>("receiveDockContent"); QVERIFY(receiveContent);
        const auto originalHeight=receive->height();
        auto divider=components->mapToScene(QPointF(components->width()/2,components->height()+6)).toPoint();
        const auto expandedDivider=divider;
        const int collapseDelta=qRound(originalHeight-38)+1;
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,divider);
        QTest::mouseMove(window,divider+QPoint(0,collapseDelta),1);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,divider+QPoint(0,collapseDelta));
        QTRY_COMPARE(receive->height(),38.); QVERIFY(!receiveContent->isVisible());
        const auto collapsedImage=qEnvironmentVariable("UART_COLLAPSED_SNAPSHOT");
        if(!collapsedImage.isEmpty()) { QTest::qWait(40); QVERIFY(window->grabWindow().save(collapsedImage)); }
        divider=components->mapToScene(QPointF(components->width()/2,components->height()+6)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,divider);
        QTest::mouseMove(window,expandedDivider,1);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,expandedDivider);
        QTRY_VERIFY(std::abs(receive->height()-originalHeight)<=1); QVERIFY(receiveContent->isVisible());
        auto rect=[](QQuickItem *item) { return QRectF(item->mapToGlobal(QPointF()),item->size()); };
        auto matches=[&](QQuickItem *item,QRectF planned) {
            const auto actual=rect(item);
            return std::abs(actual.x()-planned.x())<=1 && std::abs(actual.y()-planned.y())<=1
                && std::abs(actual.width()-planned.width())<=1 && std::abs(actual.height()-planned.height())<=1;
        };
        // Repeated local halves: hovering never modifies the current layout.
        for(int cycle=0;cycle<3;++cycle) {
            QVERIFY(QMetaObject::invokeMethod(components,"undock")); QTest::qWait(60);
            QVERIFY(!components->isVisible()); QCOMPARE(receive->height(),area->height());
            const auto sidebar=rect(serial),target=rect(receive);
            tracker->updateDrag(target.center());
            QCOMPARE(tracker->dockTarget(),receive); QVERIFY(!tracker->dockReady()); QVERIFY(!tracker->guidesVisible()); QVERIFY(!overlay->isVisible());
            tracker->updateDrag(receive->mapToGlobal(QPointF(receive->width()/2+80,40)));
            QVERIFY(!tracker->dockReady()); QVERIFY(!tracker->guidesVisible()); QVERIFY(tracker->previewRect().isEmpty());
            tracker->updateDrag(area->mapToGlobal(QPointF(8,8)));
            QVERIFY(!tracker->dockReady()); QVERIFY(!overlay->isVisible());
            const auto top=receive->mapToGlobal(QPointF(receive->width()/2,40));
            tracker->updateDrag(top); QCOMPARE(tracker->dockTarget(),receive); QCOMPARE(tracker->dockEdge(),QString("top"));
            QTRY_VERIFY(overlay->isVisible());
            const auto planned=tracker->previewRect(); QCOMPARE(planned.width(),target.width()); QCOMPARE(planned.height(),(target.height()-12)/2);
            QCOMPARE(rect(serial),sidebar); QCOMPARE(rect(receive),target); QVERIFY(!components->isVisible());
            auto *region=visualItem(overlay,"dockPreviewRegion"); QVERIFY(region); QCOMPARE(region->property("color").value<QColor>().alpha(),0);
            const auto preview=grabItem(overlay); QVERIFY(!preview.isNull()); QCOMPARE(preview.pixelColor(12,12).alpha(),0); QVERIFY(preview.pixelColor(20,1).alpha()>0);
            tracker->endDrag(top); QTest::qWait(60); QVERIFY(matches(components,planned)); QCOMPARE(rect(serial),sidebar);
        }
        // Local horizontal split also keeps the sidebar and divides the target.
        QVERIFY(QMetaObject::invokeMethod(components,"undock")); QTest::qWait(60);
        const auto sidebar=rect(serial),target=rect(receive);
        const auto right=receive->mapToGlobal(QPointF(receive->width()-40,receive->height()/2));
        tracker->updateDrag(right); QCOMPARE(tracker->dockTarget(),receive); QCOMPARE(tracker->dockEdge(),QString("right"));
        const auto horizontal=tracker->previewRect(); QCOMPARE(horizontal.width(),(target.width()-12)/2);
        tracker->endDrag(right); QTest::qWait(60); QVERIFY(matches(components,horizontal)); QCOMPARE(rect(serial),sidebar);
        // Global hover previews a half of the whole workspace without reflow.
        QVERIFY(QMetaObject::invokeMethod(components,"undock")); QTest::qWait(60);
        const auto beforeSerial=rect(serial),beforeReceive=rect(receive);
        const auto global=QPointF(contentDockRect(window).center().x(),contentDockRect(window).top()+8); tracker->updateDrag(global);
        QCOMPARE(tracker->dockTarget(),area); QCOMPARE(tracker->dockEdge(),QString("top"));
        const auto whole=tracker->previewRect(); QCOMPARE(whole.height(),(area->height()-12)/2);
        QCOMPARE(rect(serial),beforeSerial); QCOMPARE(rect(receive),beforeReceive);
        tracker->endDrag(global); QTest::qWait(60); QVERIFY(matches(components,whole));
        QVERIFY(QMetaObject::invokeMethod(serial,"undock")); QVERIFY(QMetaObject::invokeMethod(receive,"undock")); QTest::qWait(60);
        QCOMPARE(components->size(),area->size());
        window->close();
    }
    void dockLayoutRoundTrip() {
        const auto load=[](QQmlApplicationEngine &engine,ApplicationServices &services) {
            engine.rootContext()->setContextProperty("appSettings",&services.settings());
            engine.rootContext()->setContextProperty("serialConnection",&services.serialConnection());
            engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
            engine.rootContext()->setContextProperty("protocolSession",&services.protocolSession());
            engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml")));
        };
        {
            ApplicationServices services;
            QQmlApplicationEngine engine;
            load(engine,services);
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
            auto *split=window->findChild<QQuickItem *>("dockSplit"); QVERIFY(split);
            auto *workspace=window->findChild<QQuickItem *>("componentWorkspace"); QVERIFY(workspace);
            auto *component=window->findChild<QQuickItem *>("componentsDock"); QVERIFY(component);
            QVERIFY(QTest::qWaitForWindowExposed(window));
            const QVariantMap floatingGeometry{{"x",72},{"y",115},{"width",430},{"height",330}};
            const QVariantMap state{{"version",1},{"sidebarEdge","right"},{"sidebarCollapsed",true},
                {"vertical",true},{"receiveFirst",true},{"ratio",0.42},
                {"floating",QVariantMap{{"components",floatingGeometry}}}};
            QVERIFY(QMetaObject::invokeMethod(split->parentItem(),"restoreLayout",Q_ARG(QVariant,QVariant(state))));
            QTRY_VERIFY(component->property("floating").toBool());
            QVERIFY(window->setProperty("leftPage",1));
            QVERIFY(QMetaObject::invokeMethod(workspace,"addWidget",
                Q_ARG(QVariant,QVariant("time-plot")),Q_ARG(QVariant,QPointF(40,80))));
            QVERIFY(window->close());
            const auto saved=services.settings().loadDockLayout();
            QCOMPARE(saved.value("ratio").toDouble(),0.42);
            QCOMPARE(saved.value("leftPage").toInt(),1);
            QVERIFY(saved.value("floating").toMap().contains("components"));
        }
        {
            ApplicationServices services;
            QQmlApplicationEngine engine;
            load(engine,services);
            QVERIFY(!engine.rootObjects().isEmpty());
            auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
            auto *serial=window->findChild<QQuickItem *>("connectionDock"); QVERIFY(serial);
            auto *component=window->findChild<QQuickItem *>("componentsDock"); QVERIFY(component);
            auto *receive=window->findChild<QQuickItem *>("receiveDock"); QVERIFY(receive);
            auto *floating=window->findChild<QQuickWindow *>("componentsFloatingWindow"); QVERIFY(floating);
            auto *workspace=window->findChild<QQuickItem *>("componentWorkspace"); QVERIFY(workspace);
            auto *split=window->findChild<QQuickItem *>("dockSplit"); QVERIFY(split);
            QVERIFY(QTest::qWaitForWindowExposed(window));
            QCOMPARE(window->property("leftPage").toInt(),1);
            QCOMPARE(serial->property("sidebarEdge").toString(),QString("right"));
            QVERIFY(serial->property("sidebarCollapsed").toBool());
            QVERIFY(component->property("floating").toBool());
            QCOMPARE(floating->size(),QSize(430,330));
            QCOMPARE(workspace->property("widgetCount").toInt(),1);

            QVERIFY(QMetaObject::invokeMethod(component,"dock",Q_ARG(QVariant,QVariant()),
                Q_ARG(QVariant,QVariant()),Q_ARG(QVariant,QVariant())));
            QTRY_VERIFY(!component->property("floating").toBool());
            QTRY_VERIFY(receive->mapToScene(QPointF()).y()<component->mapToScene(QPointF()).y());
            QCOMPARE(split->parentItem()->property("groupRatio").toDouble(),0.42);

            auto *settings=window->findChild<QQuickItem *>("settingsButton"); QVERIFY(settings);
            auto *reset=window->findChild<QQuickItem *>("resetDockLayoutButton"); QVERIFY(reset);
            QVERIFY(QMetaObject::invokeMethod(settings,"clicked"));
            QTRY_VERIFY(reset->isVisible() && reset->width()>=120 && reset->height()>=30);
            window->requestActivate();
            QTRY_VERIFY(window->isActive());
            QTest::qWait(60);
            QSignalSpy resetClicks(reset,SIGNAL(clicked()));
            const auto buttonCenter=reset->mapToScene(QPointF(reset->width()/2,reset->height()/2)).toPoint();
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,buttonCenter);
            QCOMPARE(resetClicks.count(),1);
            QVERIFY(QMetaObject::invokeMethod(settings,"clicked"));
            QCOMPARE(window->property("leftPage").toInt(),0);
            QCOMPARE(serial->property("sidebarEdge").toString(),QString("left"));
            QVERIFY(!serial->property("sidebarCollapsed").toBool());
            QVERIFY(!component->property("floating").toBool());
            QTRY_VERIFY(serial->mapToScene(QPointF()).x()<component->mapToScene(QPointF()).x());
            QTRY_VERIFY(component->mapToScene(QPointF()).x()<receive->mapToScene(QPointF()).x());
            QCOMPARE(split->parentItem()->property("groupRatio").toDouble(),0.56);
            QCOMPARE(workspace->property("widgetCount").toInt(),1);
            QVERIFY(window->close());
            QCOMPARE(services.settings().loadDockLayout().value("sidebarEdge").toString(),QString("left"));
        }
    }
    void fixedSidebarUi() {
        ApplicationServices services; QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("numericStream",&services.numericStream());
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&services.serialConnection());
        engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
        engine.rootContext()->setContextProperty("protocolSession",&services.protocolSession());
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml"))); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); QTRY_VERIFY(window->isExposed()); QTest::qWait(60);
        auto *sidebar=window->findChild<QQuickItem *>("connectionDock"); QVERIFY(sidebar); QCOMPARE(sidebar->width(),260.);
        auto *area=window->findChild<QQuickItem *>("dockWorkspace"); QVERIFY(area);
        auto *tracker=sidebar->findChild<DockWindowController *>("connectionDockController"); QVERIFY(tracker);
        auto *floating=window->findChild<QQuickWindow *>("connectionFloatingWindow"); QVERIFY(floating);
        for(const QString &edge:QStringList{"left","right"}) {
            QVERIFY(QMetaObject::invokeMethod(sidebar,"undock")); QTest::qWait(40);
            QCOMPARE(floating->minimumWidth(),floating->maximumWidth()); QCOMPARE(floating->width(),260);
            QCOMPARE(floating->minimumHeight(),floating->maximumHeight());
            auto *button=window->findChild<QQuickItem *>("connectionMaximizeButton"); QVERIFY(button); QVERIFY(!button->isVisible());
            if(auto *grip=visualItem(floating->contentItem(),"connectionWindowResize_rb")) QVERIFY(!grip->isVisible());
            const auto before=floating->geometry(); QVERIFY(QMetaObject::invokeMethod(sidebar,"toggleMaximized")); QCOMPARE(floating->geometry(),before);
            tracker->updateDrag(area->mapToGlobal(QPointF(area->width()/2,8))); QVERIFY(!tracker->dockReady()); QVERIFY(!tracker->guidesVisible());
            tracker->updateDrag(area->mapToGlobal(QPointF(area->width()/2,area->height()-8))); QVERIFY(!tracker->dockReady());
            const auto target=area->mapToGlobal(QPointF(edge=="left"?8:area->width()-8,area->height()/2));
            tracker->updateDrag(target); QCOMPARE(tracker->dockTarget(),area); QCOMPARE(tracker->dockEdge(),edge); QCOMPARE(tracker->previewRect().width(),260.);
            tracker->endDrag(target); QTest::qWait(60); QVERIFY(!sidebar->property("floating").toBool());
            QCOMPARE(sidebar->width(),260.); QCOMPARE(sidebar->height(),area->height());
            QCOMPARE(sidebar->property("sidebarEdge").toString(),edge);
            const auto x=sidebar->mapToGlobal(QPointF()).x();
            QCOMPARE(x,area->mapToGlobal(QPointF(edge=="left"?0:area->width()-260,0)).x());
            auto *fold=window->findChild<QQuickItem *>("sidebarFoldHandle"); QVERIFY(fold); QTRY_VERIFY(fold->isVisible());
            const int outward=edge=="left"?-1:1;
            auto point=fold->mapToScene(QPointF(fold->width()/2,fold->height()/2)).toPoint();
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,point);
            QTest::mouseMove(window,point+QPoint(outward*12,0),1);
            QVERIFY(!sidebar->property("sidebarCollapsed").toBool()); QCOMPARE(sidebar->width(),260.);
            QTest::mouseMove(window,point+QPoint(outward*32,0),1);
            QTRY_VERIFY(sidebar->property("sidebarCollapsed").toBool()); QTRY_COMPARE(sidebar->width(),0.);
            QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,point+QPoint(outward*32,0)); QTest::qWait(40);
            auto *components=window->findChild<QQuickItem *>("componentsDock"); auto *receive=window->findChild<QQuickItem *>("receiveDock"); QVERIFY(components && receive);
            QCOMPARE(components->width()+receive->width()+12+12,area->width());
            auto *content=window->findChild<QQuickItem *>("connectionDockContent"); QVERIFY(content); QVERIFY(!content->isVisible());
            const auto snapshot=qEnvironmentVariable("UART_FOLDED_SIDEBAR_SNAPSHOT");
            if(!snapshot.isEmpty() && edge=="left") QVERIFY(window->grabWindow().save(snapshot));
            point=fold->mapToScene(QPointF(fold->width()/2,fold->height()/2)).toPoint();
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,point);
            QTest::mouseMove(window,point-QPoint(outward*32,0),1);
            QTRY_VERIFY(!sidebar->property("sidebarCollapsed").toBool()); QTRY_COMPARE(sidebar->width(),260.);
            QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,point-QPoint(outward*32,0)); QTest::qWait(40);
            QVERIFY(content->isVisible()); QCOMPARE(components->width()+receive->width()+12+260+12,area->width());

        }
        // Closing has no placement request: the existing dock position is enough.
        QSignalSpy placements(sidebar,SIGNAL(dockPlacementRequested(QString,QVariant,QRectF)));
        QVERIFY(placements.isValid());
        QVERIFY(QMetaObject::invokeMethod(sidebar,"undock")); QTest::qWait(40);
        auto *close=window->findChild<QQuickItem *>("connectionCloseButton"); QVERIFY(close);
        QVERIFY(QMetaObject::invokeMethod(close,"clicked")); QTest::qWait(40);
        QVERIFY(!sidebar->property("floating").toBool()); QCOMPARE(placements.count(),0);
        QCOMPARE(sidebar->width(),260.); QCOMPARE(sidebar->property("sidebarEdge").toString(),QString("right"));
        QVERIFY(QMetaObject::invokeMethod(sidebar,"undock")); QTest::qWait(40);
        floating->close(); QTest::qWait(40);
        QVERIFY(!sidebar->property("floating").toBool()); QCOMPARE(placements.count(),0);
        window->close();
    }
    void receiveMinimumWidthUi() {
        ApplicationServices services; QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("numericStream",&services.numericStream());
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&services.serialConnection());
        engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
        engine.rootContext()->setContextProperty("protocolSession",&services.protocolSession());
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml"))); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); QTRY_VERIFY(window->isExposed());
        auto *panel=window->findChild<QQuickItem *>("receiveDock"); QVERIFY(panel); QVERIFY(QMetaObject::invokeMethod(panel,"undock"));
        auto *floating=window->findChild<QQuickWindow *>("receiveFloatingWindow"); QVERIFY(floating); QTRY_VERIFY(floating->isExposed());
        QCOMPARE(floating->minimumWidth(),380); floating->resize(380,600);
        auto *toolbar=window->findChild<QQuickItem *>("receiveToolbar"); auto *actions=window->findChild<QQuickItem *>("receiveActions"); QVERIFY(toolbar && actions);
        QTRY_VERIFY(toolbar->isVisible() && actions->isVisible());
        QVERIFY(!toolbar->property("horizontalOverflow").toBool());
        QVERIFY(!actions->property("horizontalOverflow").toBool());
        auto *tab=window->findChild<QQuickItem *>("protocolTab"); QVERIFY(tab);
        QVERIFY(QMetaObject::invokeMethod(tab,"clicked"));
        auto *settings=window->findChild<QQuickItem *>("protocolToolbar"); QVERIFY(settings);
        QTRY_VERIFY(settings->isVisible());
        auto *results=window->findChild<QQuickItem *>("protocolResults"); QVERIFY(results);
        for(const QString &id:QStringList{"none","csv","keyvalue","binary"}) {
            QVERIFY(services.protocolSession().selectProtocol(id)); QTest::qWait(50);
            QVERIFY2(!toolbar->property("horizontalOverflow").toBool(),qPrintable(id+" header overflow"));
            QVERIFY2(!actions->property("horizontalOverflow").toBool(),qPrintable(id+" actions overflow"));
            QVERIFY2(settings->childrenRect().right()<=settings->width()+1,qPrintable(id+" protocol settings overflow"));
            QVERIFY2(settings->height()<=40,qPrintable(id+" protocol toolbar stretched vertically"));
            QVERIFY2(results->height()>200,qPrintable(id+" protocol results collapsed"));
        }
        auto &protocol=services.protocolSession();
        auto *selector=window->findChild<QQuickItem *>("protocolSelector"); QVERIFY(selector);
        auto *command=window->findChild<QQuickItem *>("receivedCommandSelector"); QVERIFY(command);
        auto *edit=window->findChild<QQuickItem *>("editBinaryProtocol"); QVERIFY(edit);
        const auto center = [](QQuickItem *item) {
            return item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint();
        };
        const auto chooseTemplate = [&](const QString &category,const QString &id) {
            QTest::mouseClick(floating,Qt::LeftButton,Qt::NoModifier,center(selector));
            QQuickItem *binaryItem=nullptr;
            QTRY_VERIFY_WITH_TIMEOUT((binaryItem=visualItem(floating->contentItem(),"protocolCategory_binary"))
                                     && binaryItem->isVisible(),2000);
            QTest::mouseMove(floating,center(binaryItem));
            QQuickItem *categoryItem=nullptr;
            QTRY_VERIFY((categoryItem=visualItem(floating->contentItem(),"binaryTemplateCategory_"+category))
                        && categoryItem->isVisible());
            QTest::mouseMove(floating,center(categoryItem));
            QTRY_VERIFY_WITH_TIMEOUT(categoryItem->property("hovered").toBool(),2000);
            auto *submenu=categoryItem->property("subMenu").value<QObject *>(); QVERIFY(submenu);
            QTRY_VERIFY_WITH_TIMEOUT(submenu->property("visible").toBool(),2000);
            QQuickItem *option=nullptr;
            QTRY_VERIFY_WITH_TIMEOUT((option=visualItem(floating->contentItem(),"binaryTemplateOption_"+id)) && option->isVisible(),2000);
            const auto categoryLeft=categoryItem->mapToScene(QPointF()).x();
            const auto optionLeft=option->mapToScene(QPointF()).x();
            QVERIFY(optionLeft+option->width()<=categoryLeft+2
                    || categoryLeft+categoryItem->width()<=optionLeft+2);
            const auto preview=qEnvironmentVariable("UART_TEMPLATE_MENU_SNAPSHOT");
            if (!preview.isEmpty() && category=="builtIn") QVERIFY(floating->grabWindow().save(preview));
            QTest::mouseClick(floating,Qt::LeftButton,Qt::NoModifier,center(option));
            QTRY_COMPARE(protocol.binaryConfiguration().value("templateId").toString(),id);
        };

        QVERIFY(protocol.selectProtocol("csv"));
        chooseTemplate("builtIn","builtin:simple_variable");
        QCOMPARE(protocol.selectedProtocolId(),QString("binary"));
        QTRY_VERIFY(selector->isVisible());
        QCOMPARE(selector->property("text").toString(),QStringLiteral("简单变长帧"));
        QVERIFY(!command->isVisible());
        QVERIFY(edit->mapToScene(QPointF()).x()>=selector->mapToScene(QPointF(selector->width(),0)).x());

        const auto applied=protocol.binaryConfiguration();
        QVERIFY(protocol.selectProtocol("csv"));
        QTest::mouseClick(floating,Qt::LeftButton,Qt::NoModifier,center(selector));
        auto *csvOption=visualItem(floating->contentItem(),"protocolOption_csv"); QVERIFY(csvOption);
        auto *dictionaryOption=visualItem(floating->contentItem(),"protocolOption_keyvalue"); QVERIFY(dictionaryOption);
        auto *binaryOption=visualItem(floating->contentItem(),"protocolCategory_binary"); QVERIFY(binaryOption);
        auto textX=[](QQuickItem *item) {
            auto *label=item->property("contentItem").value<QQuickItem *>();
            return label->mapToScene(QPointF(label->property("leftPadding").toReal(),0)).x();
        };
        QCOMPARE(textX(csvOption),textX(dictionaryOption));
        QCOMPARE(textX(csvOption),textX(binaryOption));
        QTest::mouseClick(floating,Qt::LeftButton,Qt::NoModifier,center(binaryOption));
        QTRY_COMPARE(protocol.selectedProtocolId(),QString("binary"));
        QTRY_VERIFY(!binaryOption->isVisible());
        QCOMPARE(protocol.binaryConfiguration(),applied);

        const auto personalId=protocol.templateStore()->saveTemplate("测试模板",BinaryConfig::preset("robot_tennis"));
        QVERIFY(!personalId.isEmpty());
        chooseTemplate("personal",personalId);
        QTRY_VERIFY(command->isVisible());
        QVERIFY(qAbs(command->y()-selector->y())<2);
        QCOMPARE(selector->property("text").toString(),QStringLiteral("测试模板"));
        QVERIFY(command->width()<=selector->width());
        QVERIFY(command->x()>=selector->x()+selector->width());
        QVERIFY(edit->x()>=command->x()+command->width());
        QCOMPARE(command->property("text").toString(),QStringLiteral("四轮轮速"));
        QTest::mouseClick(floating,Qt::LeftButton,Qt::NoModifier,center(command));
        QQuickItem *imu=nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((imu=visualItem(floating->contentItem(),"receivedChannelOption_0x102"))
                                 && imu->isVisible(),2000);
        QTest::mouseClick(floating,Qt::LeftButton,Qt::NoModifier,center(imu));
        QTRY_COMPARE(protocol.selectedBinaryChannelId(),QString("0x102"));
        QCOMPARE(command->property("text").toString(),QStringLiteral("IMU 原始数据"));

        chooseTemplate("builtIn","builtin:multi_segment");
        QCOMPARE(selector->property("text").toString(),QStringLiteral("多段命令帧"));
        QTRY_VERIFY(command->isVisible());
        QCOMPARE(command->property("text").toString(),QStringLiteral("接收命令"));

        const auto binary=protocol.binaryConfiguration();
        auto blocks=binary.value("structure").toList();
        auto commandBlock=blocks.at(3).toMap();
        auto choices=commandBlock.value("messages").toList();
        choices.append(QVariantMap{{"command","0x3"},{"name","备用命令"}});
        commandBlock["messages"]=choices;
        blocks[3]=commandBlock;
        auto payloadBlock=blocks.at(4).toMap();
        auto variants=payloadBlock.value("variants").toMap();
        variants["0x3"]=payloadBlock.value("fields");
        payloadBlock["variants"]=variants;
        blocks[4]=payloadBlock;
        QString error;
        auto config=FrameStructure::compile(blocks,binary,error);
        QVERIFY2(!config.isEmpty(),qPrintable(error));
        config["templateId"]=binary.value("templateId");
        QVERIFY(protocol.applyBinaryConfiguration(config));

        QTest::mouseClick(floating,Qt::LeftButton,Qt::NoModifier,center(command));
        QQuickItem *group=nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((group=visualItem(floating->contentItem(),"receivedCommandCategory_3"))
                                 && group->isVisible(),2000);
        QVERIFY(visualItem(floating->contentItem(),"receivedCommandCategory_1"));
        QTest::mouseMove(floating,center(group));
        QQuickItem *alternate=nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((alternate=visualItem(floating->contentItem(),"receivedCommandOption_3_0x3"))
                                 && alternate->isVisible(),2000);
        const auto preview=qEnvironmentVariable("UART_COMMAND_MENU_SNAPSHOT");
        if (!preview.isEmpty()) QVERIFY(floating->grabWindow().save(preview));
        QTest::mouseClick(floating,Qt::LeftButton,Qt::NoModifier,center(alternate));
        QTRY_COMPARE(protocol.binaryCommandGroups().at(1).toMap().value("value").toString(),QString("0x3"));
        QCOMPARE(protocol.binaryCommandGroups().at(0).toMap().value("value").toString(),QString("0x1"));
        QCOMPARE(selector->property("text").toString(),QStringLiteral("多段命令帧"));
        floating->resize(700,600);
        QTRY_COMPARE(selector->width(),200.0);
        QTRY_COMPARE(command->width(),selector->width());
        QCOMPARE(command->x()-selector->x()-selector->width(),8.0);
        QVERIFY(edit->x()>command->x()+command->width()+8);
        QVERIFY(settings->height()<=40);
        QVERIFY(results->height()>200);
        window->close();
    }
    void floatingMaximizeUi() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Native maximize geometry needs a desktop window manager");
        ApplicationServices services; QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("numericStream",&services.numericStream());
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&services.serialConnection());
        engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
        engine.rootContext()->setContextProperty("protocolSession",&services.protocolSession());
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml"))); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window); QTRY_VERIFY(window->isExposed());
        for(const QString &key:QStringList{"receive","components"}) {
            auto *panel=window->findChild<QQuickItem *>(key+"Dock"); QVERIFY(panel); QVERIFY(QMetaObject::invokeMethod(panel,"undock"));
            auto *floating=window->findChild<QQuickWindow *>(key+"FloatingWindow"); QVERIFY(floating); QTRY_VERIFY(floating->isExposed()); QTest::qWait(80);
            auto *button=window->findChild<QQuickItem *>(key+"MaximizeButton"); QVERIFY(button); QCOMPARE(button->window(),floating);
            const auto before=floating->geometry(); QSignalSpy clicks(button,SIGNAL(clicked()));
            QTest::mouseClick(floating,Qt::LeftButton,Qt::NoModifier,button->mapToScene(QPointF(17,16)).toPoint()); QTest::qWait(200);
            qInfo()<<key<<"clicks"<<clicks.count()<<"visibility"<<floating->visibility()<<"before"<<before<<"after"<<floating->geometry()<<"available"<<floating->screen()->availableGeometry();
            QCOMPARE(clicks.count(),1); QTRY_COMPARE(floating->visibility(),QWindow::Maximized);
            const auto work=floating->screen()->availableGeometry();
            QTRY_VERIFY(floating->width()>=work.width()-4 && floating->height()>=work.height()-4);
            QTest::mouseClick(floating,Qt::LeftButton,Qt::NoModifier,button->mapToScene(QPointF(17,16)).toPoint());
            QTRY_COMPARE(floating->visibility(),QWindow::Windowed); QTRY_COMPARE(floating->size(),before.size());
            floating->close(); QTRY_VERIFY(!panel->property("floating").toBool());
        }
        window->close();
    }
    void floatingDockPanels() {
#ifndef Q_OS_UNIX
        QSKIP("Linux pseudo-terminal dock/receive check");
#else
        Pty p; QVERIFY(!p.path.isEmpty()); ApplicationServices services;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&services.serialConnection());
        engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
        engine.rootContext()->setContextProperty("protocolSession",&services.protocolSession());
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml"))); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *receiveDock=window->findChild<QQuickItem *>("receiveDock"); auto *connectionDock=window->findChild<QQuickItem *>("connectionDock"); QVERIFY(receiveDock && connectionDock);
        auto *area=window->findChild<QQuickItem *>("dockWorkspace"); auto *receiveView=window->findChild<QQuickItem *>("receiveWorkspace"); QVERIFY(area && receiveView);
        auto *baud=window->findChild<QQuickItem *>("baudCombo"); QVERIFY(baud);
        QVERIFY(services.serialConnection().openPort(p.path)); QVERIFY(services.protocolSession().selectProtocol("keyvalue"));
        QVERIFY(p.send("awa:2.0, aaa:3\n")); QTRY_COMPARE(services.protocolSession().frameCount(),1ULL);
        QVERIFY(!window->findChild<QQuickItem *>("receiveFloatButton"));
        auto *handle=window->findChild<QQuickItem *>("receiveDragHandle"); QVERIFY(handle);
        QTRY_VERIFY(window->isExposed()); QTest::qWait(100);
        auto *initialFloat=window->findChild<QQuickWindow *>("receiveFloatingWindow"); QVERIFY(initialFloat);
        QSignalSpy visibilityChanges(initialFloat,&QWindow::visibleChanged);
        const auto press=handle->mapToScene(QPointF(80,14)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,press);
        QTest::mouseMove(window,press+QPoint(35,10),1);
        QVERIFY(receiveDock->property("floating").toBool());
        auto *releaseWindow=window->findChild<QQuickWindow *>("receiveFloatingWindow"); QVERIFY(releaseWindow);
        auto *dragTracker=receiveDock->findChild<DockWindowController *>("receiveDockController"); QVERIFY(dragTracker);
        const auto dragEdge=area->mapToGlobal(QPointF(area->width()-15,area->height()/2));
        const auto beforeMove=releaseWindow->position();
        QTest::mouseMove(window,window->mapFromGlobal(dragEdge.toPoint()),1);
        QTRY_COMPARE(dragTracker->dockEdge(),QString("right")); QVERIFY(dragTracker->guidesVisible()); QVERIFY(dragTracker->dragging());
        QTRY_VERIFY(releaseWindow->position()!=beforeMove);
        auto *liveGuide=window->findChild<QQuickItem *>("dockOverlay"); QVERIFY(liveGuide); QTRY_VERIFY(liveGuide->isVisible());
        QQuickItem *rightIcon=nullptr; QTRY_VERIFY((rightIcon=visualItem(liveGuide,"dockGuide_right"))); QTRY_VERIFY(rightIcon->property("selected").toBool());

        const QPointF dragCenter=window->mapToGlobal(QPoint(window->width()/2,10));
        QTest::mouseMove(window,window->mapFromGlobal(dragCenter.toPoint()),1);
        QVERIFY(dragTracker->dockEdge().isEmpty()); QVERIFY(dragTracker->dragging());
        QTRY_VERIFY(!rightIcon->property("selected").toBool());

        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,window->mapFromGlobal(dragCenter.toPoint()));
        auto *floating=window->findChild<QQuickWindow *>("receiveFloatingWindow"); QVERIFY(floating); QTRY_VERIFY(floating->isVisible());
        if(QGuiApplication::platformName()=="xcb") QTRY_VERIFY(floating->isActive());
        QTRY_COMPARE(receiveView->window(),floating); QCOMPARE(window->findChild<QQuickItem *>("receiveWorkspace"),receiveView);
        QCOMPARE(visibilityChanges.count(),1); // No remapping or transient hide/show while detaching.
        QVERIFY(floating->flags().testFlag(Qt::FramelessWindowHint)); QCOMPARE(floating->type(),Qt::Window);
        // The only title strip is now at the very top; drag it back like the docked strip.
        auto *floatingTitle=window->findChild<QQuickItem *>("receiveFloatingDragHandle"); QVERIFY(floatingTitle);
        QTRY_VERIFY(floatingTitle->isVisible() && floatingTitle->width()>100); QTest::qWait(50);
        const auto titlePress=floatingTitle->mapToScene(QPointF(80,16)).toPoint();
        QTest::mousePress(floating,Qt::LeftButton,Qt::NoModifier,titlePress);
        QTest::mouseMove(floating,titlePress+QPoint(35,10),1);
        QVERIFY(dragTracker->dragging());
        const auto titleTarget=QPointF(contentDockRect(window).center().x(),contentDockRect(window).bottom()-10).toPoint();
        QTest::mouseMove(floating,floating->mapFromGlobal(titleTarget),1);
        QCOMPARE(dragTracker->dockEdge(),QString("bottom")); QVERIFY(dragTracker->guidesVisible());
        QTest::mouseRelease(floating,Qt::LeftButton,Qt::NoModifier,floating->mapFromGlobal(titleTarget));
        QTRY_VERIFY(!receiveDock->property("floating").toBool()); QTRY_COMPARE(receiveView->window(),window);
        QVERIFY(QMetaObject::invokeMethod(receiveDock,"undock")); QTRY_COMPARE(receiveView->window(),floating);
        QVERIFY(p.send("awa:4.5, aaa:6\n")); QTRY_COMPARE(services.protocolSession().frameCount(),2ULL);
        QTRY_COMPARE(services.protocolSession().latestFields()[0].toMap()["value"].toDouble(),4.5);
        services.settings().setDarkMode(true); QTRY_VERIFY(floating->color().lightnessF()<0.2);
        const auto directory=qEnvironmentVariable("UART_DOCK_SNAPSHOT_DIR");
        if(!directory.isEmpty()) { QTest::qWait(100); QVERIFY(floating->grabWindow().save(directory+"/uart-floating-receiver.png")); }
        auto *tracker=receiveDock->findChild<DockWindowController *>("receiveDockController"); QVERIFY(tracker);
        const auto center=window->mapToGlobal(QPoint(window->width()/2,10)); tracker->updateDrag(center); QVERIFY(!tracker->dockReady());
        const QStringList edges{"left","top","right","bottom"};
        auto *split=window->findChild<QQuickItem *>("dockSplit"); QVERIFY(split);
        for(const auto &edge:edges) {
            if(!receiveDock->property("floating").toBool()) QVERIFY(QMetaObject::invokeMethod(receiveDock,"undock"));
            const auto content=contentDockRect(window);
            QPointF target;
            if(edge=="left") target={content.left()+10,content.center().y()};
            if(edge=="right") target={content.right()-10,content.center().y()};
            if(edge=="top") target={content.center().x(),content.top()+10};
            if(edge=="bottom") target={content.center().x(),content.bottom()-10};
            tracker->updateDrag(target); QVERIFY(tracker->dockReady()); QCOMPARE(tracker->dockEdge(),edge);
            const auto planned=tracker->previewRect();
            auto *overlay=window->findChild<QQuickItem *>("dockOverlay"); QVERIFY(overlay); QTRY_VERIFY(overlay->isVisible());
            if(!directory.isEmpty() && edge=="bottom") { QTest::qWait(60); QVERIFY(grabItem(overlay).save(directory+"/uart-dock-guides.png")); }
            tracker->endDrag(target); QTRY_VERIFY(!receiveDock->property("floating").toBool()); QTRY_COMPARE(receiveView->window(),window); QTest::qWait(40);
            QCOMPARE(split->property("orientation").toInt(),int(Qt::Horizontal));
            QCOMPARE(connectionDock->width(),260.); QCOMPARE(connectionDock->height(),area->height());
            const QRectF actual(receiveDock->mapToGlobal(QPointF()),receiveDock->size());
            QVERIFY(std::abs(actual.x()-planned.x())<=1 && std::abs(actual.y()-planned.y())<=1);
            QVERIFY(std::abs(actual.width()-planned.width())<=1 && std::abs(actual.height()-planned.height())<=1);
            if(!directory.isEmpty() && edge=="top") { QTest::qWait(60); QVERIFY(window->grabWindow().save(directory+"/uart-top-docked.png")); }
        }
        QCOMPARE(services.protocolSession().frameCount(),2ULL); QVERIFY(services.serialConnection().connected());
        QVERIFY(QMetaObject::invokeMethod(connectionDock,"undock"));
        auto *serialWindow=window->findChild<QQuickWindow *>("connectionFloatingWindow"); QVERIFY(serialWindow); QTRY_VERIFY(serialWindow->isVisible());
        QTRY_COMPARE(baud->window(),serialWindow); QCOMPARE(window->findChild<QQuickItem *>("baudCombo"),baud);
        serialWindow->close(); QTRY_VERIFY(!connectionDock->property("floating").toBool()); QTRY_COMPARE(baud->window(),window); QVERIFY(services.serialConnection().connected());
        // Protocol editing uses the overlay of the floating receive window.
        QVERIFY(QMetaObject::invokeMethod(receiveDock,"undock")); QTRY_VERIFY(floating->isVisible());
        QVERIFY(services.protocolSession().selectProtocol("binary"));
        auto *open=window->findChild<QQuickItem *>("editBinaryProtocol"); QVERIFY(open); QVERIFY(QMetaObject::invokeMethod(open,"clicked"));
        auto *editor=window->findChild<QObject *>("binaryProtocolEditor"); QVERIFY(editor); QTRY_VERIFY(editor->property("visible").toBool());
        auto *content=editor->property("contentItem").value<QQuickItem *>(); QVERIFY(content); QTRY_COMPARE(content->window(),floating);
        auto *exportDialog=editor->findChild<QObject *>("exportBinaryTemplateDialog"); QVERIFY(exportDialog); QCOMPARE(exportDialog->property("parentWindow").value<QWindow *>(),floating);
        QVERIFY(QMetaObject::invokeMethod(editor,"close"));
        QVERIFY(QMetaObject::invokeMethod(connectionDock,"undock")); QTRY_VERIFY(serialWindow->isVisible());
        if(!directory.isEmpty()) { QTest::qWait(80); QVERIFY(window->grabWindow().save(directory+"/uart-both-floating.png")); }
        // Closing the main window hides both detached windows as well.
        window->close(); QTRY_VERIFY(!floating->isVisible()); QTRY_VERIFY(!serialWindow->isVisible());
#endif
    }
    void keyValueReceiverUi() {
#ifndef Q_OS_UNIX
        QSKIP("Linux pseudo-terminal text receiver check");
#else
        Pty p; QVERIFY(!p.path.isEmpty()); ApplicationServices services;
        auto &protocol=services.protocolSession(); QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&services.serialConnection());
        engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
        engine.rootContext()->setContextProperty("protocolSession",&protocol);
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml"))); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *selector=window->findChild<QQuickItem *>("protocolSelector"); QVERIFY(selector);
        auto *tab=window->findChild<QQuickItem *>("protocolTab"); QVERIFY(tab);
        QVERIFY(QMetaObject::invokeMethod(tab,"clicked"));
        QTRY_VERIFY(window->isExposed());
        QVERIFY(QMetaObject::invokeMethod(selector,"clicked"));
        QQuickItem *option=nullptr;
        QTRY_VERIFY((option=visualItem(window->contentItem(),"protocolOption_keyvalue")) && option->isVisible());
        QVERIFY(QMetaObject::invokeMethod(option,"clicked"));
        QCOMPARE(protocol.selectedProtocolId(),QString("keyvalue")); QVERIFY(services.serialConnection().openPort(p.path));
        QVERIFY(p.send("awa:2.0,    aaa:3\n")); QTRY_COMPARE(protocol.latestFields().size(),2);
        QCOMPARE(protocol.latestFields()[0].toMap()["name"].toString(),QString("awa"));
        QCOMPARE(protocol.latestFields()[1].toMap()["value"].toDouble(),3.0);
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        QQuickItem *value=nullptr;
        QTRY_VERIFY((value=visualItem(window->contentItem(),"decodedValue")));
        QCOMPARE(value->property("text").toString(),QString("2"));
        const auto from=value->mapToScene(QPointF(0,value->height()/2)).toPoint();
        const auto to=value->mapToScene(QPointF(20,value->height()/2)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,from);
        QTest::mouseMove(window,to,1);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,to);
        QCOMPARE(value->property("selectedText").toString(),QString("2"));
        QVERIFY(p.send("awa:2.5, aaa:3\n"));
        QTRY_COMPARE(protocol.latestFields()[0].toMap()["value"].toDouble(),2.5);
        QCOMPARE(value->property("selectedText").toString(),QString("2"));
        QGuiApplication::clipboard()->clear();
        QContextMenuEvent contextMenu(QContextMenuEvent::Mouse,to,window->mapToGlobal(to));
        QCoreApplication::sendEvent(window,&contextMenu);
        QQuickItem *copy=nullptr;
        QTRY_VERIFY((copy=visualItem(window->contentItem(),"copyReceiveText")) && copy->isVisible());
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,
                          copy->mapToScene(QPointF(copy->width()/2,copy->height()/2)).toPoint());
        QTRY_COMPARE(QGuiApplication::clipboard()->text(),QString("2"));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,to);
        QTRY_COMPARE(value->property("text").toString(),QString("2.5"));
        const auto snapshot=qEnvironmentVariable("UART_KEYVALUE_SNAPSHOT");
        if(!snapshot.isEmpty()) { QTest::qWait(80); QVERIFY(window->grabWindow().save(snapshot)); }
        protocol.setDisplayPaused(true); QVERIFY(p.send("awa:4.5, aaa:6\n")); QTRY_COMPARE(protocol.frameCount(),3ULL);
        QCOMPARE(protocol.latestFields()[0].toMap()["value"].toDouble(),2.5);
        protocol.setDisplayPaused(false); QTRY_COMPARE(protocol.latestFields()[0].toMap()["value"].toDouble(),4.5);
        services.receiveSession().clear(); QTRY_VERIFY(protocol.latestFields().isEmpty()); QCOMPARE(protocol.frameCount(),0ULL);
#endif
    }
    void multiRegionProtocol() {
        ApplicationServices services; auto &protocol=services.protocolSession();
        auto config=protocol.templateStore()->loadTemplate("builtin:multi_segment")["configuration"].toMap(); QVERIFY(!config.isEmpty());
        auto blocks=config["structure"].toList(); auto command=blocks[3].toMap();
        auto options=command["messages"].toList(); options.append(QVariantMap{{"command","0x3"},{"name","第二组备用"}}); command["messages"]=options; blocks[3]=command;
        auto data=blocks[4].toMap(); auto variants=data["variants"].toMap(); auto alternate=data["fields"].toList(); auto field=alternate[0].toMap(); field["name"]="temperature"; alternate[0]=field;
        variants["0x3"]=alternate; data["variants"]=variants; blocks[4]=data;
        QString error; config=FrameStructure::compile(blocks,config,error); QVERIFY2(!config.isEmpty(),qPrintable(error));
        const auto startingId=protocol.templateStore()->saveTemplate("多模块测试",config); QVERIFY(!startingId.isEmpty());
        config=protocol.templateStore()->loadTemplate(startingId)["configuration"].toMap();
        QVERIFY(protocol.applyBinaryConfiguration(config)); QVERIFY(protocol.selectProtocol("binary"));
        QCOMPARE(protocol.binaryCommandGroups().size(),2);
        protocol.feedBytes(QByteArray::fromHex("aa55010000c03f0200002040"));
        QTRY_COMPARE(protocol.latestFields().size(),4); QCOMPARE(protocol.latestFields()[1].toMap()["value"].toDouble(),1.5);
        QCOMPARE(protocol.latestFields()[3].toMap()["value"].toDouble(),2.5);
        protocol.feedBytes(QByteArray::fromHex("aa55010000c03f0300002040")); QTest::qWait(60); QCOMPARE(protocol.frameCount(),1ULL);
        QVERIFY(protocol.selectBinaryCommand(3,"0x3")); QCOMPARE(protocol.frameCount(),0ULL);
        QCOMPARE(protocol.binaryCommandGroups()[0].toMap()["value"].toString(),QString("0x1"));
        const auto bytes=QByteArray::fromHex("aa55010000c03f0300006040");
        protocol.feedBytes(bytes.first(5)); protocol.feedBytes(bytes.sliced(5));
        QTRY_COMPARE(protocol.latestFields().size(),4); QCOMPARE(protocol.latestFields()[3].toMap()["name"].toString(),QStringLiteral("数据区2.temperature"));
        QCOMPARE(protocol.latestFields()[3].toMap()["value"].toDouble(),3.5);
        // The final data region can be a variable array; checksum positions remain automatic.
        auto variableConfig=protocol.templateStore()->loadTemplate("builtin:multi_segment")["configuration"].toMap();
        auto variableBlocks=variableConfig["structure"].toList();
        variableBlocks.insert(1,QVariantMap{{"kind","length"},{"bytes",2},{"endian","little"},{"meaning","payload"},{"includesTrailer",false}});
        auto arrayBlock=variableBlocks[5].toMap(); auto samples=arrayBlock["fields"].toList(); auto sample=samples[0].toMap(); sample["type"]="uint16"; sample["variable"]=true; samples[0]=sample;
        arrayBlock["fields"]=samples; arrayBlock["variants"]=QVariantMap{{"0x2",samples}}; variableBlocks[5]=arrayBlock;
        auto rule=protocol.checksumPreset("sum8"); rule["name"]="sum";
        variableBlocks.append(QVariantMap{{"kind","check"},{"coverage","before"},{"rule",rule}});
        variableConfig=FrameStructure::compile(variableBlocks,variableConfig,error); QVERIFY2(!variableConfig.isEmpty(),qPrintable(error));
        BinaryConfig parsed; QVERIFY(BinaryConfig::fromMap(variableConfig,parsed,error));
        auto parser=makeProtocolParser("binary",parsed); QVERIFY(parser); int decoded=0;
        auto wire=QByteArray::fromHex("aa550900010000c03f0201000200"); quint8 sum=0; for(char byte:wire) sum+=quint8(byte); wire.append(char(sum));
        parser->feed(wire,[&](DecodedFrame frame) { ++decoded; QCOMPARE(frame.fields.last().value.toList(),QVariantList({quint64(1),quint64(2)})); });
        QCOMPARE(decoded,1);
        auto earlier=variableBlocks[3].toMap(); auto invalidFields=earlier["fields"].toList(); auto invalid=invalidFields[0].toMap(); invalid["variable"]=true; invalidFields[0]=invalid; earlier["fields"]=invalidFields; variableBlocks[3]=earlier;
        QVERIFY(FrameStructure::compile(variableBlocks,variableConfig,error).isEmpty());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&services.serialConnection());
        engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
        engine.rootContext()->setContextProperty("protocolSession",&protocol);
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml"))); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *button=window->findChild<QQuickItem *>("editBinaryProtocol"); QVERIFY(button); QVERIFY(QMetaObject::invokeMethod(button,"clicked"));
        auto *editor=window->findChild<QObject *>("binaryProtocolEditor"); QVERIFY(editor); QTRY_VERIFY(editor->property("visible").toBool());
        auto *structure=editor->findChild<QQuickItem *>("frameStructureEditor"); QVERIFY(structure);
        const auto visibleBlocks=structure->property("blocks").toList(); QCOMPARE(visibleBlocks.size(),5);
        const auto snapshot=qEnvironmentVariable("UART_MULTI_EDITOR_SNAPSHOT");
        if(!snapshot.isEmpty()) { QTest::qWait(100); QVERIFY(window->grabWindow().save(snapshot)); }
        // Add another independent command and data region through the same buttons as the UI.
        auto *kind=editor->findChild<QQuickItem *>("structureAddKind"); auto *add=editor->findChild<QQuickItem *>("structureAddBlock"); QVERIFY(kind && add);
        kind->setProperty("currentIndex",2); QVERIFY(add->isEnabled()); QVERIFY(QMetaObject::invokeMethod(add,"clicked"));
        kind->setProperty("currentIndex",4); QVERIFY(add->isEnabled()); QVERIFY(QMetaObject::invokeMethod(add,"clicked"));
        auto *save=editor->findChild<QQuickItem *>("saveBinaryProtocol"); QVERIFY(save); QVERIFY(QMetaObject::invokeMethod(save,"clicked")); QVERIFY(acceptUnsavedIfVisible(editor));
        QTRY_VERIFY(!editor->property("visible").toBool()); QCOMPARE(protocol.binaryCommandGroups().size(),3);
        QCOMPARE(protocol.binaryConfiguration()["frameSize"].toInt(),18);
        // Copies from built-ins live in the personal library, and round-trip all module definitions.
        const auto id=protocol.templateStore()->saveTemplate("多段自定义",protocol.binaryConfiguration()); QVERIFY(!id.isEmpty());
        QCOMPARE(protocol.templateStore()->loadTemplate(id)["configuration"].toMap()["structure"],protocol.binaryConfiguration()["structure"]);
    }
    void mergedProtocolEditor() {
#ifndef Q_OS_UNIX
        QSKIP("Linux pseudo-terminal merged editor test");
#else
        Pty p; QVERIFY(!p.path.isEmpty()); ApplicationServices services;
        auto &protocol=services.protocolSession(); auto &connection=services.serialConnection();
        const auto id=protocol.templateStore()->saveTemplate("网球测试",BinaryConfig::preset("robot_tennis")); QVERIFY(!id.isEmpty());
        QVERIFY(protocol.applyBinaryConfiguration(protocol.templateStore()->loadTemplate(id)["configuration"].toMap())); QVERIFY(protocol.selectProtocol("binary"));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&connection);
        engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
        engine.rootContext()->setContextProperty("protocolSession",&protocol);
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty()); auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(connection.openPort(p.path));
        auto *button=window->findChild<QQuickItem *>("editBinaryProtocol"); QVERIFY(button);
        auto *editor=window->findChild<QObject *>("binaryProtocolEditor"); QVERIFY(editor);
        QVERIFY(QMetaObject::invokeMethod(button,"clicked")); QTRY_VERIFY(editor->property("visible").toBool());
        QVERIFY(!editor->findChild<QObject *>("editFrameStructure"));
        auto *structure=editor->findChild<QQuickItem *>("frameStructureEditor"); QVERIFY(structure); QVERIFY(structure->isVisible());
        QQuickItem *name=nullptr; QTRY_VERIFY((name=visualItem(window->contentItem(),"binaryFieldName0")));
        QCOMPARE(name->property("text").toString(),QString("wheel0_rad_s"));
        name->setProperty("text","front_wheel"); QVERIFY(QMetaObject::invokeMethod(name,"textEdited"));
        auto *command=visualItem(window->contentItem(),"structureCommandSelector"); QVERIFY(command);
        command->setProperty("currentIndex",1); QVERIFY(QMetaObject::invokeMethod(command,"activated",Q_ARG(int,1)));
        QTRY_VERIFY((name=visualItem(window->contentItem(),"binaryFieldName0"))); QCOMPARE(name->property("text").toString(),QString("gyro_x"));
        name->setProperty("text","omega_x"); QVERIFY(QMetaObject::invokeMethod(name,"textEdited"));
        command=visualItem(window->contentItem(),"structureCommandSelector"); QVERIFY(command);
        command->setProperty("currentIndex",0); QVERIFY(QMetaObject::invokeMethod(command,"activated",Q_ARG(int,0)));
        QTRY_VERIFY((name=visualItem(window->contentItem(),"binaryFieldName0"))); QCOMPARE(name->property("text").toString(),QString("front_wheel"));
        command=visualItem(window->contentItem(),"structureCommandSelector"); QVERIFY(command);
        command->setProperty("currentIndex",1); QVERIFY(QMetaObject::invokeMethod(command,"activated",Q_ARG(int,1)));
        QTRY_VERIFY((name=visualItem(window->contentItem(),"binaryFieldName0"))); QCOMPARE(name->property("text").toString(),QString("omega_x"));
        auto *save=editor->findChild<QQuickItem *>("saveBinaryProtocol"); QVERIFY(save);
        QVERIFY(QMetaObject::invokeMethod(save,"clicked")); QVERIFY(acceptUnsavedIfVisible(editor)); QTRY_VERIFY(!editor->property("visible").toBool());
        const auto config=protocol.binaryConfiguration(); const auto messages=config["messages"].toList(); QCOMPARE(messages.size(),2);
        QCOMPARE(messages[0].toMap()["fields"].toList()[0].toMap()["name"].toString(),QString("front_wheel"));
        QCOMPARE(messages[1].toMap()["fields"].toList()[0].toMap()["name"].toString(),QString("omega_x"));
        QVERIFY(p.send(QByteArray::fromHex("a51000114a01010000803f0000004000004040000080403500a52400110a02010000803f0000004000004040000080400000a0400000c0400000e04000000041000010417984")));
        QTRY_COMPARE(protocol.latestFields().size(),11); QCOMPARE(protocol.latestFields()[2].toMap()["name"].toString(),QString("omega_x"));
        QCOMPARE(protocol.latestFields().last().toMap()["value"].toDouble(),9.0);
        QVERIFY(QMetaObject::invokeMethod(button,"clicked")); QTRY_VERIFY(editor->property("visible").toBool());
        auto *remove=visualItem(window->contentItem(),"removePayloadModule"); QVERIFY(remove); QVERIFY(QMetaObject::invokeMethod(remove,"clicked"));
        QTest::qWait(20); QVERIFY(!visualItem(window->contentItem(),"inlineDataFields"));
        auto *kind=editor->findChild<QQuickItem *>("structureAddKind"); QVERIFY(kind); kind->setProperty("currentIndex",4);
        auto *add=editor->findChild<QQuickItem *>("structureAddBlock"); QVERIFY(add); QVERIFY(add->isEnabled()); QVERIFY(QMetaObject::invokeMethod(add,"clicked"));
        QTRY_VERIFY((name=visualItem(window->contentItem(),"binaryFieldName0"))); QCOMPARE(name->property("text").toString(),QString("value"));
        auto *addField=visualItem(window->contentItem(),"addDataField"); QVERIFY(addField); QVERIFY(QMetaObject::invokeMethod(addField,"clicked"));
        QTRY_VERIFY(visualItem(window->contentItem(),"binaryFieldName1"));
        const auto snapshot=qEnvironmentVariable("UART_MERGED_EDITOR_SNAPSHOT");
        if(!snapshot.isEmpty()) { QTest::qWait(100); QVERIFY(window->grabWindow().save(snapshot)); }
        QVERIFY(QMetaObject::invokeMethod(editor,"close")); QCOMPARE(protocol.binaryConfiguration(),config); // Cancel keeps the active layout.
#endif
    }
    void templateLibraryUi() {
        ApplicationServices services; auto &protocol=services.protocolSession();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&services.serialConnection());
        engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
        engine.rootContext()->setContextProperty("protocolSession",&protocol);
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty()); auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *edit=window->findChild<QQuickItem *>("editBinaryProtocol"); QVERIFY(edit); QVERIFY(QMetaObject::invokeMethod(edit,"clicked"));
        auto *editor=window->findChild<QObject *>("binaryProtocolEditor"); QVERIFY(editor); QTRY_VERIFY(editor->property("visible").toBool());
        auto *picker=editor->findChild<QQuickItem *>("binaryTemplatePicker"); QVERIFY(picker);
        auto *category=editor->findChild<QQuickItem *>("binaryTemplateCategory"); QVERIFY(category);
        category->setProperty("currentIndex",0);
        QTRY_COMPARE(picker->property("count").toInt(),3);
        QVERIFY(QMetaObject::invokeMethod(category,"activated",Q_ARG(int,0)));
        auto *details=editor->findChild<QQuickItem *>("binaryTemplateDetails"); QVERIFY(details); QVERIFY(!details->isVisible());
        for(const auto &entry:protocol.templateStore()->builtInEntries()) QVERIFY(!entry.toMap()["id"].toString().contains("robot_tennis"));
        picker->setProperty("currentIndex",0); QVERIFY(QMetaObject::invokeMethod(picker,"activated",Q_ARG(int,0)));
        QVERIFY(details->isVisible());
        auto *deleteBuiltIn=editor->findChild<QQuickItem *>("deleteBinaryTemplate"); QVERIFY(deleteBuiltIn); QVERIFY(!deleteBuiltIn->isEnabled());
        const auto beforeNew=protocol.binaryConfiguration();
        auto *create=editor->findChild<QQuickItem *>("newBinaryTemplate"); QVERIFY(create);
        QVERIFY(QMetaObject::invokeMethod(create,"clicked"));
        QCOMPARE(picker->property("currentIndex").toInt(),-1);
        QVERIFY(details->isVisible());
        QCOMPARE(editor->property("selectedTemplateId").toString(),QString());
        QCOMPARE(protocol.binaryConfiguration(),beforeNew);
        const auto active=protocol.binaryConfiguration();
        auto *saveAs=editor->findChild<QQuickItem *>("saveBinaryTemplateAs"); QVERIFY(saveAs); QVERIFY(QMetaObject::invokeMethod(saveAs,"clicked"));
        auto *copy=editor->findChild<QObject *>("copyBinaryTemplateDialog"); QVERIFY(copy); QTRY_VERIFY(copy->property("visible").toBool());
        auto *copyName=copy->findChild<QQuickItem *>("copyBinaryTemplateName"); QVERIFY(copyName); copyName->setProperty("text",QStringLiteral("我的传感器"));
        auto *confirm=copy->findChild<QQuickItem *>("confirmCopyBinaryTemplate"); QVERIFY(confirm); QVERIFY(QMetaObject::invokeMethod(confirm,"clicked"));
        QTRY_VERIFY(!copy->property("visible").toBool()); QCOMPARE(protocol.templateStore()->entries().size(),1);
        const auto id=editor->property("selectedTemplateId").toString(); QVERIFY(!id.isEmpty());
        QCOMPARE(protocol.templateStore()->loadTemplate(id)["name"].toString(),QStringLiteral("我的传感器"));
        auto *name=editor->findChild<QQuickItem *>("binaryTemplateName"); QVERIFY(name); name->setProperty("text",QStringLiteral("重命名传感器"));
        auto *save=editor->findChild<QQuickItem *>("saveBinaryTemplate"); QVERIFY(save); QVERIFY(QMetaObject::invokeMethod(save,"clicked"));
        QCOMPARE(protocol.templateStore()->loadTemplate(id)["name"].toString(),QStringLiteral("重命名传感器")); QCOMPARE(protocol.templateStore()->entries().size(),1);
        const auto snapshot=qEnvironmentVariable("UART_TEMPLATE_EDITOR_SNAPSHOT");
        if(!snapshot.isEmpty()) { QTest::qWait(100); QVERIFY(window->grabWindow().save(snapshot)); }
        // Exercise the same file-dialog callbacks used after a native file selection.
        QTemporaryDir output; QVERIFY(output.isValid()); const auto file=output.filePath("传感器.json");
        auto *exportDialog=editor->findChild<QObject *>("exportBinaryTemplateDialog"); QVERIFY(exportDialog);
        QVERIFY(exportDialog->setProperty("selectedFile",QUrl::fromLocalFile(file))); QVERIFY(QMetaObject::invokeMethod(exportDialog,"accepted"));
        QVERIFY(QFile::exists(file));
        auto *importDialog=editor->findChild<QObject *>("importBinaryTemplateDialog"); QVERIFY(importDialog);
        QVERIFY(importDialog->setProperty("selectedFile",QUrl::fromLocalFile(file))); QVERIFY(QMetaObject::invokeMethod(importDialog,"accepted"));
        QCOMPARE(protocol.templateStore()->entries().size(),2); const auto imported=editor->property("selectedTemplateId").toString(); QVERIFY(imported!=id);
        auto *remove=editor->findChild<QQuickItem *>("deleteBinaryTemplate"); QVERIFY(remove); QVERIFY(QMetaObject::invokeMethod(remove,"clicked"));
        auto *deletion=editor->findChild<QObject *>("deleteBinaryTemplateDialog"); QVERIFY(deletion); QTRY_VERIFY(deletion->property("visible").toBool());
        auto *confirmDelete=deletion->findChild<QQuickItem *>("confirmDeleteBinaryTemplate"); QVERIFY(confirmDelete); QVERIFY(QMetaObject::invokeMethod(confirmDelete,"clicked"));
        QTRY_VERIFY(!deletion->property("visible").toBool()); QCOMPARE(protocol.templateStore()->entries().size(),1);
        ProtocolSession restarted; QVERIFY(restarted.templateStore()->loadTemplate(imported).isEmpty());
        QCOMPARE(restarted.templateStore()->loadTemplate(id)["name"].toString(),QStringLiteral("重命名传感器"));
        QCOMPARE(protocol.binaryConfiguration(),active); // File actions never apply or reset the running protocol.
        QVERIFY(!editor->findChild<QObject *>("binaryChecksumAlgorithm")); QVERIFY(!editor->findChild<QObject *>("binaryAdvancedToggle"));
    }
    void simpleProtocolTemplates() {
#ifndef Q_OS_UNIX
        QSKIP("Linux pseudo-terminal template UI test");
#else
        Pty p; QVERIFY(!p.path.isEmpty()); ApplicationServices services;
        auto &protocol=services.protocolSession(); auto &connection=services.serialConnection();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&connection);
        engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
        engine.rootContext()->setContextProperty("protocolSession",&protocol);
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty()); auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(protocol.selectProtocol("binary")); QVERIFY(connection.openPort(p.path));
        auto *tab=window->findChild<QQuickItem *>("protocolTab"); QVERIFY(tab);
        QVERIFY(QMetaObject::invokeMethod(tab,"clicked"));
        auto *mainTemplate=window->findChild<QQuickItem *>("protocolSelector"); QVERIFY(mainTemplate);
        auto *button=window->findChild<QQuickItem *>("editBinaryProtocol"); QVERIFY(button);
        auto *editor=window->findChild<QObject *>("binaryProtocolEditor"); QVERIFY(editor);
        QVERIFY(QMetaObject::invokeMethod(button,"clicked")); QTRY_VERIFY(editor->property("visible").toBool());
        auto *picker=editor->findChild<QQuickItem *>("binaryTemplatePicker"); QVERIFY(picker);
        const auto personalId=protocol.templateStore()->saveTemplate("网球机器人上行 · A5",BinaryConfig::preset("robot_tennis")); QVERIFY(!personalId.isEmpty());
        const int robotIndex=templateIndex(protocol,personalId); QVERIFY(robotIndex>=0); picker->setProperty("currentIndex",robotIndex); QVERIFY(QMetaObject::invokeMethod(picker,"activated",Q_ARG(int,robotIndex)));
        QQuickItem *name=nullptr; QTRY_VERIFY((name=visualItem(window->contentItem(),"binaryFieldName0")));
        name->setProperty("text","front_wheel"); QVERIFY(QMetaObject::invokeMethod(name,"textEdited"));
        QVERIFY(!editor->findChild<QQuickItem *>("binaryMessagePicker"));
        const auto snapshot=qEnvironmentVariable("UART_SIMPLE_EDITOR_SNAPSHOT");
        if(!snapshot.isEmpty()) { QTest::qWait(100); QVERIFY(window->grabWindow().save(snapshot)); }
        auto *save=editor->findChild<QQuickItem *>("saveBinaryProtocol"); QVERIFY(save);
        QVERIFY(QMetaObject::invokeMethod(save,"clicked")); QVERIFY(acceptUnsavedIfVisible(editor)); QTRY_VERIFY(!editor->property("visible").toBool());
        QCOMPARE(protocol.binaryConfiguration()["templateId"].toString(),personalId);
        QCOMPARE(protocol.binaryTemplateName(),QStringLiteral("网球机器人上行 · A5"));
        QTRY_COMPARE(mainTemplate->property("text").toString(),protocol.binaryTemplateName());
        QCOMPARE(protocol.binaryConfiguration()["messages"].toList().size(),2);
        const auto wheels=QByteArray::fromHex("a51000114a01010000803f0000004000004040000080403500");
        const auto imu=QByteArray::fromHex("a52400110a02010000803f0000004000004040000080400000a0400000c0400000e04000000041000010417984");
        QVERIFY(p.send(wheels)); QTRY_COMPARE(protocol.latestFields().size(),6);
        QCOMPARE(protocol.latestFields()[2].toMap()["name"].toString(),QString("front_wheel"));
        QCOMPARE(protocol.latestFields().last().toMap()["value"].toDouble(),4.0);
        QVERIFY(p.send(imu)); QTest::qWait(70); QCOMPARE(protocol.latestFields().size(),6);
        QVERIFY(protocol.selectBinaryChannel("0x102")); QVERIFY(p.send(imu)); QTRY_COMPARE(protocol.latestFields().size(),11);
        QCOMPARE(protocol.latestFields().last().toMap()["value"].toDouble(),9.0);
        QVERIFY(QMetaObject::invokeMethod(button,"clicked")); QTRY_VERIFY(editor->property("visible").toBool());
        QVERIFY(!editor->findChild<QObject *>("binaryChecksumAlgorithm"));
        QVERIFY(!editor->findChild<QObject *>("binaryAdvancedToggle"));
        ProtocolSession restored;
        QCOMPARE(restored.binaryTemplateName(),protocol.binaryTemplateName());
        auto restoredConfig=restored.binaryConfiguration(),activeConfig=protocol.binaryConfiguration(); QCOMPARE(restoredConfig["messages"],activeConfig["messages"]); QCOMPARE(restoredConfig["checks"],activeConfig["checks"]);
#endif
    }
    void unsavedTemplatePrompt() {
        ApplicationServices services; auto &protocol=services.protocolSession();
        const auto id=protocol.templateStore()->saveTemplate("保存提醒测试",BinaryConfig::preset("generic")); QVERIFY(!id.isEmpty());
        const auto original=protocol.templateStore()->loadTemplate(id)["configuration"].toMap();
        QVERIFY(protocol.applyBinaryConfiguration(original)); QVERIFY(protocol.selectProtocol("binary"));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&services.serialConnection());
        engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
        engine.rootContext()->setContextProperty("protocolSession",&protocol);
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml"))); QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *open=window->findChild<QQuickItem *>("editBinaryProtocol"); auto *editor=window->findChild<QObject *>("binaryProtocolEditor"); QVERIFY(open && editor);
        auto *apply=editor->findChild<QQuickItem *>("saveBinaryProtocol"); auto *prompt=editor->findChild<QObject *>("unsavedBinaryTemplateDialog"); QVERIFY(apply && prompt);
        QVERIFY(QMetaObject::invokeMethod(open,"clicked")); QTRY_VERIFY(editor->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(apply,"clicked")); QTRY_VERIFY(!editor->property("visible").toBool()); QVERIFY(!prompt->property("visible").toBool());
        // An edit reverted to its original value must not cause a warning.
        QVERIFY(QMetaObject::invokeMethod(open,"clicked")); QTRY_VERIFY(editor->property("visible").toBool());
        auto *field=visualItem(window->contentItem(),"binaryFieldName0"); QVERIFY(field);
        field->setProperty("text","changed"); QVERIFY(QMetaObject::invokeMethod(field,"textEdited"));
        field->setProperty("text","value"); QVERIFY(QMetaObject::invokeMethod(field,"textEdited"));
        QVERIFY(QMetaObject::invokeMethod(apply,"clicked")); QTRY_VERIFY(!editor->property("visible").toBool()); QVERIFY(!prompt->property("visible").toBool());
        // Return to editing preserves edits and does not apply them.
        QVERIFY(QMetaObject::invokeMethod(open,"clicked")); QTRY_VERIFY(editor->property("visible").toBool());
        field=visualItem(window->contentItem(),"binaryFieldName0"); QVERIFY(field); field->setProperty("text","temperature"); QVERIFY(QMetaObject::invokeMethod(field,"textEdited"));
        const auto before=protocol.binaryConfiguration(); QVERIFY(QMetaObject::invokeMethod(apply,"clicked")); QTRY_VERIFY(prompt->property("visible").toBool()); QCOMPARE(protocol.binaryConfiguration(),before);
        auto *back=prompt->findChild<QQuickItem *>("returnToBinaryEditor"); QVERIFY(back); QVERIFY(QMetaObject::invokeMethod(back,"clicked")); QTRY_VERIFY(!prompt->property("visible").toBool()); QVERIFY(editor->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(apply,"clicked")); QTRY_VERIFY(prompt->property("visible").toBool());
        QVERIFY(!prompt->findChild<QQuickItem *>("applyWithoutSavingTemplate"));
        QCOMPARE(protocol.binaryConfiguration(),before);
        QCOMPARE(protocol.templateStore()->loadTemplate(id)["configuration"].toMap()["fields"].toList()[0].toMap()["name"].toString(),QString("value"));
        auto *save=prompt->findChild<QQuickItem *>("saveTemplateAndApply"); QVERIFY(save); QVERIFY(QMetaObject::invokeMethod(save,"clicked")); QTRY_VERIFY(!editor->property("visible").toBool()); QTRY_VERIFY(!prompt->property("visible").toBool());
        QCOMPARE(protocol.templateStore()->loadTemplate(id)["configuration"].toMap()["fields"].toList()[0].toMap()["name"].toString(),QString("temperature"));
        QVERIFY(QMetaObject::invokeMethod(open,"clicked")); QTRY_VERIFY(editor->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(apply,"clicked")); QTRY_VERIFY(!editor->property("visible").toBool()); QVERIFY(!prompt->property("visible").toBool());
    }
    void editableProtocols() {
        ApplicationServices services; auto &protocol=services.protocolSession();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&services.serialConnection());
        engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
        engine.rootContext()->setContextProperty("protocolSession",&protocol);
        QVERIFY(protocol.selectProtocol("binary"));
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty()); auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *button=window->findChild<QQuickItem *>("editBinaryProtocol"); QVERIFY(button); QVERIFY(QMetaObject::invokeMethod(button,"clicked"));
        auto *editor=window->findChild<QObject *>("binaryProtocolEditor"); QVERIFY(editor); QTRY_VERIFY(editor->property("visible").toBool());
        auto *create=editor->findChild<QQuickItem *>("newBinaryTemplate"); QVERIFY(create); QVERIFY(QMetaObject::invokeMethod(create,"clicked"));
        QQuickItem *header=nullptr; QTRY_VERIFY((header=visualItem(window->contentItem(),"structureHex0")));
        header->setProperty("text","GG"); QVERIFY(QMetaObject::invokeMethod(header,"editingFinished"));
        auto *save=editor->findChild<QQuickItem *>("saveBinaryProtocol"); QVERIFY(save); QVERIFY(QMetaObject::invokeMethod(save,"clicked"));
        QVERIFY(editor->property("visible").toBool()); QVERIFY(!editor->property("localError").toString().isEmpty());
        header=visualItem(window->contentItem(),"structureHex0"); QVERIFY(header); header->setProperty("text","AA 55"); QVERIFY(QMetaObject::invokeMethod(header,"editingFinished"));
        QVERIFY(QMetaObject::invokeMethod(save,"clicked")); QVERIFY(acceptUnsavedIfVisible(editor)); QTRY_VERIFY(!editor->property("visible").toBool());
        protocol.feedBytes(QByteArray::fromHex("aa550000c03f")); QTRY_COMPARE(protocol.latestFields().size(),1);
        QCOMPARE(protocol.latestFields().first().toMap()["value"].toDouble(),1.5);
    }
    void unifiedReceiverLifecycle() {
#ifndef Q_OS_UNIX
        QSKIP("Linux pseudo-terminal receiver lifecycle test");
#else
        Pty p; QVERIFY(!p.path.isEmpty());
        ApplicationServices services;
        auto &connection=services.serialConnection();
        auto &receive=services.receiveSession();
        receive.setShowTimestamps(false);
        auto &protocol=services.protocolSession();
        QSignalSpy resets(&protocol,&ProtocolSession::sessionReset);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("numericStream",&services.numericStream());
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&connection);
        engine.rootContext()->setContextProperty("receiveSession",&receive);
        engine.rootContext()->setContextProperty("protocolSession",&protocol);
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QTRY_VERIFY(window->isExposed());
        auto *workspace=window->findChild<QQuickItem *>("receiveWorkspace"); QVERIFY(workspace);
        auto *rawPage=window->findChild<QQuickItem *>("rawReceivePage"); QVERIFY(rawPage);
        auto *protocolPage=window->findChild<QQuickItem *>("protocolPage"); QVERIFY(protocolPage);
        auto *rawTab=window->findChild<QQuickItem *>("rawDataTab"); QVERIFY(rawTab);
        auto *protocolTab=window->findChild<QQuickItem *>("protocolTab"); QVERIFY(protocolTab);
        auto *output=window->findChild<QQuickItem *>("receiveText"); QVERIFY(output);
        auto *selector=window->findChild<QQuickItem *>("protocolSelector"); QVERIFY(selector);
        auto *status=window->findChild<QQuickItem *>("protocolStatus"); QVERIFY(status);
        QTRY_VERIFY(rawPage->isVisible()); QVERIFY(!protocolPage->isVisible());
        QVERIFY(!protocol.selectProtocol("unknown")); QCOMPARE(resets.size(),0);
        QVERIFY(!connection.openPort("/invalid/protocol-test")); QCOMPARE(resets.size(),0);
        QVERIFY(connection.openPort(p.path)); QCOMPARE(resets.size(),1);
        QVERIFY(p.send("first\n")); QTRY_COMPARE(output->property("text").toString(),QString("[RX] first\n"));
        receive.setHexMode(true);
        window->setProperty("autoScroll",false);

        QVERIFY(QMetaObject::invokeMethod(protocolTab,"clicked"));
        QVERIFY(protocolPage->isVisible()); QVERIFY(!rawPage->isVisible());
        QCOMPARE(selector->property("text").toString(),QStringLiteral("选择协议"));
        QCOMPARE(status->property("text").toString(),QStringLiteral("未启用协议解析"));
        QVERIFY(QMetaObject::invokeMethod(selector,"clicked"));
        QQuickItem *csv=nullptr;
        QTRY_VERIFY((csv=visualItem(window->contentItem(),"protocolOption_csv")) && csv->isVisible());
        QVERIFY(QMetaObject::invokeMethod(csv,"clicked"));
        QCOMPARE(resets.size(),2);
        QVERIFY(p.send("1,")); QTRY_COMPARE(receive.receivedBytes(),8ULL);
        QCOMPARE(protocol.frameCount(),0ULL);

        QVERIFY(QMetaObject::invokeMethod(rawTab,"clicked"));
        QTRY_VERIFY(rawPage->isVisible()); QVERIFY(!protocolPage->isVisible());
        QVERIFY(receive.hexMode()); QVERIFY(!window->property("autoScroll").toBool());
        QVERIFY(p.send("2\n3,4\n")); QTRY_COMPARE(receive.receivedBytes(),14ULL);
        QTRY_COMPARE(protocol.frameCount(),2ULL);
        QTRY_COMPARE(services.numericStream().channels().size(),2);
        const auto hexPayload = [&] {
            auto log = output->property("text").toString();
            log.replace("[RX] ","").replace('\n',' ');
            return log;
        };
        QTRY_COMPARE(hexPayload(),QString("66 69 72 73 74 0A 31 2C 32 0A 33 2C 34 0A"));
        QVERIFY(QMetaObject::invokeMethod(protocolTab,"clicked"));
        QCOMPARE(resets.size(),2); QVERIFY(connection.connected());
        QCOMPARE(protocol.selectedProtocolId(),QString("csv"));
        QTRY_COMPARE(protocol.latestFields().size(),2);
        QTRY_COMPARE(protocol.latestFields().at(1).toMap()["value"].toDouble(),4.0);
        const QString snapshot=qEnvironmentVariable("UART_PROTOCOL_SNAPSHOT");
        if(!snapshot.isEmpty()) { QTest::qWait(100); QVERIFY(window->grabWindow().save(snapshot)); }

        QVERIFY(QMetaObject::invokeMethod(rawTab,"clicked"));
        receive.setHexMode(false); receive.setDisplayPaused(true);
        const auto frozen=receive.displayText();
        QVERIFY(p.send("5,6\n")); QTRY_COMPARE(receive.receivedBytes(),18ULL);
        QTRY_COMPARE(protocol.frameCount(),3ULL); QCOMPARE(output->property("text").toString(),frozen);
        receive.setDisplayPaused(false);
        QTRY_COMPARE(output->property("text").toString(),QString("[RX] first\n[RX] 1,2\n[RX] 3,4\n[RX] 5,6\n"));
        receive.clear(); QCOMPARE(resets.size(),3); QCOMPARE(receive.receivedBytes(),0ULL);
        QCOMPARE(protocol.selectedProtocolId(),QString("csv"));
        QVERIFY(receive.displayText().isEmpty());
        connection.disconnectPort(); QCOMPARE(resets.size(),3);
        QVERIFY(connection.openPort(p.path)); QCOMPARE(resets.size(),4);
        QCOMPARE(receive.receivedBytes(),0ULL); QVERIFY(rawPage->isVisible());
        QCOMPARE(protocol.selectedProtocolId(),QString("csv"));
        QVERIFY(!protocol.selectProtocol("missing")); QCOMPARE(resets.size(),4);
#endif
    }
    void recordingDuplexUi() {
#ifndef Q_OS_UNIX
        QSKIP("Linux pseudo-terminal recording test");
#else
        Pty peer; QVERIFY(!peer.path.isEmpty());
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const auto parent = QUrl::fromLocalFile(qEnvironmentVariable("UART_RECORDING_TEST_DIR", temporary.path()));
        ApplicationServices services;
        auto &connection=services.serialConnection();
        auto &protocol=services.protocolSession();
        auto &recorder=services.recorder();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("appSettings",&services.settings());
        engine.rootContext()->setContextProperty("serialConnection",&connection);
        engine.rootContext()->setContextProperty("receiveSession",&services.receiveSession());
        engine.rootContext()->setContextProperty("protocolSession",&protocol);
        engine.rootContext()->setContextProperty("csvRecorder",&recorder);
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *button=window->findChild<QQuickItem *>("recordingButton"); QVERIFY(button);
        auto *folder=window->findChild<QObject *>("recordingDirectoryPicker"); QVERIFY(folder);
        auto *status=window->findChild<QQuickItem *>("recordingStatus"); QVERIFY(status);
        auto *rawTab=window->findChild<QQuickItem *>("rawDataTab"); QVERIFY(rawTab);
        auto *protocolTab=window->findChild<QQuickItem *>("protocolTab"); QVERIFY(protocolTab);

        QVERIFY(!button->isEnabled());
        QVERIFY(connection.openPort(peer.path));
        QVERIFY(!button->isEnabled());
        QVERIFY(protocol.selectProtocol("keyvalue"));
        QTRY_VERIFY(button->isEnabled());
        QVERIFY(folder->setProperty("currentFolder",parent));
        QVERIFY(QMetaObject::invokeMethod(button,"clicked"));
        QTRY_VERIFY(folder->property("visible").toBool());
        QVERIFY(folder->setProperty("selectedFolder",parent));
        QVERIFY(QMetaObject::invokeMethod(folder,"accept"));
        QVERIFY2(recorder.recording(),qPrintable(recorder.status()));
        QCOMPARE(button->property("text").toString(),QStringLiteral("停止录制"));
        const auto dictionaryDir=recorder.directory();
        auto csv=[](const QString &directory,const QString &suffix={}) {
            const QDir path(directory);
            QFile file(path.filePath(path.dirName()+suffix+".csv"));
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };

        QVERIFY(services.sendSession().send("temperature:99",false,"lf"));
        QVERIFY(peer.send("temperature:1,speed:2\nspeed:3,temperature:2\n"));
        QTRY_COMPARE(protocol.frameCount(),2ULL);
        QVERIFY(QMetaObject::invokeMethod(protocolTab,"clicked"));
        QVERIFY(QMetaObject::invokeMethod(rawTab,"clicked"));
        services.receiveSession().setDisplayPaused(true);
        protocol.setDisplayPaused(true);
        services.receiveSession().clear();
        QVERIFY(protocol.selectProtocol("keyvalue"));
        QVERIFY(recorder.recording());
        QVERIFY(peer.send("speed:4\nspeed:5,current:6\nbad frame\n"));
        QTRY_COMPARE(protocol.frameCount(),2ULL);
        QVERIFY(protocol.selectProtocol("csv"));
        QVERIFY(!recorder.recording());
        QVERIFY(peer.send("999\n"));
        QTRY_VERIFY(!recorder.stopping());
        QCOMPARE(recorder.writtenFrames(),4ULL);
        QVERIFY(status->property("text").toString().contains(QStringLiteral("数据协议已切换")));
        const auto first=csv(dictionaryDir);
        const auto second=csv(dictionaryDir,"_002");
        QVERIFY(first.startsWith("\xef\xbb\xbftimestamp_utc,elapsed_us,frame,data.temperature,data.speed\r\n"));
        QVERIFY(second.startsWith("\xef\xbb\xbftimestamp_utc,elapsed_us,frame,data.temperature,data.speed,data.current\r\n"));
        for(const auto &suffix : {QByteArray(",1,1,2\r\n"),QByteArray(",2,2,3\r\n"),QByteArray(",3,,4\r\n")}) {
            QVERIFY(first.contains(suffix));
        }
        QVERIFY(second.endsWith(",4,,5,6\r\n"));
        QCOMPARE(first.count('\n'),4);
        QCOMPARE(second.count('\n'),2);
        const QRegularExpression time(QStringLiteral(R"(\n\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{6}Z,\d+,1,1,2)"));
        QVERIFY(time.match(QString::fromUtf8(first)).hasMatch());

        QVERIFY(recorder.start(parent));
        QVERIFY(peer.send("10,20\n11,21\n"));
        QTRY_COMPARE(recorder.writtenFrames(),2ULL);
        const auto textDir=recorder.directory();
        QVERIFY(QMetaObject::invokeMethod(button,"clicked"));
        QTRY_VERIFY(!recorder.stopping());
        QVERIFY(csv(textDir).endsWith(",2,11,21\r\n"));

        const auto variable=protocol.binaryTemplate("builtin:simple_variable");
        const auto fixed=protocol.binaryTemplate("builtin:simple_fixed");
        QVERIFY(protocol.applyBinaryConfiguration(variable));
        QVERIFY(protocol.selectProtocol("binary"));
        QVERIFY(recorder.start(parent));
        const auto binaryDir=recorder.directory();
        QVERIFY(peer.send(QByteArray::fromHex("aa5508000000803f00000040")));
        QTRY_COMPARE(protocol.frameCount(),1ULL);
        QVERIFY(protocol.applyBinaryConfiguration(variable)); // Reapplying the same configuration keeps recording.
        QVERIFY(recorder.recording());
        QVERIFY(protocol.applyBinaryConfiguration(fixed));
        QVERIFY(!recorder.recording());
        QVERIFY(peer.send(QByteArray::fromHex("aa5500004040")));
        QTRY_VERIFY(!recorder.stopping());
        QCOMPARE(recorder.writtenFrames(),1ULL);
        QVERIFY(csv(binaryDir).endsWith(",1,\"[1,2]\"\r\n"));

        QVERIFY(protocol.applyBinaryConfiguration(BinaryConfig::preset("robot_tennis")));
        QVERIFY(protocol.selectBinaryChannel("0x101"));
        QVERIFY(recorder.start(parent));
        QVERIFY(protocol.selectBinaryChannel("0x101"));
        QVERIFY(recorder.recording());
        QVERIFY(peer.send(QByteArray::fromHex("a51000114a01010000803f0000004000004040000080403500")));
        QTRY_COMPARE(protocol.frameCount(),1ULL);
        QVERIFY(protocol.selectBinaryChannel("0x102"));
        QTRY_VERIFY(!recorder.recording() && !recorder.stopping());
        QCOMPARE(recorder.writtenFrames(),1ULL);
        QVERIFY(recorder.status().contains(QStringLiteral("命令字")));

        QVERIFY(recorder.start(parent));
        connection.disconnectPort();
        QVERIFY(!recorder.recording());
        QTRY_VERIFY(!recorder.stopping());
        QVERIFY(recorder.status().contains(QStringLiteral("串口已断开")));
        QVERIFY(!button->isEnabled());
        window->close();
#endif
    }
    void recordingWriteFailureAndClose() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const auto parent=QUrl::fromLocalFile(temporary.path());
        const DecodedFrame first{{{QStringLiteral("速度"),"uint64",
                                  QVariant::fromValue(std::numeric_limits<quint64>::max()),8,1}},{} ,{}};
        const DecodedFrame second{{{"current","float64",1.25,8,1}},{} ,{}};
        QString saved;
        {
            CsvRecorder recorder;
            recorder.setAvailable(true);
            QVERIFY(!recorder.start(QUrl::fromLocalFile(temporary.filePath("missing"))));
            QVERIFY(recorder.failed());
            QVERIFY(recorder.start(parent));
            const auto directory=QDir(recorder.directory());
            recorder.appendFrame(first);
            QTRY_COMPARE(recorder.writtenFrames(),1ULL);
            QVERIFY(directory.mkdir(directory.dirName()+"_002.csv")); // Simulate failure creating the next segment.
            recorder.appendFrame(second);
            QTRY_VERIFY(recorder.failed());
            QVERIFY(!recorder.recording());
            QCOMPARE(recorder.writtenFrames(),1ULL);
            QFile retained(directory.filePath(directory.dirName()+".csv")); QVERIFY(retained.open(QIODevice::ReadOnly));
            QVERIFY(retained.readAll().endsWith(",1,18446744073709551615\r\n"));

            QVERIFY(recorder.start(parent));
            const QDir nextDirectory(recorder.directory());
            saved=nextDirectory.filePath(nextDirectory.dirName()+".csv");
            for (int i=0;i<100;++i) recorder.appendFrame(first);
        } // Normal application teardown drains the background queue.
        QFile drained(saved); QVERIFY(drained.open(QIODevice::ReadOnly));
        const auto contents=drained.readAll();
        QCOMPARE(contents.count('\n'),101);
        QVERIFY(contents.endsWith(",100,18446744073709551615\r\n"));
    }
    void qmlReception() {
#ifndef Q_OS_UNIX
        QSKIP("Linux pseudo-terminal UI test");
#else
        Pty p; QVERIFY(!p.path.isEmpty());
        ApplicationServices services;
        auto &c = services.receiveSession(); auto &connection = services.serialConnection(); QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("appSettings", &services.settings());
        engine.rootContext()->setContextProperty("serialConnection", &connection);
        engine.rootContext()->setContextProperty("receiveSession", &c);
        engine.rootContext()->setContextProperty("protocolSession", &services.protocolSession());
        engine.load(QUrl::fromLocalFile(QStringLiteral(UART_SOURCE_DIR "/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto *output=window->findChild<QQuickItem *>("receiveText"); QVERIFY(output);
        auto *combo=window->findChild<QQuickItem *>("baudCombo"); QVERIFY(combo);
        auto *popup=combo->property("popup").value<QObject *>(); QVERIFY(popup);
        QVERIFY(QMetaObject::invokeMethod(popup,"open")); QTest::qWait(50);
        QVERIFY(popup->property("height").toDouble()<=260);
        auto *background=popup->property("background").value<QObject *>(); QVERIFY(background);
        QCOMPARE(background->property("color").value<QColor>(),QColor("white"));
        QVERIFY(QMetaObject::invokeMethod(popup,"close"));
        auto *baudInput=combo->property("contentItem").value<QQuickItem *>(); QVERIFY(baudInput);
        QTRY_VERIFY(!popup->property("visible").toBool());
        const auto baudPoint=baudInput->mapToScene(QPointF(20,baudInput->height()/2)).toPoint();
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,baudPoint);
        QTRY_VERIFY(baudInput->hasActiveFocus());
        QTRY_COMPARE(window->cursor().shape(),Qt::IBeamCursor);
        QVERIFY(QMetaObject::invokeMethod(baudInput,"selectAll"));
        QTest::mouseClick(window,Qt::RightButton,Qt::NoModifier,baudPoint);
        auto *baudMenu=baudInput->findChild<QObject *>("textEditingMenu"); QVERIFY(baudMenu);
        QTRY_VERIFY(baudMenu->property("opened").toBool());
        auto *baudMenuContent=baudMenu->property("contentItem").value<QQuickItem *>(); QVERIFY(baudMenuContent);
        auto *cut=visualItem(baudMenuContent,"cutInputText"); QVERIFY(cut && cut->isEnabled());
        auto *deleteText=visualItem(baudMenuContent,"deleteInputText"); QVERIFY(deleteText && deleteText->isEnabled());
        QCOMPARE(cut->property("text").toString(),QStringLiteral("剪切"));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,
                          deleteText->mapToScene(QPointF(deleteText->width()/2,deleteText->height()/2)).toPoint());
        QCOMPARE(combo->property("editText").toString(),QString());
        combo->setProperty("editText","115200");
        for (int attempt=0;attempt<2;++attempt) {
            QTRY_VERIFY(!baudMenu->property("visible").toBool());
            QTest::mouseClick(window,Qt::RightButton,Qt::NoModifier,baudPoint);
            QTRY_VERIFY(baudMenu->property("opened").toBool());
            QTest::keyClick(window,Qt::Key_Escape);
        }
        QTRY_VERIFY(!baudMenu->property("visible").toBool());
        QVERIFY(output->property("readOnly").toBool()); QVERIFY(output->property("selectByMouse").toBool());
        QVERIFY(connection.openPort(p.path));
        QVERIFY(p.send("hello\n"));
        auto *timestamp=window->findChild<QQuickItem *>("receiveTimestampToggle"); QVERIFY(timestamp);
        QVERIFY(timestamp->property("checked").toBool());
        const QRegularExpression timedLine(QStringLiteral(R"(^\[RX\] \[\d{2}:\d{2}:\d{2}\.\d{6}\] hello\n$)"));
        QTRY_VERIFY(timedLine.match(output->property("text").toString()).hasMatch());
        const auto shown=output->property("text").toString();
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        const auto togglePoint=timestamp->mapToScene(QPointF(timestamp->width()/2,timestamp->height()/2)).toPoint();
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,togglePoint);
        QTRY_COMPARE(output->property("text").toString(),QString("[RX] hello\n"));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,togglePoint);
        QTRY_COMPARE(output->property("text").toString(),shown);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,togglePoint);
        QTRY_COMPARE(output->property("text").toString(),QString("[RX] hello\n"));
        auto *logDocument=output->property("textDocument").value<QQuickTextDocument *>();
        QVERIFY(logDocument); QVERIFY(!logDocument->textDocument()->isUndoRedoEnabled()); QCOMPARE(logDocument->textDocument()->availableUndoSteps(),0);
        auto *baud=window->findChild<QQuickItem *>("baudCombo"); QVERIFY(baud); QVERIFY(!baud->isEnabled());
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        const QFontMetricsF metrics(output->property("font").value<QFont>());
        const auto start=output->mapToScene(QPointF(14+metrics.horizontalAdvance("[RX] "),22)).toPoint();
        const auto end=output->mapToScene(QPointF(14+metrics.horizontalAdvance("[RX] hello"),22)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseMove(window,end,1);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,end);
        QCOMPARE(output->property("selectedText").toString(),QString("hello"));
        QGuiApplication::clipboard()->clear();
        QTest::keyClick(window,Qt::Key_C,Qt::ControlModifier);
        QCOMPARE(QGuiApplication::clipboard()->text(),QString("hello"));
        c.setDisplayPaused(true); QVERIFY(p.send("paused\n")); QTRY_COMPARE(c.receivedBytes(),13ULL);
        QTest::qWait(70); QCOMPARE(output->property("text").toString(),QString("[RX] hello\n"));
        c.setDisplayPaused(false); QTRY_COMPARE(output->property("text").toString(),QString("[RX] hello\n[RX] paused\n"));
        c.setHexMode(true); QTRY_VERIFY(output->property("text").toString().startsWith("[RX] 68 65 6C 6C 6F"));
        c.clear(); QTRY_COMPARE(output->property("text").toString(),QString());
        c.setHexMode(false);
        window->setProperty("autoScroll",false);
        const QByteArray lines=QByteArray("scroll line\n").repeated(100);
        QVERIFY(p.send(lines)); QTRY_VERIFY(output->property("text").toString().contains("scroll line"));
        QTest::qWait(100);
        auto *flick=window->findChild<QQuickItem *>("receiveFlickable"); QVERIFY(flick);
        flick->setProperty("contentY",40.0);
        QVERIFY(p.send("tail\n")); QTRY_VERIFY(output->property("text").toString().endsWith("tail\n"));
        QTest::qWait(70); QVERIFY(qAbs(flick->property("contentY").toDouble()-40)<2);
        window->setProperty("autoScroll",true); QVERIFY(p.send("follow\n"));
        QTRY_VERIFY(output->property("text").toString().endsWith("follow\n")); QTest::qWait(70);
        const double bottom=flick->property("contentHeight").toDouble()-flick->height();
        QVERIFY(qAbs(flick->property("contentY").toDouble()-bottom)<3);
        window->resize(960,680); QTest::qWait(100); QVERIFY(window->width()>=960);
#endif
    }
};
int main(int argc,char **argv) {
    QGuiApplication app(argc,argv); QQuickStyle::setStyle("Basic");
    registerUartUiTypes();
    app.setOrganizationName("UartToolTests");
    app.setApplicationName("receiver_tests");
    QTemporaryDir settingsDirectory;
    if(!settingsDirectory.isValid()) return 2;
    qputenv("XDG_DATA_HOME",settingsDirectory.path().toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settingsDirectory.path());
    ReceiverTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "receiver_tests.moc"
