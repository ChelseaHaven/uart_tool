#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVariantList>

// Drag tracking and docking feedback only; application sessions stay untouched.
class DockWindowController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickWindow *floatingWindow READ floatingWindow WRITE setFloatingWindow NOTIFY floatingWindowChanged)
    Q_PROPERTY(QQuickItem *dockArea READ dockArea WRITE setDockArea NOTIFY dockAreaChanged)
    Q_PROPERTY(QQuickItem *dockPanel READ dockPanel WRITE setDockPanel NOTIFY dockPanelChanged)
    Q_PROPERTY(QRectF guideRect READ guideRect NOTIFY feedbackRectChanged)
    Q_PROPERTY(QRectF previewRect READ previewRect NOTIFY feedbackRectChanged)
    Q_PROPERTY(QVariantList dockPanels READ dockPanels WRITE setDockPanels NOTIFY dockPanelsChanged)
    Q_PROPERTY(QQuickItem *dockTarget READ dockTarget NOTIFY dockTargetChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool dockReady READ dockReady NOTIFY dockEdgeChanged)
    Q_PROPERTY(QString dockEdge READ dockEdge NOTIFY dockEdgeChanged)
    Q_PROPERTY(bool dragging READ dragging NOTIFY draggingChanged)
    Q_PROPERTY(bool guidesVisible READ guidesVisible NOTIFY guidesVisibleChanged)
public:
    explicit DockWindowController(QObject *parent=nullptr);
    ~DockWindowController() override;
    QQuickWindow *floatingWindow() const { return m_window; }
    void setFloatingWindow(QQuickWindow *window);
    QQuickItem *dockArea() const { return m_area; }
    void setDockArea(QQuickItem *area);
    QQuickItem *dockPanel() const { return m_panel; }
    void setDockPanel(QQuickItem *panel);
    QRectF guideRect() const { return m_guideRect; }
    QRectF previewRect() const { return m_previewRect; }
    QVariantList dockPanels() const { return m_panels; }
    void setDockPanels(const QVariantList &panels);
    QQuickItem *dockTarget() const { return m_target; }
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    bool dockReady() const { return !m_edge.isEmpty(); }
    QString dockEdge() const { return m_edge; }
    bool dragging() const { return m_dragging; }
    bool guidesVisible() const { return m_guides; }
    Q_INVOKABLE void beginDrag(const QPointF &globalPosition,const QPointF &windowOffset,QQuickWindow *sourceWindow);
    Q_INVOKABLE void updateDrag(const QPointF &globalPosition);
    Q_INVOKABLE void endDrag(const QPointF &globalPosition);
signals:
    void floatingWindowChanged(); void dockAreaChanged(); void enabledChanged(); void dockPanelsChanged(); void dockTargetChanged(); void dockPanelChanged(); void feedbackRectChanged();
    void dockEdgeChanged(); void draggingChanged(); void guidesVisibleChanged();
    void dockRequested(const QString &edge,QQuickItem *targetPanel,const QRectF &placement);
protected:
    bool eventFilter(QObject *watched,QEvent *event) override;
private:
    QPointer<QQuickWindow> m_window;
    QPointer<QQuickItem> m_area,m_target,m_panel;
    QRectF m_guideRect,m_previewRect;
    QVariantList m_panels;
    QPointer<QQuickWindow> m_sourceWindow;
    QPointF m_offset;
    QString m_edge;
    bool m_enabled=false,m_dragging=false,m_guides=false,m_mouseGrab=false;
    QString edgeAt(const QPointF &globalPosition,bool &near,QQuickItem *&target) const;
    QQuickItem *targetAt(const QPointF &globalPosition) const;
    QRectF boundsFor(QQuickItem *target) const;
    QRectF placementFor(const QString &edge,QQuickItem *target) const;
    void setFeedback(const QString &edge,bool guides,QQuickItem *target=nullptr);
    void setDragging(bool dragging);
    void stopTracking();
    void moveTo(const QPointF &position);
};
