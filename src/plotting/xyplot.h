#pragma once

#include "numericstream.h"
#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>

class XYPlot : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(NumericStream *stream READ stream WRITE setStream NOTIFY streamChanged)
    Q_PROPERTY(QString xChannel READ xChannel WRITE setXChannel NOTIFY channelsChanged)
    Q_PROPERTY(QString yChannel READ yChannel WRITE setYChannel NOTIFY channelsChanged)
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
protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
private:
    struct Axis {
        // Work in normalized units so even finite values near double limits fit.
        double scale=1;
        double low=-1;
        double high=1;
        bool automatic=true;
        void fit(double minimum,double maximum);
        void zoom(double factor,double anchor);
        void pan(double fraction);
        double fraction(double value) const;
        QString label(double fraction) const;
    };
    QRectF plotArea() const;
    void refresh();
    void fitAxes();
    void changeChannel(QString &channel,const QString &key);

    QPointer<NumericStream> m_stream;
    QString m_xKey,m_yKey;
    QVector<QPointF> m_points;
    Axis m_xAxis,m_yAxis;
    QColor m_text,m_grid,m_trace;
    QPointF m_lastPointer;
    bool m_paused=false,m_panX=false,m_panY=false;
};
