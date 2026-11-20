#pragma once
#include "numericstream.h"
#include "plotviewport.h"
#include "interactiveplot.h"
#include <QPointer>
#include <QColor>

class TimeSeriesPlot : public InteractivePlot {
    Q_OBJECT
    Q_PROPERTY(NumericStream *stream READ stream WRITE setStream NOTIFY streamChanged)
    Q_PROPERTY(QStringList channels READ channels WRITE setChannels NOTIFY channelsChanged)
    Q_PROPERTY(int yLabelWidth READ yLabelWidth CONSTANT)
    Q_PROPERTY(double timeSpan READ timeSpan NOTIFY viewChanged)
    Q_PROPERTY(bool following READ following NOTIFY viewChanged)
    Q_PROPERTY(bool autoRange READ autoRange NOTIFY viewChanged)
    Q_PROPERTY(bool referenceEnabled READ referenceEnabled WRITE setReferenceEnabled NOTIFY referenceChanged)
    Q_PROPERTY(double referenceValue READ referenceValue WRITE setReferenceValue NOTIFY referenceChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY colorsChanged)
public:
    explicit TimeSeriesPlot(QQuickItem *parent=nullptr);
    NumericStream *stream() const { return m_stream; }
    void setStream(NumericStream *stream);
    QStringList channels() const { return m_channels; }
    void setChannels(const QStringList &channels);
    bool referenceEnabled() const { return m_referenceEnabled; }
    void setReferenceEnabled(bool enabled);
    double referenceValue() const { return m_referenceValue; }
    void setReferenceValue(double value);
    bool paused() const { return m_paused; }
    void setPaused(bool paused);
    QColor textColor() const { return m_text; }
    void setTextColor(const QColor &color);
    QColor gridColor() const { return m_grid; }
    void setGridColor(const QColor &color);
    Q_INVOKABLE QColor colorAt(int index) const;
    int yLabelWidth() const { return 54; }
    double timeSpan() const { return m_view.timeSpan(); }
    bool following() const { return m_view.following(); }
    bool autoRange() const { return m_view.autoRange(); }
    Q_INVOKABLE void resetView();
    void paint(QPainter *painter) override;
signals:
    void streamChanged(); void channelsChanged(); void pausedChanged(); void colorsChanged(); void viewChanged();
    void referenceChanged();
protected:
    void wheelEvent(QWheelEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
private:
    PlotViewport m_view;
    QRectF plotArea() const override;
    void panBy(const QPointF &fraction,bool x,bool y) override;
    void fitVisibleY();
    QPointer<NumericStream> m_stream;
    QStringList m_channels;
    QVector<QVector<QPointF>> m_points;
    QColor m_text=QColor("#8497aa"),m_grid=QColor("#d9e2eb");
    double m_end=0;
    bool m_paused=false;
    bool m_referenceEnabled=true;
    double m_referenceValue=0;
    void refresh();
};
