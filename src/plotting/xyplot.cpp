#include "xyplot.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

void XYPlot::Axis::fit(double minimum,double maximum,bool pad) {
    if(!automatic) return;
    scale=std::max({std::abs(minimum),std::abs(maximum),std::numeric_limits<double>::min()});
    if(minimum==maximum && scale<1) scale=1;
    low=minimum/scale;
    high=maximum/scale;
    if (!pad) return;
    const double padding=high==low?0.1:std::max((high-low)*0.08,1e-12);
    low-=padding;
    high+=padding;
}
void XYPlot::Axis::zoom(double factor,double anchor) {
    const double span=high-low;
    const double center=low+anchor*span;
    const double next=std::clamp(span*factor,1e-9,1e6);
    low=center-anchor*next;
    high=low+next;
    automatic=false;
}
void XYPlot::Axis::pan(double fraction) {
    if(fraction==0) return;
    const double delta=fraction*(high-low);
    low+=delta;
    high+=delta;
    automatic=false;
}
double XYPlot::Axis::fraction(double value) const {
    return std::clamp((value/scale-low)/(high-low),-1e6,1e6);
}
QString XYPlot::Axis::label(double fraction) const {
    const double limit=std::numeric_limits<double>::max();
    const double value=std::clamp((low+(high-low)*fraction)*scale,-limit,limit);
    if(std::abs(value)<1e-12*scale) return QStringLiteral("0");
    return QString::number(value,'g',4);
}

XYPlot::XYPlot(QQuickItem *parent):InteractivePlot(parent) {
    setAntialiasing(true);
    setClip(true);
    connect(this,&XYPlot::colorsChanged,this,[this] { update(); });
}
void XYPlot::setStream(NumericStream *stream) {
    if(m_stream==stream) return;
    if(m_stream) disconnect(m_stream,nullptr,this,nullptr);
    m_stream=stream;
    if(stream) {
        connect(stream,&NumericStream::samplesChanged,this,[this] { if(!m_paused) refresh(); });
        connect(stream,&NumericStream::channelsChanged,this,[this] {
            if(!m_stream->hasChannel(m_xKey) || !m_stream->hasChannel(m_yKey)) refresh();
        });
        connect(stream,&QObject::destroyed,this,[this] { m_stream=nullptr; refresh(); });
    }
    refresh();
    emit streamChanged();
}
void XYPlot::changeChannel(QString &channel,const QString &key) {
    if(channel==key) return;
    channel=key;
    m_xAxis={};
    m_yAxis={};
    refresh();
    emit channelsChanged();
}
void XYPlot::setXChannel(const QString &key) { changeChannel(m_xKey,key); }
void XYPlot::setYChannel(const QString &key) { changeChannel(m_yKey,key); }
void XYPlot::setPaused(bool paused) {
    if(m_paused==paused) return;
    m_paused=paused;
    if(!paused) refresh();
    emit pausedChanged();
}
void XYPlot::setXReferenceEnabled(bool enabled) { setReference(m_xReference,enabled,m_xReference.value); }
void XYPlot::setYReferenceEnabled(bool enabled) { setReference(m_yReference,enabled,m_yReference.value); }
void XYPlot::setXReferenceValue(double value) { setReference(m_xReference,m_xReference.enabled,value); }
void XYPlot::setYReferenceValue(double value) { setReference(m_yReference,m_yReference.enabled,value); }

void XYPlot::setReference(Reference &reference,bool enabled,double value) {
    if (!std::isfinite(value) || (reference.enabled==enabled && reference.value==value)) return;
    reference={enabled,value};
    fitAxes();
    update();
    emit referencesChanged();
}

void XYPlot::refresh() {
    const auto oldCount=m_points.size();
    m_points=m_stream && !m_xKey.isEmpty() && !m_yKey.isEmpty()?m_stream->xyPoints(m_xKey,m_yKey):QVector<QPointF>();
    fitAxes();
    update();
    if(oldCount!=m_points.size()) emit pointCountChanged();
}
void XYPlot::fitAxes() {
    if(!m_xAxis.automatic && !m_yAxis.automatic) return;
    const bool hasPoints=!m_points.isEmpty();
    double minX=hasPoints ? m_points.first().x() : -1;
    double maxX=hasPoints ? minX : 1;
    double minY=hasPoints ? m_points.first().y() : -1;
    double maxY=hasPoints ? minY : 1;
    for(const auto &point:m_points) {
        minX=std::min(minX,point.x()); maxX=std::max(maxX,point.x());
        minY=std::min(minY,point.y()); maxY=std::max(maxY,point.y());
    }
    if (m_xReference.enabled) {
        minX=std::min(minX,m_xReference.value);
        maxX=std::max(maxX,m_xReference.value);
    }
    if (m_yReference.enabled) {
        minY=std::min(minY,m_yReference.value);
        maxY=std::max(maxY,m_yReference.value);
    }
    m_xAxis.fit(minX,maxX,hasPoints || minX < -1 || maxX > 1);
    m_yAxis.fit(minY,maxY,hasPoints || minY < -1 || maxY > 1);
}
void XYPlot::resetView() {
    m_xAxis={};
    m_yAxis={};
    fitAxes();
    update();
}
QRectF XYPlot::plotArea() const {
    return {64,12,std::max(1.,width()-92),std::max(1.,height()-40)};
}
void XYPlot::wheelEvent(QWheelEvent *event) {
    const auto area=plotArea();
    const auto position=event->position();
    const bool zoomX=position.x()>=area.left();
    const bool zoomY=position.y()<=area.bottom();
    const double steps=event->angleDelta().y()?event->angleDelta().y()/120.:event->pixelDelta().y()/40.;
    if((!zoomX && !zoomY) || steps==0) { event->ignore(); return; }
    const double factor=std::exp(-std::clamp(steps,-10.,10.)*0.18);
    if(zoomX) m_xAxis.zoom(factor,std::clamp((position.x()-area.left())/area.width(),0.,1.));
    if(zoomY) m_yAxis.zoom(factor,std::clamp((area.bottom()-position.y())/area.height(),0.,1.));
    update();
    event->accept();
}
void XYPlot::panBy(const QPointF &fraction,bool x,bool y) {
    if (x) m_xAxis.pan(fraction.x());
    if (y) m_yAxis.pan(fraction.y());
    update();
}
void XYPlot::mouseDoubleClickEvent(QMouseEvent *event) {
    resetView();
    event->accept();
}

void XYPlot::paint(QPainter *painter) {
    if(width()<100 || height()<55) return;
    const auto area=plotArea();
    QFont labels=painter->font();
    labels.setPixelSize(10);
    painter->setFont(labels);
    drawGrid(painter,m_text,m_grid,58,
             [this](double fraction) { return m_xAxis.label(fraction); },
             [this](double fraction) { return m_yAxis.label(fraction); });

    QPolygonF trace;
    trace.reserve(m_points.size());
    for(const auto &point:m_points)
        trace.append(QPointF(area.left()+m_xAxis.fraction(point.x())*area.width(),
                             area.bottom()-m_yAxis.fraction(point.y())*area.height()));
    painter->save();
    painter->setClipRect(area.adjusted(-1,-1,1,1));
    painter->setPen(QPen(m_text,1.5,Qt::DashLine));
    if (m_xReference.enabled) {
        const auto fraction=m_xAxis.fraction(m_xReference.value);
        if (fraction>=0 && fraction<=1) {
            const double x=area.left()+fraction*area.width();
            painter->drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));
        }
    }
    if (m_yReference.enabled) {
        const auto fraction=m_yAxis.fraction(m_yReference.value);
        if (fraction>=0 && fraction<=1) {
            const double y=area.bottom()-fraction*area.height();
            painter->drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
        }
    }
    if (!trace.isEmpty()) {
        painter->setPen(QPen(m_trace,1.6));
        if (trace.size()>1) painter->drawPolyline(trace);
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_trace);
        painter->drawEllipse(trace.constLast(),3.5,3.5);
    }
    painter->restore();
    if (focusActive()) {
        const auto point=focusPosition();
        drawFocus(painter,m_text,m_grid,m_xAxis.label((point.x()-area.left())/area.width()),
                  m_yAxis.label((area.bottom()-point.y())/area.height()));
    }
}
