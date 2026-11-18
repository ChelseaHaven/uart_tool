#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>

// In-application component dragging across the existing dock/floating windows.
class ComponentDragController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickItem *targetArea READ targetArea WRITE setTargetArea NOTIFY targetAreaChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool hovering READ hovering NOTIFY hoveringChanged)
    Q_PROPERTY(QPointF globalPosition READ globalPosition NOTIFY positionChanged)
public:
    explicit ComponentDragController(QObject *parent=nullptr):QObject(parent) {}
    ~ComponentDragController() override;
    QQuickItem *targetArea() const { return m_target; }
    void setTargetArea(QQuickItem *area);
    bool active() const { return m_active; }
    bool hovering() const { return m_hovering; }
    QPointF globalPosition() const { return m_position; }
    Q_INVOKABLE void begin(QQuickWindow *source,const QString &type,const QPointF &position);
    Q_INVOKABLE void finish(const QPointF &position);
    Q_INVOKABLE void cancel();
signals:
    void targetAreaChanged(); void activeChanged(); void hoveringChanged(); void positionChanged();
    void dropped(const QString &type,const QPointF &targetPosition);
protected:
    bool eventFilter(QObject *watched,QEvent *event) override;
private:
    QPointer<QQuickItem> m_target;
    QPointer<QQuickWindow> m_source;
    QPointF m_position;
    QString m_type;
    bool m_active=false,m_hovering=false,m_grab=false;
    void move(const QPointF &position);
};
