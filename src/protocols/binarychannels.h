#pragma once
#include "protocolparser.h"
#include <map>

struct BinaryChannelDefinition {
    QString id,name;
    BinaryConfig configuration;
};
struct BinaryChannelState {
    BinaryChannelDefinition definition;
    std::unique_ptr<ProtocolParser> parser;
    DecodedFrame latest;
    QString time;
    quint64 frames=0;
};
// Command definitions remain selectable, but only the selected parser receives
// input. Inactive commands do not update results in the background.
class BinaryChannels {
public:
    void configure(const QVector<BinaryChannelDefinition> &definitions);
    void feed(const QString &selectedId,const QByteArray &bytes,const std::function<void(const DecodedFrame &)> &observer={});
    void reset();
    const BinaryChannelState *get(const QString &id) const;
    QVariantList choices() const;
private:
    std::map<QString,BinaryChannelState> m_channels;
    QStringList m_order;
};
