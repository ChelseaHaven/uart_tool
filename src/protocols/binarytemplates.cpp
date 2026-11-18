#include "binaryconfig.h"
#include "framestructure.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QResource>

void initializeProtocolTemplates() {
    static const bool initialized=[] { Q_INIT_RESOURCE(protocol_templates); return true; }();
    Q_UNUSED(initialized)
}
QVariantMap BinaryConfig::preset(const QString &id) {
    initializeProtocolTemplates();
    if(!QStringList{"simple_fixed","simple_variable","robot_tennis","generic"}.contains(id)) return {};
    QFile file(":/protocol-templates/"+id+".json");
    if(!file.open(QIODevice::ReadOnly)) return {};
    const auto map=QJsonDocument::fromJson(file.readAll()).object().value("configuration").toObject().toVariantMap();
    BinaryConfig config; QString error;
    if(!BinaryConfig::fromMap(map,config,error)) return {};
    if(!config.structure.isEmpty()) return FrameStructure::compile(config.structure,config.toMap(),error);
    return config.toMap();
}
