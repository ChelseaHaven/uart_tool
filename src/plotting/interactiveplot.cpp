#include "interactiveplot.h"

#include <QCursor>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleHints>
#include <algorithm>

InteractivePlot::InteractivePlot(QQuickItem *parent):QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    m_hold.setSingleShot(true);
    m_hold.setInterval(250);
    connect(&m_hold,&QTimer::timeout,this,[this] { setFocusActive(true); });
}

void InteractivePlot::setFocusEnabled(bool enabled) {
    if (m_focusEnabled==enabled) return;
    m_focusEnabled=enabled;
    if (!enabled) {
        m_hold.stop();
        setFocusActive(false);
    }
    emit focusEnabledChanged();
}

void InteractivePlot::setFocusActive(bool active) {
    if (m_focusActive==active) return;
    m_focusActive=active;
    if (active) setCursor(QCursor(Qt::CrossCursor));
    emit focusActiveChanged();
    update();
}

void InteractivePlot::mousePressEvent(QMouseEvent *event) {
    emit plotPressed();
    const auto area=plotArea();
    m_lastPointer=event->position();
    const bool insidePlot=area.contains(m_lastPointer);
    m_panX=insidePlot || (m_lastPointer.y()>area.bottom()
        && m_lastPointer.x()>=area.left() && m_lastPointer.x()<=area.right());
    m_panY=insidePlot || (m_lastPointer.x()<area.left()
        && m_lastPointer.y()>=area.top() && m_lastPointer.y()<=area.bottom());
    const bool canHold=m_focusEnabled && insidePlot;
    if (canHold)
        m_hold.start();
    else if (m_panX || m_panY)
        setCursor(QCursor(Qt::ClosedHandCursor));
    else {
        event->ignore();
        return;
    }
    event->accept();
}

void InteractivePlot::mouseMoveEvent(QMouseEvent *event) {
    const auto delta=event->position()-m_lastPointer;
    if (m_focusActive) {
        m_lastPointer=event->position();
        update();
    } else if (m_panX || m_panY) {
        if (m_hold.isActive() && delta.manhattanLength()<QGuiApplication::styleHints()->startDragDistance()) {
            event->accept();
            return;
        }
        m_hold.stop();
        m_lastPointer=event->position();
        setCursor(QCursor(Qt::ClosedHandCursor));
        const auto area=plotArea();
        panBy({-delta.x()/area.width(),delta.y()/area.height()},m_panX,m_panY);
    } else {
        event->ignore();
        return;
    }
    event->accept();
}

void InteractivePlot::mouseReleaseEvent(QMouseEvent *event) {
    mouseUngrabEvent();
    event->accept();
}

void InteractivePlot::mouseUngrabEvent() {
    m_hold.stop();
    m_panX=m_panY=false;
    setFocusActive(false);
    unsetCursor();
}

void InteractivePlot::drawGrid(QPainter *painter,const QColor &textColor,const QColor &gridColor,
                               int yLabelWidth,const AxisLabel &xLabel,const AxisLabel &yLabel) const {
    const auto area=plotArea();
    const auto metrics=painter->fontMetrics();
    const int labelHeight=metrics.height()+3;
    int xLabelWidth=0;
    for (int i=0;i<=4;++i)
        xLabelWidth=std::max(xLabelWidth,metrics.horizontalAdvance(xLabel(i/4.0)));
    const int xSpacing=std::max(34,xLabelWidth+8);
    const int ySpacing=std::max(28,labelHeight+8);
    const auto divisions=[](double length,int spacing) {
        return std::clamp(int(length/spacing),1,12);
    };
    const int columns=divisions(area.width(),xSpacing);
    constexpr int defaultRows=5;
    constexpr double defaultGridHeight=264;
    const int extraRows=int(std::max(0.,area.height()-defaultGridHeight)/ySpacing);
    const int rows=area.height()<defaultRows*ySpacing ? 4 : std::min(12,defaultRows+extraRows);
    const QPen gridPen(gridColor,1);
    painter->save();
    for (int i=0;i<=columns;++i) {
        const double fraction=double(i)/columns;
        const double x=area.left()+fraction*area.width();
        painter->setPen(gridPen);
        painter->drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));
        const auto label=xLabel(fraction);
        const double labelWidth=metrics.horizontalAdvance(label)+4;
        const double left=std::clamp(x-labelWidth/2,0.,std::max(0.,width()-labelWidth));
        painter->setPen(textColor);
        painter->drawText(QRectF(left,area.bottom()+5,labelWidth,labelHeight),Qt::AlignCenter,label);
    }
    for (int i=0;i<=rows;++i) {
        const double fraction=double(i)/rows;
        const double y=area.bottom()-fraction*area.height();
        painter->setPen(gridPen);
        painter->drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
        painter->setPen(textColor);
        painter->drawText(QRectF(0,y-labelHeight/2.0,yLabelWidth,labelHeight),
                          Qt::AlignRight|Qt::AlignVCenter,yLabel(fraction));
    }
    painter->restore();
}

QPointF InteractivePlot::focusPosition() const {
    const auto area=plotArea();
    return {std::clamp(m_lastPointer.x(),area.left(),area.right()),
            std::clamp(m_lastPointer.y(),area.top(),area.bottom())};
}

void InteractivePlot::drawFocus(QPainter *painter,const QColor &color,const QColor &background,
                                const QString &x,const QString &y) const {
    const auto area=plotArea();
    const auto point=focusPosition();
    painter->save();
    painter->setClipRect(area.adjusted(-1,-1,1,1));
    painter->setPen(QPen(color,1.5));
    painter->drawLine(QPointF(area.left(),point.y()),QPointF(area.right(),point.y()));
    painter->drawLine(QPointF(point.x(),area.top()),QPointF(point.x(),area.bottom()));

    const QString label=QStringLiteral("X %1   Y %2").arg(x,y);
    const double width=std::min(area.width(),painter->fontMetrics().horizontalAdvance(label)+12.);
    const double height=painter->fontMetrics().height()+6.;
    const double left=std::clamp(point.x()+6,area.left(),area.right()-width);
    const double top=point.y()-area.top()>=height+6 ? point.y()-height-6 : point.y()+6;
    const QRectF box(left,top,width,height);
    painter->setBrush(background);
    painter->drawRoundedRect(box,3,3);
    painter->drawText(box,Qt::AlignCenter,label);
    painter->restore();
}
