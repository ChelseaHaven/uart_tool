#pragma once
#include "numericstream.h"
#include "plotviewport.h"
#include <QQuickPaintedItem>
#include <QPointer>
#include <QColor>

class TimeSeriesPlot : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(NumericStream *stream READ stream WRITE setStream NOTIFY streamChanged)
    Q_PROPERTY(QStringList channels READ channels WRITE setChannels NOTIFY channelsChanged)
    Q_PROPERTY(double timeSpan READ timeSpan NOTIFY viewChanged)
    Q_PROPERTY(bool following READ following NOTIFY viewChanged)
    Q_PROPERTY(bool autoRange READ autoRange NOTIFY viewChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY colorsChanged)
public:
    explicit TimeSeriesPlot(QQuickItem *parent=nullptr);
    NumericStream *stream() const { return m_stream; }
    void setStream(NumericStream *stream);
    QStringList channels() const { return m_channels; }
    void setChannels(const QStringList &channels);
    bool paused() const { return m_paused; }
    void setPaused(bool paused);
    QColor textColor() const { return m_text; }
    void setTextColor(const QColor &color);
    QColor gridColor() const { return m_grid; }
    void setGridColor(const QColor &color);
    Q_INVOKABLE QColor colorAt(int index) const;
    double timeSpan() const { return m_view.timeSpan(); }
    bool following() const { return m_view.following(); }
    bool autoRange() const { return m_view.autoRange(); }
    Q_INVOKABLE void resetView();
    void paint(QPainter *painter) override;
signals:
    void streamChanged(); void channelsChanged(); void pausedChanged(); void colorsChanged(); void viewChanged();
protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
private:
    PlotViewport m_view;
    QPointF m_lastPointer;
    bool m_panX=false,m_panY=false;
    QRectF plotArea() const;
    void fitVisibleY();
    QPointer<NumericStream> m_stream;
    QStringList m_channels;
    QVector<QVector<QPointF>> m_points;
    QColor m_text=QColor("#8497aa"),m_grid=QColor("#d9e2eb");
    double m_end=0;
    bool m_paused=false;
    void refresh();
};
