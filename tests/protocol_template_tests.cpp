#include "protocols/protocoltemplatestore.h"
#include "protocols/binaryconfig.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

class ProtocolTemplateTests : public QObject {
    Q_OBJECT
private slots:
    void filesRenameAndDeletionPersist() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); const auto library=temp.filePath("library");
        ProtocolTemplateStore store(nullptr,library); QCOMPARE(store.entries().size(),0); QVERIFY2(store.lastError().isEmpty(),qPrintable(store.lastError()));
        const auto config=BinaryConfig::preset("generic");
        const auto id=store.saveTemplate(QStringLiteral("我的传感器"),config); QVERIFY2(!id.isEmpty(),qPrintable(store.lastError()));
        QCOMPARE(store.entries().size(),1); const auto original=store.loadTemplate(id);
        QCOMPARE(original["name"].toString(),QStringLiteral("我的传感器"));
        QCOMPARE(original["configuration"].toMap()["fields"],config["fields"]);
        QVERIFY(QFile::exists(library+"/"+id+".json"));
        QCOMPARE(store.saveTemplate(QStringLiteral("改名后"),config,id),id); QCOMPARE(store.entries().size(),1);
        ProtocolTemplateStore reopened(nullptr,library); QCOMPARE(reopened.loadTemplate(id)["name"].toString(),QStringLiteral("改名后"));
        QVERIFY(reopened.saveTemplate(QStringLiteral("改名后"),config).isEmpty()); QVERIFY(!reopened.lastError().isEmpty());
        auto invalid=config; invalid["header"]="GG";
        QVERIFY(reopened.saveTemplate("bad",invalid,id).isEmpty()); QCOMPARE(reopened.loadTemplate(id)["name"].toString(),QStringLiteral("改名后"));
        QVERIFY(reopened.deleteTemplate(id)); QVERIFY(!QFile::exists(library+"/"+id+".json"));
        QCOMPARE(reopened.builtInEntries().size(),3);
        QVERIFY(!reopened.deleteTemplate("builtin:simple_fixed"));
        QVERIFY(!reopened.loadTemplate("builtin:simple_fixed").isEmpty());
        QVERIFY(reopened.saveTemplate("禁止覆盖",config,"builtin:simple_fixed").isEmpty());
        ProtocolTemplateStore again(nullptr,library); QCOMPARE(again.entries().size(),0);
        QVERIFY(again.loadTemplate(id).isEmpty()); QVERIFY(again.loadTemplate("robot_tennis").isEmpty());
    }
    void legacyTemplatesStayPersonalWhenEdited() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); ProtocolTemplateStore store(nullptr,temp.path());
        QVERIFY(QFile::copy(":/protocol-templates/robot_tennis.json",temp.filePath("robot_tennis.json")));
        QVERIFY(QFile::copy(":/protocol-templates/simple_variable.json",temp.filePath("simple_variable.json")));
        QFile seed(":/protocol-templates/simple_fixed.json"); QVERIFY(seed.open(QIODevice::ReadOnly));
        auto changed=QJsonDocument::fromJson(seed.readAll()).object(); changed["name"]=QStringLiteral("以前修改过的模板");
        QFile personal(temp.filePath("simple_fixed.json")); QVERIFY(personal.open(QIODevice::WriteOnly)); personal.write(QJsonDocument(changed).toJson()); personal.close();
        store.refresh(); QCOMPARE(store.entries().size(),2);
        QVERIFY(!store.loadTemplate("robot_tennis").isEmpty());
        QCOMPARE(store.loadTemplate("simple_fixed")["name"].toString(),QStringLiteral("以前修改过的模板"));
        for(const auto &entry:store.builtInEntries()) QVERIFY(!entry.toMap()["id"].toString().contains("robot_tennis"));
    }
    void exportImportAndExternalEdits() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); ProtocolTemplateStore source(nullptr,temp.filePath("source"));
        const auto config=BinaryConfig::preset("robot_tennis"); const auto path=temp.filePath("传感器.json");
        QVERIFY2(source.exportConfiguration("robot",config,QUrl::fromLocalFile(path)),qPrintable(source.lastError()));
        QFile exported(path); QVERIFY(exported.open(QIODevice::ReadOnly)); auto doc=QJsonDocument::fromJson(exported.readAll()); exported.close();
        QCOMPARE(doc.object()["format"].toString(),QString("uart-tool.binary-protocol")); QCOMPARE(doc.object()["version"].toInt(),1);
        ProtocolTemplateStore destination(nullptr,temp.filePath("destination"));
        const auto id=destination.importTemplate(QUrl::fromLocalFile(path)); QVERIFY2(!id.isEmpty(),qPrintable(destination.lastError()));
        auto loaded=destination.loadTemplate(id); QCOMPARE(loaded["name"].toString(),QString("robot"));
        QCOMPARE(loaded["configuration"].toMap()["checks"],config["checks"]); QCOMPARE(loaded["configuration"].toMap()["messages"],config["messages"]);
        const auto duplicate=destination.importTemplate(QUrl::fromLocalFile(path)); QVERIFY(!duplicate.isEmpty()); QVERIFY(duplicate!=id);
        QCOMPARE(destination.loadTemplate(duplicate)["name"].toString(),QStringLiteral("robot（2）"));
        const auto localPath=destination.directory()+"/"+id+".json";
        QFile local(localPath); QVERIFY(local.open(QIODevice::ReadOnly)); auto object=QJsonDocument::fromJson(local.readAll()).object(); local.close();
        object["name"]=QStringLiteral("外部修改的名称"); QVERIFY(local.open(QIODevice::WriteOnly|QIODevice::Truncate)); local.write(QJsonDocument(object).toJson()); local.close();
        destination.refresh(); QCOMPARE(destination.loadTemplate(id)["name"].toString(),QStringLiteral("外部修改的名称"));
        QVERIFY(!destination.exportConfiguration("other",config,QUrl::fromLocalFile(localPath)));
        QCOMPARE(destination.loadTemplate(id)["name"].toString(),QStringLiteral("外部修改的名称"));
    }
    void invalidImportsAreRejected() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); ProtocolTemplateStore store(nullptr,temp.filePath("library"));
        const auto path=temp.filePath("bad.json");
        auto attempt=[&](const QByteArray &data) {
            QFile f(path); if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate)) return false;
            f.write(data); f.close(); return store.importTemplate(QUrl::fromLocalFile(path)).isEmpty();
        };
        QVERIFY(attempt("not json")); QVERIFY(!store.lastError().isEmpty());
        auto object=QJsonObject::fromVariantMap({{"format","uart-tool.binary-protocol"},{"version",2},{"id","valid-id"},{"name","bad"},{"configuration",BinaryConfig::preset("generic")}});
        QVERIFY(attempt(QJsonDocument(object).toJson()));
        object["version"]=1.5; QVERIFY(attempt(QJsonDocument(object).toJson()));
        object["version"]=1; object["id"]="../outside"; QVERIFY(attempt(QJsonDocument(object).toJson()));
        object["id"]="valid-id"; auto config=BinaryConfig::preset("generic"); config["header"]="A";
        object["configuration"]=QJsonObject::fromVariantMap(config); QVERIFY(attempt(QJsonDocument(object).toJson()));
        QVERIFY(attempt(QByteArray(2*1024*1024+1,' ')));
        QVERIFY(store.importTemplate(QUrl("https://example.invalid/protocol.json")).isEmpty());
        QCOMPARE(store.entries().size(),0);
        QVERIFY(!store.exportConfiguration("bad",BinaryConfig::preset("generic"),QUrl::fromLocalFile(temp.filePath("missing/file.json"))));
    }
};
QTEST_GUILESS_MAIN(ProtocolTemplateTests)
#include "protocol_template_tests.moc"
