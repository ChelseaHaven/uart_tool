#include "xyplot.h"
#include <QCursor>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

void XYPlot::Axis::fit(double minimum,double maximum) {
    if(!automatic) return;
    scale=std::max({std::abs(minimum),std::abs(maximum),std::numeric_limits<double>::min()});
    if(minimum==maximum && scale<1) scale=1;
    low=minimum/scale;
    high=maximum/scale;
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

XYPlot::XYPlot(QQuickItem *parent):QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setClip(true);
    setAcceptedMouseButtons(Qt::LeftButton);
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
void XYPlot::refresh() {
    const auto oldCount=m_points.size();
    m_points=m_stream && !m_xKey.isEmpty() && !m_yKey.isEmpty()?m_stream->xyPoints(m_xKey,m_yKey):QVector<QPointF>();
    fitAxes();
    update();
    if(oldCount!=m_points.size()) emit pointCountChanged();
}
void XYPlot::fitAxes() {
    if(!m_xAxis.automatic && !m_yAxis.automatic) return;
    if(m_points.isEmpty()) {
        m_xAxis.fit(-1,1);
        m_yAxis.fit(-1,1);
        return;
    }
    double minX=m_points.first().x(), maxX=minX;
    double minY=m_points.first().y(), maxY=minY;
    for(const auto &point:m_points) {
        minX=std::min(minX,point.x()); maxX=std::max(maxX,point.x());
        minY=std::min(minY,point.y()); maxY=std::max(maxY,point.y());
    }
    m_xAxis.fit(minX,maxX);
    m_yAxis.fit(minY,maxY);
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
    if(m_points.isEmpty() || (!zoomX && !zoomY) || steps==0) { event->ignore(); return; }
    const double factor=std::exp(-std::clamp(steps,-10.,10.)*0.18);
    if(zoomX) m_xAxis.zoom(factor,std::clamp((position.x()-area.left())/area.width(),0.,1.));
    if(zoomY) m_yAxis.zoom(factor,std::clamp((area.bottom()-position.y())/area.height(),0.,1.));
    update();
    event->accept();
}
void XYPlot::mousePressEvent(QMouseEvent *event) {
    const auto area=plotArea();
    m_lastPointer=event->position();
    m_panX=!m_points.isEmpty() && m_lastPointer.x()>=area.left();
    m_panY=!m_points.isEmpty() && m_lastPointer.y()<=area.bottom();
    if(!m_panX && !m_panY) { event->ignore(); return; }
    setCursor(QCursor(Qt::ClosedHandCursor));
    event->accept();
}
void XYPlot::mouseMoveEvent(QMouseEvent *event) {
    if(!m_panX && !m_panY) { event->ignore(); return; }
    const auto area=plotArea();
    const auto delta=event->position()-m_lastPointer;
    m_lastPointer=event->position();
    if(m_panX) m_xAxis.pan(-delta.x()/area.width());
    if(m_panY) m_yAxis.pan(delta.y()/area.height());
    update();
    event->accept();
}
void XYPlot::mouseUngrabEvent() {
    m_panX=m_panY=false;
    unsetCursor();
}
void XYPlot::mouseReleaseEvent(QMouseEvent *event) {
    mouseUngrabEvent();
    event->accept();
}
void XYPlot::mouseDoubleClickEvent(QMouseEvent *event) { resetView(); event->accept(); }

void XYPlot::paint(QPainter *painter) {
    if(width()<100 || height()<55 || m_points.isEmpty()) return;
    const auto area=plotArea();
    QFont labels=painter->font();
    labels.setPixelSize(10);
    painter->setFont(labels);
    const int columns=std::clamp(int(area.width()/70),2,8);
    for(int i=0;i<=columns;++i) {
        const double t=double(i)/columns;
        const double x=area.left()+t*area.width();
        painter->setPen(m_grid);
        painter->drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));
        painter->setPen(m_text);
        painter->drawText(QRectF(x-29,area.bottom()+5,58,17),Qt::AlignCenter,m_xAxis.label(t));
    }
    const int rows=std::clamp(int(area.height()/40),2,6);
    for(int i=0;i<=rows;++i) {
        const double t=double(i)/rows;
        const double y=area.bottom()-t*area.height();
        painter->setPen(m_grid);
        painter->drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
        painter->setPen(m_text);
        painter->drawText(QRectF(0,y-8,58,16),Qt::AlignRight|Qt::AlignVCenter,m_yAxis.label(t));
    }

    QPolygonF trace;
    trace.reserve(m_points.size());
    for(const auto &point:m_points)
        trace.append(QPointF(area.left()+m_xAxis.fraction(point.x())*area.width(),
                             area.bottom()-m_yAxis.fraction(point.y())*area.height()));
    painter->save();
    painter->setClipRect(area.adjusted(-1,-1,1,1));
    painter->setPen(QPen(m_trace,1.6));
    if(trace.size()>1) painter->drawPolyline(trace);
    painter->setPen(Qt::NoPen);
    painter->setBrush(m_trace);
    painter->drawEllipse(trace.constLast(),3.5,3.5);
    painter->restore();
}
