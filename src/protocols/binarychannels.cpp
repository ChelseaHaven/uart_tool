#include "binarychannels.h"
#include <QDateTime>

void BinaryChannels::configure(const QVector<BinaryChannelDefinition> &definitions) {
    std::map<QString,BinaryChannelState> next; m_order.clear();
    for(const auto &definition:definitions) {
        if(next.count(definition.id)) continue;
        BinaryChannelState state;
        const auto old=m_channels.find(definition.id);
        if(old!=m_channels.end() && old->second.definition.configuration.toMap()==definition.configuration.toMap())
            state=std::move(old->second);
        else state.parser=makeProtocolParser("binary",definition.configuration);
        state.definition=definition; next.emplace(definition.id,std::move(state)); m_order.append(definition.id);
    }
    m_channels=std::move(next);
}
void BinaryChannels::feed(const QString &selectedId,const QByteArray &bytes,const std::function<void(const DecodedFrame &)> &observer) {
    const auto found=m_channels.find(selectedId); if(found==m_channels.end()) return;
    auto &state=found->second;
    state.parser->feed(bytes,[&state,&observer](DecodedFrame frame) {
        if(observer) observer(frame);
        state.latest=std::move(frame); state.time=QDateTime::currentDateTime().toString("HH:mm:ss.zzz"); ++state.frames;
    });
}
void BinaryChannels::reset() {
    for(auto &[id,state]:m_channels) {
        Q_UNUSED(id)
        state.parser=makeProtocolParser("binary",state.definition.configuration);
        state.latest={}; state.time.clear(); state.frames=0;
    }
}
const BinaryChannelState *BinaryChannels::get(const QString &id) const {
    const auto found=m_channels.find(id); return found==m_channels.end()?nullptr:&found->second;
}
QVariantList BinaryChannels::choices() const {
    QVariantList result;
    for(const auto &id:m_order) {
        const auto &d=m_channels.at(id).definition;
        const auto command=d.configuration.commandOffset>=0?"0x"+QString::number(d.configuration.commandValue,16).toUpper():QString();
        result.append(QVariantMap{{"id",id},{"name",d.name},{"command",command},{"label",command.isEmpty()?d.name:d.name+"  ·  "+command}});
    }
    return result;
}
