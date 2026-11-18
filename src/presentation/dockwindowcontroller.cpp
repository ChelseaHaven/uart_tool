#include "dockwindowcontroller.h"
#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QTimer>
#include <algorithm>
#include <array>

DockWindowController::DockWindowController(QObject *parent):QObject(parent) {}
DockWindowController::~DockWindowController() { stopTracking(); }
void DockWindowController::setFloatingWindow(QQuickWindow *window) {
    if(m_window==window) return;
    stopTracking();
    m_window=window;
    emit floatingWindowChanged();
}
void DockWindowController::setDockArea(QQuickItem *area) {
    if(m_area==area) return;
    m_area=area; setFeedback({},false); emit dockAreaChanged();
}
void DockWindowController::setDockPanel(QQuickItem *panel) {
    if(m_panel==panel) return;
    m_panel=panel; emit dockPanelChanged();
}
void DockWindowController::setDockPanels(const QVariantList &panels) {
    if(m_panels==panels) return;
    m_panels=panels; setFeedback({},false); emit dockPanelsChanged();
}
void DockWindowController::setEnabled(bool enabled) {
    if(m_enabled==enabled) return;
    m_enabled=enabled;
    if(!enabled) stopTracking();
    emit enabledChanged();
}
void DockWindowController::setDragging(bool dragging) {
    if(m_dragging==dragging) return;
    m_dragging=dragging; emit draggingChanged();
}
void DockWindowController::setFeedback(const QString &edge,bool guides,QQuickItem *target) {
    const auto bounds=boundsFor(target);
    const auto preview=placementFor(edge,target);
    if(bounds!=m_guideRect || preview!=m_previewRect) {
        m_guideRect=bounds; m_previewRect=preview; emit feedbackRectChanged();
    }
    if(m_target!=target) { m_target=target; emit dockTargetChanged(); }
    if(m_edge!=edge) { m_edge=edge; emit dockEdgeChanged(); }
    if(m_guides!=guides) { m_guides=guides; emit guidesVisibleChanged(); }
}
QQuickItem *DockWindowController::targetAt(const QPointF &position) const {
    if(!m_enabled || !m_area || !m_area->isVisible() || !m_area->window()
        || !m_area->window()->isVisible() || m_area->window()->visibility()==QWindow::Minimized) return nullptr;
    const QRectF workspace(m_area->mapToGlobal(QPointF()),QSizeF(m_area->width(),m_area->height()));
    if(!workspace.adjusted(-24,-24,24,24).contains(position)) return nullptr;
    // The outer rim selects the whole workspace; the interior selects the
    // docked panel under the pointer. Floating panels do not reserve any space.
    if(m_panel && m_panel->property("fixedSidebar").toBool()) return m_area;
    if(!workspace.adjusted(24,24,-24,-24).contains(position)) return m_area;
    for(const auto &entry:m_panels) {
        auto *panel=qobject_cast<QQuickItem *>(entry.value<QObject *>());
        if(!panel || panel->property("fixedSidebar").toBool() || panel->property("floating").toBool() || !panel->isVisible()
            || panel->window()!=m_area->window()) continue;
        const auto local=panel->mapFromGlobal(position);
        if(QRectF(0,0,panel->width(),panel->height()).contains(local)) return panel;
    }
    return m_area;
}
QString DockWindowController::edgeAt(const QPointF &position,bool &near,QQuickItem *&target) const {
    target=targetAt(position); near=false;
    if(!target) return {};
    const auto area=boundsFor(target);
    const std::array<qreal,4> distances{std::abs(position.x()-area.left()),std::abs(position.y()-area.top()),
        std::abs(position.x()-area.right()),std::abs(position.y()-area.bottom())};
    const auto closest=std::min_element(distances.begin(),distances.end());
    const auto index=size_t(closest-distances.begin());
    const bool verticalEdge=index==0 || index==2;
    if(m_panel && m_panel->property("fixedSidebar").toBool() && !verticalEdge) return {};
    const qreal normalRange=std::min(qreal(40),std::min(area.width(),area.height())/3);
    const qreal midpointRange=std::min(qreal(32),(verticalEdge?area.height():area.width())/4);
    const qreal alongEdge=verticalEdge?std::abs(position.y()-area.center().y()):std::abs(position.x()-area.center().x());
    // A small gate around each edge midpoint, rather than the whole edge.
    if(*closest>normalRange || alongEdge>midpointRange) return {};
    near=true;
    const std::array<QString,4> edges{QStringLiteral("left"),QStringLiteral("top"),QStringLiteral("right"),QStringLiteral("bottom")};
    return edges[index];
}
QRectF DockWindowController::boundsFor(QQuickItem *target) const {
    if(!target) return {};
    QRectF area(target->mapToGlobal(QPointF()),QSizeF(target->width(),target->height()));
    if(target!=m_area || (m_panel && m_panel->property("fixedSidebar").toBool())) return area;
    // Ordinary panels share the content area; a pinned sidebar stays outside it.
    for(const auto &entry:m_panels) {
        const auto *panel=qobject_cast<QQuickItem *>(entry.value<QObject *>());
        if(!panel || !panel->property("fixedSidebar").toBool() || panel->property("floating").toBool()) continue;
        const QRectF sidebar(panel->mapToGlobal(QPointF()),QSizeF(panel->width(),panel->height()));
        const qreal gap=panel->property("sidebarDividerWidth").toReal();
        if(panel->property("sidebarEdge").toString()==QStringLiteral("right")) area.setRight(sidebar.left()-gap);
        else area.setLeft(sidebar.right()+gap);
    }
    return area;
}
QRectF DockWindowController::placementFor(const QString &edge,QQuickItem *target) const {
    if(edge.isEmpty() || !target) return {};
    const auto area=boundsFor(target);
    const bool horizontal=edge==QStringLiteral("left") || edge==QStringLiteral("right");
    if(m_panel && m_panel->property("fixedSidebar").toBool()) {
        const qreal width=m_panel->property("sidebarWidth").toReal();
        return {edge==QStringLiteral("right")?area.right()-width:area.left(),area.top(),width,area.height()};
    }
    const qreal available=std::max(qreal(0),(horizontal?area.width():area.height())-12);
    const bool others=std::any_of(m_panels.begin(),m_panels.end(),[this](const QVariant &entry) {
        const auto *panel=qobject_cast<QQuickItem *>(entry.value<QObject *>());
        return panel && panel!=m_panel && !panel->property("fixedSidebar").toBool() && !panel->property("floating").toBool();
    });
    if(!others) return area;
    qreal extent=available/2;
    if(horizontal) {
        const qreal ownMinimum=m_panel?m_panel->property("minimumDockWidth").toReal():0;
        qreal peerMinimum=0;
        for(const auto &entry:m_panels) {
            const auto *panel=qobject_cast<QQuickItem *>(entry.value<QObject *>());
            if(panel && panel!=m_panel && !panel->property("fixedSidebar").toBool() && !panel->property("floating").toBool())
                peerMinimum=std::max(peerMinimum,panel->property("minimumDockWidth").toReal());
        }
        extent=std::clamp(extent,std::min(ownMinimum,available),std::max(std::min(ownMinimum,available),available-peerMinimum));
    }
    if(horizontal) return {edge==QStringLiteral("right")?area.right()-extent:area.left(),area.top(),extent,area.height()};
    return {area.left(),edge==QStringLiteral("bottom")?area.bottom()-extent:area.top(),area.width(),extent};
}
void DockWindowController::stopTracking() {
    if(qGuiApp) qGuiApp->removeEventFilter(this);
    setDragging(false);
    if(m_mouseGrab && m_sourceWindow) m_sourceWindow->setMouseGrabEnabled(false);
    m_sourceWindow=nullptr;
    m_mouseGrab=false; setFeedback({},false);
}
void DockWindowController::moveTo(const QPointF &position) {
    if(m_window) {
        m_window->setPosition((position-m_offset).toPoint());
    }
}
void DockWindowController::beginDrag(const QPointF &position,const QPointF &offset,QQuickWindow *sourceWindow) {
    if(!m_enabled || !m_window || !sourceWindow) return;
    // Keep the press on its original window. A native system move cannot safely
    // inherit a press from a different window after an Item was reparented.
    m_offset=offset; m_sourceWindow=sourceWindow;
    setDragging(true); qGuiApp->installEventFilter(this);
    m_mouseGrab=sourceWindow->setMouseGrabEnabled(true);
    m_window->raise(); updateDrag(position);
}
void DockWindowController::updateDrag(const QPointF &position) {
    if(!m_enabled) return;
    setDragging(true);
    bool near=false; QQuickItem *target=nullptr;
    const auto edge=edgeAt(position,near,target); setFeedback(edge,near,target);
}
void DockWindowController::endDrag(const QPointF &position) {
    if(!m_dragging) return;
    bool near=false; QQuickItem *target=nullptr; const auto edge=edgeAt(position,near,target);
    const QPointer<QQuickItem> panelTarget=target==m_area?nullptr:target;
    const auto placement=placementFor(edge,target);
    stopTracking();
    if(!edge.isEmpty()) emit dockRequested(edge,panelTarget,placement);
    else {
        // Let the source window finish processing the release before activating
        // the new window, otherwise the desktop can restack it underneath.
        const QPointer<QQuickWindow> window=m_window;
        QTimer::singleShot(0,this,[window] { if(window && window->isVisible()) { window->requestActivate(); } });
    }
}
bool DockWindowController::eventFilter(QObject *watched,QEvent *event) {
    if(!m_enabled) return false;
    if(m_dragging && (watched==m_sourceWindow || watched==m_window)
        && (event->type()==QEvent::Hide || event->type()==QEvent::Close)) { stopTracking(); return false; }
    // Quick forwards pointer events from the window to its MouseArea. Process
    // the original window event once; moving again during forwarding shifts
    // coordinates underneath the same event and can invalidate the preview.
    if(m_dragging && watched==m_sourceWindow && event->type()==QEvent::MouseMove) {
        const auto *mouse=static_cast<QMouseEvent *>(event);
        moveTo(mouse->globalPosition());
        updateDrag(mouse->globalPosition());
    } else if(m_dragging && watched==m_sourceWindow && (event->type()==QEvent::MouseButtonRelease || event->type()==QEvent::NonClientAreaMouseButtonRelease)) {
        const auto *mouse=static_cast<QMouseEvent *>(event);
        if(mouse->button()==Qt::LeftButton) endDrag(mouse->globalPosition());
    }
    return false;
}
