#pragma once

#include "numericstream.h"
#include <QColor>
#include <QPointer>
#include "interactiveplot.h"

class XYPlot : public InteractivePlot {
    Q_OBJECT
    Q_PROPERTY(NumericStream *stream READ stream WRITE setStream NOTIFY streamChanged)
    Q_PROPERTY(QString xChannel READ xChannel WRITE setXChannel NOTIFY channelsChanged)
    Q_PROPERTY(QString yChannel READ yChannel WRITE setYChannel NOTIFY channelsChanged)
    Q_PROPERTY(bool xReferenceEnabled READ xReferenceEnabled WRITE setXReferenceEnabled NOTIFY referencesChanged)
    Q_PROPERTY(double xReferenceValue READ xReferenceValue WRITE setXReferenceValue NOTIFY referencesChanged)
    Q_PROPERTY(bool yReferenceEnabled READ yReferenceEnabled WRITE setYReferenceEnabled NOTIFY referencesChanged)
    Q_PROPERTY(double yReferenceValue READ yReferenceValue WRITE setYReferenceValue NOTIFY referencesChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(qsizetype pointCount READ pointCount NOTIFY pointCountChanged)
    Q_PROPERTY(QColor textColor MEMBER m_text NOTIFY colorsChanged)
    Q_PROPERTY(QColor gridColor MEMBER m_grid NOTIFY colorsChanged)
    Q_PROPERTY(QColor traceColor MEMBER m_trace NOTIFY colorsChanged)
public:
    explicit XYPlot(QQuickItem *parent=nullptr);
    NumericStream *stream() const { return m_stream; }
    void setStream(NumericStream *stream);
    QString xChannel() const { return m_xKey; }
    QString yChannel() const { return m_yKey; }
    void setXChannel(const QString &key);
    void setYChannel(const QString &key);
    bool xReferenceEnabled() const { return m_xReference.enabled; }
    double xReferenceValue() const { return m_xReference.value; }
    bool yReferenceEnabled() const { return m_yReference.enabled; }
    double yReferenceValue() const { return m_yReference.value; }
    void setXReferenceEnabled(bool enabled);
    void setXReferenceValue(double value);
    void setYReferenceEnabled(bool enabled);
    void setYReferenceValue(double value);
    bool paused() const { return m_paused; }
    void setPaused(bool paused);
    qsizetype pointCount() const { return m_points.size(); }
    Q_INVOKABLE void resetView();
    void paint(QPainter *painter) override;
signals:
    void streamChanged();
    void channelsChanged();
    void pausedChanged();
    void pointCountChanged();
    void colorsChanged();
    void referencesChanged();
protected:
    void wheelEvent(QWheelEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
private:
    struct Axis {
        // Work in normalized units so even finite values near double limits fit.
        double scale=1;
        double low=-1;
        double high=1;
        bool automatic=true;
        void fit(double minimum,double maximum,bool pad);
        void zoom(double factor,double anchor);
        void pan(double fraction);
        double fraction(double value) const;
        QString label(double fraction) const;
    };
    QRectF plotArea() const override;
    void panBy(const QPointF &fraction,bool x,bool y) override;
    struct Reference { bool enabled=true; double value=0; };
    void setReference(Reference &reference,bool enabled,double value);
    Reference m_xReference,m_yReference;
    void refresh();
    void fitAxes();
    void changeChannel(QString &channel,const QString &key);

    QPointer<NumericStream> m_stream;
    QString m_xKey,m_yKey;
    QVector<QPointF> m_points;
    Axis m_xAxis,m_yAxis;
    QColor m_text,m_grid,m_trace;
    bool m_paused=false;
};
