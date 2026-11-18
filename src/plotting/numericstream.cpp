#include "numericstream.h"
#include <QSet>
#include <algorithm>
#include <cmath>

namespace {
bool numeric(const QVariant &value,double &number) {
    const int type=value.metaType().id();
    if(type!=QMetaType::Double && type!=QMetaType::Float && type!=QMetaType::Int && type!=QMetaType::UInt
        && type!=QMetaType::LongLong && type!=QMetaType::ULongLong) return false;
    number=value.toDouble(); return std::isfinite(number);
}
}
NumericStream::NumericStream(QObject *parent):QObject(parent) {
    m_clock.start(); m_publish.setInterval(50);
    connect(&m_publish,&QTimer::timeout,this,[this] {
        if(m_schemaDirty) { m_schemaDirty=false; emit channelsChanged(); }
        if(m_dirty) { m_dirty=false; emit samplesChanged(); }
    });
    m_publish.start();
}
bool NumericStream::hasChannel(const QString &key) const { return m_history.find(key)!=m_history.end(); }
void NumericStream::appendFrame(const DecodedFrame &frame) {
    const double now=m_clock.nsecsElapsed()/1e9; m_latestTime=now;
    ++m_frame;
    QVariantList channels = frame.sparseFields ? m_channels : QVariantList();
    QSet<QString> present;
    auto append=[&](const DecodedField &field,const QVariant &value,int index) {
        double number;
        if(!numeric(value,number)) return;
        const auto key=QString::number(field.name.size())+":"+field.name+":"+QString::number(index);
        if(present.contains(key)) return;
        if(!frame.sparseFields || !hasChannel(key)) {
            if(channels.size()>=MaxChannels) return;
            const auto name=index<0?field.name:field.name+"["+QString::number(index)+"]";
            channels.append(QVariantMap{{"key",key},{"name",name},{"type",field.type}});
        }
        present.insert(key);
        m_history[key].push_back({m_frame,now,number});
    };
    for(const auto &field:frame.fields) {
        if(!(field.type.startsWith("float") || field.type.startsWith("uint") || field.type.startsWith("int"))) continue;
        if(field.value.metaType().id()==QMetaType::QVariantList) {
            const auto values=field.value.toList();
            for(int i=0;i<values.size();++i) {
                if(!frame.sparseFields && channels.size()>=MaxChannels) break;
                append(field,values[i],i);
            }
        } else append(field,field.value,-1);
    }
    for(auto it=m_history.begin();it!=m_history.end();) {
        if(!frame.sparseFields && !present.contains(it->first)) {
            it=m_history.erase(it);
            continue;
        }
        auto &history=it->second;
        while(!history.empty() && (history.size()>MaxPoints || history.front().time<now-60)) history.pop_front();
        ++it;
    }
    if(channels!=m_channels) { m_channels=channels; m_schemaDirty=true; }
    m_dirty=true;
}
QVector<QPointF> NumericStream::points(const QString &key,double since,double until) const {
    QVector<QPointF> result; const auto found=m_history.find(key); if(found==m_history.end()) return result;
    const auto &history=found->second; bool previous=false; QPointF last;
    for(const auto &sample:history) {
        const QPointF point(sample.time,sample.value);
        if(point.x()>until) break;
        if(point.x()<since) { previous=true; last=point; continue; }
        if(previous) { result.append(last); previous=false; }
        result.append(point);
    }
    return result;
}
QVector<QPointF> NumericStream::xyPoints(const QString &xKey,const QString &yKey) const {
    const auto x=m_history.find(xKey), y=m_history.find(yKey);
    if(x==m_history.end() || y==m_history.end()) return {};

    QVector<QPointF> result;
    result.reserve(std::min(x->second.size(),y->second.size()));
    auto xSample=x->second.cbegin(), ySample=y->second.cbegin();
    while(xSample!=x->second.cend() && ySample!=y->second.cend()) {
        if(xSample->frame<ySample->frame) { ++xSample; continue; }
        if(ySample->frame<xSample->frame) { ++ySample; continue; }
        result.append(QPointF(xSample->value,ySample->value));
        ++xSample;
        ++ySample;
    }
    return result;
}
void NumericStream::clear() {
    m_frame=0;
    m_history.clear(); m_channels.clear(); m_clock.restart(); m_latestTime=0;
    m_dirty=false; m_schemaDirty=false; emit channelsChanged(); emit samplesChanged();
}
