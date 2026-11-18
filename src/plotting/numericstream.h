#pragma once
#include "protocols/protocolparser.h"
#include <QObject>
#include <QElapsedTimer>
#include <QPointF>
#include <QTimer>
#include <QVariantList>
#include <deque>
#include <map>

// Parsed numeric channels and bounded history, independent of views/transports.
class NumericStream final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList channels READ channels NOTIFY channelsChanged)
public:
    static constexpr int MaxChannels=128;
    static constexpr int MaxPoints=8192;
    explicit NumericStream(QObject *parent=nullptr);
    QVariantList channels() const { return m_channels; }
    double latestTime() const { return m_latestTime; }
    QVector<QPointF> points(const QString &key,double since,double until) const;
    QVector<QPointF> xyPoints(const QString &xKey,const QString &yKey) const;
    Q_INVOKABLE bool hasChannel(const QString &key) const;
    void appendFrame(const DecodedFrame &frame);
    void clear();
signals:
    void channelsChanged();
    void samplesChanged();
private:
    QVariantList m_channels;
    struct Sample {
        quint64 frame;
        double time;
        double value;
    };
    std::map<QString,std::deque<Sample>> m_history;
    quint64 m_frame=0;
    QElapsedTimer m_clock;
    QTimer m_publish;
    double m_latestTime=0;
    bool m_dirty=false,m_schemaDirty=false;
};
