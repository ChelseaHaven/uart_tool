#include "protocolsession.h"
#include "protocols/keyvalueparser.h"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <cmath>
namespace {
QString scalarText(const QVariant &value) {
    if(value.metaType().id()==QMetaType::Double) return QString::number(value.toDouble(),'g',12);
    return value.toString();
}
QString preview(const QVariant &value,int count) {
    if(value.metaType().id()==QMetaType::QByteArray) {
        const auto bytes=value.toByteArray();
        return QString::fromLatin1(bytes.first(std::min<qsizetype>(128,bytes.size())).toHex(' ').toUpper())+(bytes.size()>128?QStringLiteral(" …（共 %1 字节）").arg(bytes.size()):QString());
    }
    if(value.metaType().id()==QMetaType::QVariantList) {
        QStringList values; const auto list=value.toList();
        for(qsizetype i=0;i<std::min<qsizetype>(64,list.size());++i) values.append(scalarText(list[i]));
        return "["+values.join(", ")+"]"+(count>64?QStringLiteral(" …（共 %1 项）").arg(count):QString());
    }
    return scalarText(value);
}
}
ProtocolSession::ProtocolSession(QObject *parent):QObject(parent) {
    for(const auto &p:m_catalog.all()) m_choices.append(QVariantMap{{"id",p.id},{"name",p.name},{"description",p.description}});
    QSettings store; const auto saved=store.value("protocols/binaryConfiguration").toByteArray();
    if(!saved.isEmpty()) {
        QJsonParseError error; const auto doc=QJsonDocument::fromJson(saved,&error);
        const auto configuration = doc.object().toVariantMap();
        if(error.error!=QJsonParseError::NoError || !doc.isObject() || !BinaryConfig::fromMap(configuration,m_binary,m_configError)) {
            m_configError=QStringLiteral("已保存的协议配置无效，已恢复默认值。")+m_configError;
        } else {
            m_binaryTemplateName = configuration.value("templateName").toString();
            if (m_binaryTemplateName.isEmpty()) m_binaryTemplateName = m_templates.nameForId(m_binary.templateId);
        }
    }
    rebuildBinaryChannels(true);
    m_publishTimer.setInterval(50);
    connect(&m_publishTimer,&QTimer::timeout,this,&ProtocolSession::publish); m_publishTimer.start();
}
QString ProtocolSession::selectedDescription() const { const auto *p=m_catalog.find(m_selected); return p?p->description:QString(); }
QVariantMap ProtocolSession::binaryConfiguration() const { return m_binary.toMap(); }
QVariantList ProtocolSession::binaryCommandGroups() const {
    QVariantList result;
    if(m_binary.commandMatches.isEmpty()) return result;
    int number=0;
    for(int i=0;i<m_binary.structure.size();++i) {
        const auto b=m_binary.structure[i].toMap(); if(b.value("kind")!="command") continue;
        QVariantList choices;
        for(const auto &value:b.value("messages").toList()) {
            auto m=value.toMap(); m["label"]=m.value("name").toString()+" · "+m.value("command").toString(); choices.append(m);
        }
        result.append(QVariantMap{{"blockIndex",i},{"name",QStringLiteral("命令字 %1").arg(++number)},{"choices",choices},{"value",b.value("value")}});
    }
    return result;
}
bool ProtocolSession::selectBinaryCommand(int blockIndex,const QString &value) {
    if(m_binary.commandMatches.isEmpty() || blockIndex<0 || blockIndex>=m_binary.structure.size()) return false;
    auto blocks=m_binary.structure; auto cmd=blocks[blockIndex].toMap();
    if(cmd.value("kind")!="command") return false;
    bool found=false;
    for(const auto &m:cmd.value("messages").toList()) if(m.toMap().value("command")==value) found=true;
    if(!found) return false;
    if(cmd.value("value")==value) return true;
    cmd["value"]=value; blocks[blockIndex]=cmd;
    for(int j=blockIndex+1;j<blocks.size();++j) {
        auto b=blocks[j].toMap(); if(b.value("kind")=="command") break;
        if(b.value("kind")!="payload") continue;
        const auto variants=b.value("variants").toMap(); if(!variants.contains(value)) return false;
        b["fields"]=variants.value(value); blocks[j]=b;
    }
    QString error; const auto configuration=FrameStructure::compile(blocks,m_binary.toMap(),error);
    if(configuration.isEmpty()) { m_configError=error; emit configurationErrorChanged(); return false; }
    // Selection is session state; template files are only changed by an explicit save.
    BinaryConfig candidate; if(!BinaryConfig::fromMap(configuration,candidate,error)) return false;
    candidate.templateId=m_binary.templateId; m_binary=candidate;
    rebuildBinaryChannels(true); emit configurationChanged();
    if(m_selected=="binary") reset();
    return true;
}
qulonglong ProtocolSession::frameCount() const {
    const auto *state=m_selected=="binary"?m_binaryChannels.get(m_selectedBinaryChannel):nullptr;
    return state?state->frames:m_frames;
}
qulonglong ProtocolSession::invalidFrames() const {
    const auto *state=m_selected=="binary"?m_binaryChannels.get(m_selectedBinaryChannel):nullptr;
    return state?state->parser->statistics().invalidFrames:m_parser?m_parser->statistics().invalidFrames:0;
}
qulonglong ProtocolSession::discardedBytes() const {
    const auto *state=m_selected=="binary"?m_binaryChannels.get(m_selectedBinaryChannel):nullptr;
    return state?state->parser->statistics().discardedBytes:m_parser?m_parser->statistics().discardedBytes:0;
}
QString ProtocolSession::lastError() const {
    const auto *state=m_selected=="binary"?m_binaryChannels.get(m_selectedBinaryChannel):nullptr;
    return state?state->parser->statistics().lastError:m_parser?m_parser->statistics().lastError:QString();
}
void ProtocolSession::rebuildBinaryChannels(bool selectCurrent) {
    QVector<BinaryChannelDefinition> definitions;
    const auto config=m_binary.toMap();
    if(m_binary.messages.isEmpty()) {
        const auto id=m_binary.commandOffset>=0?"0x"+QString::number(m_binary.commandValue,16).toUpper():QString("current");
        const auto name = m_binary.commandOffset>=0 ? QStringLiteral("命令 %1").arg(id) : QStringLiteral("当前数据");
        definitions.append({id,name,m_binary});
    } else for(const auto &value:config.value("messages").toList()) {
        const auto message=value.toMap(); auto chosen=config; chosen["commandValue"]=message.value("command");
        BinaryConfig channel; QString error;
        if(!BinaryConfig::fromMap(BinaryConfig::singleCommandConfiguration(chosen),channel,error)) continue;
        const auto id="0x"+QString::number(channel.commandValue,16).toUpper();
        definitions.append({id,message.value("name").toString(),channel});
    }
    m_binaryChannels.configure(definitions);
    const auto preferred="0x"+QString::number(m_binary.commandValue,16).toUpper();
    if(selectCurrent || !m_binaryChannels.get(m_selectedBinaryChannel)) {
        m_selectedBinaryChannel=m_binaryChannels.get(preferred)?preferred:definitions.isEmpty()?QString():definitions.first().id;
        m_channelVersion=0; emit binaryChannelChanged();
    }
    emit binaryChannelsChanged();
}
void ProtocolSession::syncChannel(bool force) {
    const auto *state=m_binaryChannels.get(m_selectedBinaryChannel); if(!state) return;
    if(force || state->frames!=m_channelVersion) {
        m_channelVersion=state->frames; m_latest=state->latest; m_lastTime=state->time; m_frameDirty=true; m_dirty=true;
    }
}
bool ProtocolSession::selectBinaryChannel(const QString &id) {
    const auto *state=m_binaryChannels.get(id); if(!state) return false;
    if(id==m_selectedBinaryChannel) return true;
    m_selectedBinaryChannel=id; m_binary.commandValue=state->definition.configuration.commandValue;
    m_binaryChannels.reset(); m_channelVersion=0;
    for(auto &value:m_binary.structure) {
        auto block=value.toMap();
        if(block.value("kind")=="command") { block["value"]="0x"+QString::number(m_binary.commandValue,16).toUpper(); value=block; }
    }
    emit binaryChannelChanged(); emit configurationChanged();
    if(m_selected=="binary") { syncChannel(true); publishFrame(); publish(); emit sessionReset(); }
    return true;
}
bool ProtocolSession::selectProtocol(const QString &id) {
    if(!m_catalog.find(id)) return false;
    if(m_selected==id) return true;
    m_selected=id; emit selectionChanged(); reset(); return true;
}
bool ProtocolSession::applyBinaryConfiguration(const QVariantMap &configuration) {
    BinaryConfig candidate; QString error;
    if(!BinaryConfig::fromMap(configuration,candidate,error)) { m_configError=error; emit configurationErrorChanged(); return false; }
    const auto name = m_templates.nameForId(candidate.templateId);
    auto saved = candidate.toMap();
    saved["templateName"] = name;
    QSettings store;
    store.setValue("protocols/binaryConfiguration",QJsonDocument(QJsonObject::fromVariantMap(saved)).toJson(QJsonDocument::Compact));
    store.sync();
    if(store.status()!=QSettings::NoError) { m_configError=QStringLiteral("无法保存配置，请检查当前用户配置目录的写权限"); emit configurationErrorChanged(); return false; }
    m_binary=candidate; m_binaryTemplateName=name; rebuildBinaryChannels(true); m_configError.clear(); emit configurationErrorChanged(); emit configurationChanged();
    if(m_selected=="binary") reset();
    return true;
}
void ProtocolSession::feedBytes(const QByteArray &bytes) {
    if(bytes.isEmpty()) return;
    if(m_selected=="binary") { m_binaryChannels.feed(m_selectedBinaryChannel,bytes,[this](const DecodedFrame &frame) { emit frameDecoded(frame); }); syncChannel(); m_dirty=true; return; }
    if(!m_parser) return;
    m_parser->feed(bytes,[this](DecodedFrame frame) {
        emit frameDecoded(frame);
        storeLatestFrame(std::move(frame)); ++m_frames;
        m_lastTime=QDateTime::currentDateTime().toString("HH:mm:ss.zzz"); m_frameDirty=true;
    });
    m_dirty=true;
}
void ProtocolSession::storeLatestFrame(DecodedFrame frame) {
    if (!frame.sparseFields) {
        m_latest = std::move(frame);
        return;
    }

    for (auto &field : frame.fields) {
        const auto existing = m_dictionaryFieldIndices.constFind(field.name);
        if (existing != m_dictionaryFieldIndices.cend()) {
            m_latest.fields[*existing] = std::move(field);
        } else if (m_latest.fields.size() < MaxDictionaryFields) {
            m_dictionaryFieldIndices.insert(field.name, m_latest.fields.size());
            m_latest.fields.append(std::move(field));
        }
    }
}
void ProtocolSession::publish() {
    if(!m_dirty) return;
    m_dirty=false;
    if(m_frameDirty && !m_displayPaused) {
        publishFrame();
    }
    const QString status=m_selected=="none"?QStringLiteral("未启用协议解析"):
        frameCount()?QStringLiteral("已解析 %1 帧").arg(frameCount()):QStringLiteral("等待有效数据帧");
    if(m_status!=status) { m_status=status; emit statusChanged(); }
    emit statisticsChanged();
}
void ProtocolSession::publishFrame() {
        emit latestFrameAboutToChange();
        m_latestRows.clear();
        if(!m_latest.messageName.isEmpty()) m_latestRows.append(QVariantMap{{"name",QStringLiteral("消息类型")},{"type","message"},{"value",m_latest.messageName},{"valueText",m_latest.messageName},{"byteCount",0},{"elementCount",1}});
        for(const auto &field:m_latest.fields) m_latestRows.append(QVariantMap{{"name",field.name},{"type",field.type},
            {"value",field.value},{"valueText",preview(field.value,field.elementCount)},
            {"byteCount",field.byteCount},{"elementCount",field.elementCount}});
        m_frameDirty=false; emit latestFrameChanged();
 }
void ProtocolSession::setDisplayPaused(bool paused) {
    if(m_displayPaused==paused) return;
    m_displayPaused=paused; emit displayPausedChanged();
    if(!paused) { m_dirty=true; publish(); }
}
void ProtocolSession::reset() {
    emit latestFrameAboutToChange();
    m_parser=m_selected=="binary"?nullptr:makeProtocolParser(m_selected,m_binary); m_frames=0;
    m_binaryChannels.reset(); m_channelVersion=0;
    m_latest={}; m_dictionaryFieldIndices.clear(); m_latestRows.clear(); m_lastTime.clear(); m_dirty=true; m_frameDirty=false;
    publish(); emit latestFrameChanged(); emit sessionReset();
}
