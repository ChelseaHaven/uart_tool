#include "protocoltemplatestore.h"
#include "binaryconfig.h"
#include "framestructure.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>

void initializeProtocolTemplates();
namespace {
constexpr qint64 MaxFileSize=2*1024*1024;
bool validId(const QString &id) { return QRegularExpression("^[A-Za-z0-9_-]{1,80}$").match(id).hasMatch(); }
}
ProtocolTemplateStore::ProtocolTemplateStore(QObject *parent,const QString &directory):QObject(parent),
    m_directory(directory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/protocol-templates":directory) {
    initializeProtocolTemplates();
    if(!QDir().mkpath(m_directory)) { setError(QStringLiteral("无法创建模板目录：%1").arg(m_directory)); return; }
    refresh();
}
QVariantList ProtocolTemplateStore::builtInEntries() const {
    QVariantList result;
    for(const auto &file:QDir(":/protocol-templates").entryList({"*.json"},QDir::Files)) {
        if(file=="generic.json" || file=="robot_tennis.json") continue;
        QString error; const auto record=read(":/protocol-templates/"+file,error);
        if(!record.isEmpty()) result.append(QVariantMap{{"id","builtin:"+record.value("id").toString()},{"name",record.value("name")}});
    }
    return result;
}
QString ProtocolTemplateStore::nameForId(const QString &id) const {
    if (id.isEmpty() || id == QLatin1String("custom")) return {};
    for (const auto &entry : m_entries) {
        const auto item = entry.toMap();
        if (item.value("id").toString() == id) return item.value("name").toString();
    }
    const auto builtinId = id.startsWith(QLatin1String("builtin:")) ? id : QStringLiteral("builtin:") + id;
    for (const auto &entry : builtInEntries()) {
        const auto item = entry.toMap();
        if (item.value("id").toString() == builtinId) return item.value("name").toString();
    }
    return {};
}
void ProtocolTemplateStore::setError(const QString &error) { if(m_error!=error) { m_error=error; emit errorChanged(); } }
QVariantMap ProtocolTemplateStore::normalize(const QVariantMap &configuration,QString &error) const {
    BinaryConfig parsed;
    if(!BinaryConfig::fromMap(configuration,parsed,error)) return {};
    const auto map=parsed.toMap();
    if(!parsed.structure.isEmpty()) return FrameStructure::compile(parsed.structure,map,error);
    return map;
}
QVariantMap ProtocolTemplateStore::read(const QString &path,QString &error) const {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) { error=QStringLiteral("无法读取模板：%1").arg(file.errorString()); return {}; }
    if(file.size()>MaxFileSize) { error=QStringLiteral("模板文件不能超过 2 MiB"); return {}; }
    QJsonParseError parse; const auto doc=QJsonDocument::fromJson(file.readAll(),&parse);
    if(parse.error!=QJsonParseError::NoError || !doc.isObject()) { error=QStringLiteral("模板不是有效的 JSON 对象：%1").arg(parse.errorString()); return {}; }
    const auto object=doc.object();
    if(object.value("format").toString()!="uart-tool.binary-protocol" || object.value("version").toDouble()!=1.0) {
        error=QStringLiteral("不支持的模板格式或版本"); return {};
    }
    const QString id=object.value("id").toString(),name=object.value("name").toString().trimmed();
    if(!validId(id) || name.isEmpty() || name.size()>80) { error=QStringLiteral("模板标识或名称无效"); return {}; }
    auto config=normalize(object.value("configuration").toObject().toVariantMap(),error);
    if(config.isEmpty()) return {};
    config["templateId"]=id;
    return {{"id",id},{"name",name},{"configuration",config}};
}
QString ProtocolTemplateStore::pathFor(const QString &id) const {
    for(const auto &entry:m_entries) if(entry.toMap()["id"].toString()==id) return entry.toMap()["filePath"].toString();
    return {};
}
void ProtocolTemplateStore::refresh() {
    QVariantList entries; QStringList problems;
    for(const auto &file:QDir(m_directory).entryInfoList({"*.json"},QDir::Files,QDir::Name)) {
        QString error; const auto record=read(file.absoluteFilePath(),error);
        if(record.isEmpty()) { problems.append(file.fileName()+"："+error); continue; }
        QString builtInError;
        const auto original=read(":/protocol-templates/"+record["id"].toString()+".json",builtInError);
        if(record["id"]!="robot_tennis" && !original.isEmpty() && original["configuration"]==record["configuration"] && original["name"]==record["name"]) continue;
        bool duplicate=false;
        for(const auto &entry:entries) if(entry.toMap()["id"]==record["id"]) duplicate=true;
        if(duplicate) { problems.append(file.fileName()+QStringLiteral("：模板标识重复")); continue; }
        entries.append(QVariantMap{{"id",record["id"]},{"name",record["name"]},{"filePath",file.absoluteFilePath()}});
    }
    std::sort(entries.begin(),entries.end(),[](const QVariant &a,const QVariant &b) {
        return QString::localeAwareCompare(a.toMap()["name"].toString(),b.toMap()["name"].toString())<0;
    });
    m_entries=entries; emit entriesChanged(); setError(problems.join('\n'));
}
QVariantMap ProtocolTemplateStore::loadTemplate(const QString &id) {
    if(id.startsWith("builtin:")) {
        const auto key=id.sliced(8);
        if(!QStringList{"simple_fixed","simple_variable","multi_segment"}.contains(key)) { setError(QStringLiteral("内置模板不存在")); return {}; }
        QString error; auto record=read(":/protocol-templates/"+key+".json",error);
        if(!record.isEmpty()) { auto config=record.value("configuration").toMap(); config["templateId"]=id; record["configuration"]=config; record["id"]=id; }
        setError(error); return record;
    }
    const auto path=pathFor(id); QString error;
    if(path.isEmpty()) { setError(QStringLiteral("模板不存在，请刷新列表")); return {}; }
    const auto record=read(path,error); setError(error); return record;
}
bool ProtocolTemplateStore::validName(const QString &name,const QString &exceptId) {
    if(name.isEmpty() || name.size()>80 || name.contains(QChar('\n')) || name.contains(QChar('\r'))) { setError(QStringLiteral("模板名称需为 1～80 个字符，不能包含换行")); return false; }
    for(const auto &entry:m_entries) {
        const auto record=entry.toMap();
        if(record["id"].toString()!=exceptId && record["name"].toString().compare(name,Qt::CaseInsensitive)==0) {
            setError(QStringLiteral("已存在同名模板，请换一个名称")); return false;
        }
    }
    return true;
}
bool ProtocolTemplateStore::write(const QString &path,const QString &id,const QString &name,const QVariantMap &configuration) {
    if(QFileInfo(path).isSymLink()) { setError(QStringLiteral("模板目标为符号链接，请选择普通文件")); return false; }
    const auto data=QJsonDocument(QJsonObject::fromVariantMap({{"format","uart-tool.binary-protocol"},{"version",1},{"id",id},{"name",name},{"configuration",configuration}})).toJson(QJsonDocument::Indented);
    if(data.size()>MaxFileSize) { setError(QStringLiteral("模板文件不能超过 2 MiB")); return false; }
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(data)!=data.size() || !file.commit()) {
        setError(QStringLiteral("无法保存模板：%1").arg(file.errorString())); return false;
    }
    return true;
}
QString ProtocolTemplateStore::saveTemplate(const QString &input,const QVariantMap &configuration,const QString &existingId) {
    setError({}); const auto name=input.trimmed();
    if(existingId.startsWith("builtin:")) { setError(QStringLiteral("内置模板只读，请保存为我的模板")); return {}; }
    if(!validName(name,existingId)) return {};
    QString error; auto config=normalize(configuration,error);
    if(config.isEmpty()) { setError(error); return {}; }
    const auto id=existingId.isEmpty()?QUuid::createUuid().toString(QUuid::WithoutBraces):existingId;
    config["templateId"]=id;
    QString path=existingId.isEmpty()?QDir(m_directory).filePath(id+".json"):pathFor(id);
    if(path.isEmpty()) { setError(QStringLiteral("待更新的模板不存在，请另存为新模板")); return {}; }
    if(!write(path,id,name,config)) return {};
    refresh(); return id;
}
QString ProtocolTemplateStore::importTemplate(const QUrl &url) {
    if(!url.isLocalFile()) { setError(QStringLiteral("请选择本地 JSON 文件")); return {}; }
    QString error; const auto record=read(url.toLocalFile(),error);
    if(record.isEmpty()) { setError(error); return {}; }
    QString name=record["name"].toString(); const auto base=name; int suffix=2;
    auto exists=[&] { for(const auto &e:m_entries) if(e.toMap()["name"].toString().compare(name,Qt::CaseInsensitive)==0) return true; return false; };
    while(exists()) { const auto tail=QStringLiteral("（%1）").arg(suffix++); name=base.left(80-tail.size())+tail; }
    // Always create a new local identity; imports never overwrite an existing template.
    return saveTemplate(name,record["configuration"].toMap());
}
bool ProtocolTemplateStore::exportConfiguration(const QString &input,const QVariantMap &configuration,const QUrl &url) {
    setError({}); const auto name=input.trimmed();
    if(!url.isLocalFile() || name.isEmpty() || name.size()>80) { setError(QStringLiteral("请填写模板名称并选择本地导出位置")); return false; }
    const auto destination=QFileInfo(url.toLocalFile()).absoluteFilePath();
    // The library is managed by Save, so an export cannot replace another template's identity.
    if(QFileInfo(destination).absolutePath()==QFileInfo(m_directory).absoluteFilePath()) {
        setError(QStringLiteral("导出请选择模板库以外的位置；更新库内模板请使用保存模板")); return false;
    }
    QString error; const auto config=normalize(configuration,error);
    if(config.isEmpty()) { setError(error); return false; }
    return write(destination,QUuid::createUuid().toString(QUuid::WithoutBraces),name,config);
}
bool ProtocolTemplateStore::deleteTemplate(const QString &id) {
    if(id.startsWith("builtin:")) { setError(QStringLiteral("内置模板不可删除")); return false; }
    const auto path=pathFor(id);
    if(path.isEmpty()) { setError(QStringLiteral("模板不存在")); return false; }
    if(!QFile::remove(path)) { setError(QStringLiteral("删除失败，请检查模板文件权限")); return false; }
    refresh(); return true;
}
