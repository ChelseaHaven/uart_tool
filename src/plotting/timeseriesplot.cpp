#include "timeseriesplot.h"
#include <QPainter>
#include <QPolygonF>
#include <QWheelEvent>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>
#include <limits>

TimeSeriesPlot::TimeSeriesPlot(QQuickItem *parent):InteractivePlot(parent) {
    setAntialiasing(true); setClip(true);
}
void TimeSeriesPlot::setStream(NumericStream *stream) {
    if(m_stream==stream) return;
    if(m_stream) disconnect(m_stream,nullptr,this,nullptr);
    m_stream=stream;
    if(stream) {
        connect(stream,&NumericStream::samplesChanged,this,[this] { if(!m_paused) refresh(); });
        connect(stream,&NumericStream::channelsChanged,this,[this] { if(!m_stream || m_stream->channels().isEmpty()) { m_points.clear(); m_view.reset(); fitVisibleY(); emit viewChanged(); update(); } });
        connect(stream,&QObject::destroyed,this,[this] { m_points.clear(); update(); });
    }
    refresh(); emit streamChanged();
}
void TimeSeriesPlot::setChannels(const QStringList &channels) {
    const auto limited=channels.sliced(0,std::min<qsizetype>(8,channels.size()));
    if(m_channels==limited) return;
    m_channels=limited; refresh(); emit channelsChanged();
}
void TimeSeriesPlot::setReferenceEnabled(bool enabled) {
    if (m_referenceEnabled == enabled) return;
    m_referenceEnabled = enabled;
    fitVisibleY();
    update();
    emit referenceChanged();
}
void TimeSeriesPlot::setReferenceValue(double value) {
    if (!std::isfinite(value) || m_referenceValue == value) return;
    m_referenceValue = value;
    fitVisibleY();
    update();
    emit referenceChanged();
}
void TimeSeriesPlot::setPaused(bool paused) {
    if(m_paused==paused) return;
    m_paused=paused; if(!paused) refresh(); emit pausedChanged();
}
void TimeSeriesPlot::setTextColor(const QColor &color) { if(color!=m_text) { m_text=color; update(); emit colorsChanged(); } }
void TimeSeriesPlot::setGridColor(const QColor &color) { if(color!=m_grid) { m_grid=color; update(); emit colorsChanged(); } }
QColor TimeSeriesPlot::colorAt(int index) const {
    static const QStringList dark{"#55cbd4","#efb864","#80aaff","#f08cac","#92cf83","#b8a0ee","#e9906f","#74c5a6"};
    static const QStringList light{"#078591","#b97913","#3d72d0","#ba507b","#538b38","#8257bc","#bd6338","#26806a"};
    const auto &palette=m_text.lightnessF()>0.55?dark:light;
    return QColor(palette[std::max(0,index)%palette.size()]);
}
void TimeSeriesPlot::refresh() {
    m_points.clear();
    if(m_stream) {
        m_end=m_stream->latestTime();
        for(const auto &key:m_channels) m_points.append(m_stream->points(key,m_end-60,m_end));
    }
    fitVisibleY(); update();
}
QRectF TimeSeriesPlot::plotArea() const { return {62,12,std::max(1.,width()-76),std::max(1.,height()-40)}; }
void TimeSeriesPlot::fitVisibleY() {
    const auto view=m_view.range(m_end);
    long double low = m_referenceEnabled ? m_referenceValue : std::numeric_limits<double>::max();
    long double high = m_referenceEnabled ? m_referenceValue : -std::numeric_limits<double>::max();
    bool have = m_referenceEnabled;
    for(const auto &series:m_points) for(const auto &point:series) if(point.x()>=view.left && point.x()<=view.right) {
        low=std::min(low,(long double)point.y()); high=std::max(high,(long double)point.y()); have=true;
    }
    m_view.fitY(have?low:-1,have?high:1);
}
void TimeSeriesPlot::resetView() { m_view.reset(); fitVisibleY(); emit viewChanged(); update(); }
void TimeSeriesPlot::wheelEvent(QWheelEvent *event) {
    const auto area=plotArea(); const auto pos=event->position();
    const bool x=pos.x()>=area.left(), y=pos.y()<=area.bottom();
    if(!x && !y) { event->ignore(); return; }
    const double steps=event->angleDelta().y()?event->angleDelta().y()/120.:event->pixelDelta().y()/40.;
    if(steps==0) { event->ignore(); return; }
    const double anchorX=std::clamp((pos.x()-area.left())/area.width(),0.,1.);
    const double anchorY=std::clamp((area.bottom()-pos.y())/area.height(),0.,1.);
    m_view.zoom(std::exp(-std::clamp(steps,-10.,10.)*0.18),x,y,anchorX,anchorY,m_end);
    fitVisibleY(); emit viewChanged(); update(); event->accept();
}
void TimeSeriesPlot::panBy(const QPointF &fraction,bool x,bool y) {
    m_view.pan(fraction.x(),fraction.y(),x,y,m_end);
    fitVisibleY(); emit viewChanged(); update();
}
void TimeSeriesPlot::mouseDoubleClickEvent(QMouseEvent *event) {
    resetView();
    event->accept();
}
void TimeSeriesPlot::paint(QPainter *painter) {
    const auto area=plotArea();
    if(width()<100 || height()<65) return;
    painter->setRenderHint(QPainter::Antialiasing); QFont label=painter->font(); label.setPixelSize(10); painter->setFont(label);
    const auto view=m_view.range(m_end);
    const auto low=view.low,high=view.high;
    auto textNumber=[](long double n) { const auto bound=(long double)std::numeric_limits<double>::max(); return QString::number(double(std::clamp(n,-bound,bound)),'g',4); };
    drawGrid(painter,m_text,m_grid,yLabelWidth(),
             [&](double fraction) {
                 return QString::number(view.left-m_end+(view.right-view.left)*fraction,'g',3)+QStringLiteral(" s");
             },
             [&](double fraction) { return textNumber(low+(high-low)*fraction); });
    painter->save(); painter->setClipRect(area.adjusted(-1,-1,1,1));
    if (m_referenceEnabled && m_referenceValue >= low && m_referenceValue <= high) {
        const double y = area.bottom() - double((m_referenceValue - low) / (high - low)) * area.height();
        painter->setPen(QPen(m_text, 1.5, Qt::DashLine));
        painter->drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }
    for(int index=0;index<m_points.size();++index) {
        QPolygonF curve; int bucket=-1; double minY=0,maxY=0,lastY=0,bucketX=0;
        auto flush=[&] { if(bucket<0) return; curve.append({bucketX,minY}); if(maxY!=minY) curve.append({bucketX,maxY}); if(lastY!=maxY) curve.append({bucketX,lastY}); };
        const auto &points=m_points[index];
        for(qsizetype i=0;i<points.size();++i) {
            const auto &point=points[i];
            if(point.x()<view.left && i+1<points.size() && points[i+1].x()<view.left) continue;
            if(i>0 && points[i-1].x()>view.right) break;
            const double x=area.left()+((point.x()-view.left)/(view.right-view.left))*area.width();
            const double y=area.bottom()-double(std::clamp(((long double)point.y()-low)/(high-low),-1e6L,1e6L))*area.height();
            const int pixel=int(std::clamp(x,-1e6,1e6));
            if(pixel!=bucket) { flush(); bucket=pixel; bucketX=x; minY=maxY=lastY=y; }
            else { minY=std::min(minY,y); maxY=std::max(maxY,y); lastY=y; }
        }
        flush(); painter->setPen(QPen(colorAt(index),1.7));
        if(curve.size()==1) painter->drawEllipse(curve.first(),2,2); else painter->drawPolyline(curve);
    }
    painter->restore();
    if (focusActive()) {
        const auto point=focusPosition();
        const double x=view.left-m_end+(view.right-view.left)*(point.x()-area.left())/area.width();
        const auto y=low+(high-low)*(area.bottom()-point.y())/area.height();
        drawFocus(painter,m_text,m_grid,QString::number(x,'g',6)+QStringLiteral(" s"),textNumber(y));
    }
}
