#include "componentdragcontroller.h"
#include <QGuiApplication>
#include <QMouseEvent>
#include <QKeyEvent>

ComponentDragController::~ComponentDragController() { cancel(); }
void ComponentDragController::setTargetArea(QQuickItem *area) {
    if(m_target==area) return; m_target=area; emit targetAreaChanged();
}
void ComponentDragController::begin(QQuickWindow *source,const QString &type,const QPointF &position) {
    if(m_active || !source || type.isEmpty()) return;
    m_source=source; m_type=type; m_active=true;
    qGuiApp->installEventFilter(this); m_grab=source->setMouseGrabEnabled(true);
    move(position); emit activeChanged();
}
void ComponentDragController::move(const QPointF &position) {
    m_position=position; emit positionChanged(); bool inside=false;
    if(m_target && m_target->isVisible() && m_target->window() && m_target->window()->isVisible()) {
        const auto local=m_target->mapFromGlobal(position);
        inside=QRectF(0,0,m_target->width(),m_target->height()).contains(local);
    }
    if(inside!=m_hovering) { m_hovering=inside; emit hoveringChanged(); }
}
void ComponentDragController::cancel() {
    if(qGuiApp) qGuiApp->removeEventFilter(this);
    if(m_grab && m_source) m_source->setMouseGrabEnabled(false);
    m_grab=false; m_source=nullptr;
    if(m_active) { m_active=false; emit activeChanged(); }
    if(m_hovering) { m_hovering=false; emit hoveringChanged(); }
}
void ComponentDragController::finish(const QPointF &position) {
    if(!m_active) return; move(position); const bool accepted=m_hovering; const auto type=m_type;
    const auto local=m_target?m_target->mapFromGlobal(position):QPointF();
    cancel(); if(accepted) emit dropped(type,local);
}
bool ComponentDragController::eventFilter(QObject *watched,QEvent *event) {
    if(!m_active) return false;
    if(event->type()==QEvent::MouseMove) move(static_cast<QMouseEvent *>(event)->globalPosition());
    else if(event->type()==QEvent::MouseButtonRelease && static_cast<QMouseEvent *>(event)->button()==Qt::LeftButton)
        finish(static_cast<QMouseEvent *>(event)->globalPosition());
    else if(event->type()==QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key()==Qt::Key_Escape) { cancel(); return true; }
    else if(watched==m_source && (event->type()==QEvent::Close || event->type()==QEvent::Hide)) cancel();
    return false;
}
