#pragma once

#include <QQuickPaintedItem>
#include <QTimer>
#include <functional>

// Shared press, pan and crosshair behavior for the two plot types.
class InteractivePlot : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(bool focusEnabled READ focusEnabled WRITE setFocusEnabled NOTIFY focusEnabledChanged)
    Q_PROPERTY(bool focusActive READ focusActive NOTIFY focusActiveChanged)
public:
    explicit InteractivePlot(QQuickItem *parent=nullptr);
    bool focusEnabled() const { return m_focusEnabled; }
    void setFocusEnabled(bool enabled);
    bool focusActive() const { return m_focusActive; }

signals:
    void plotPressed();
    void focusEnabledChanged();
    void focusActiveChanged();

protected:
    virtual QRectF plotArea() const = 0;
    virtual void panBy(const QPointF &fraction, bool x, bool y) = 0;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    using AxisLabel = std::function<QString(double)>;
    void drawGrid(QPainter *painter, const QColor &textColor, const QColor &gridColor, int yLabelWidth,
                  const AxisLabel &xLabel, const AxisLabel &yLabel) const;
    QPointF focusPosition() const;
    void drawFocus(QPainter *painter, const QColor &color, const QColor &background,
                   const QString &x, const QString &y) const;

private:
    void setFocusActive(bool active);
    QTimer m_hold;
    QPointF m_lastPointer;
    bool m_panX=false,m_panY=false;
    bool m_focusEnabled=true,m_focusActive=false;
};
