#pragma once

#include <QObject>
#include <QTimer>

// One timer per send panel, independent of the serial connection and payload.
class SendTimer : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
public:
    explicit SendTimer(QObject *parent=nullptr):QObject(parent) {
        m_timer.setTimerType(Qt::PreciseTimer);
        connect(&m_timer,&QTimer::timeout,this,&SendTimer::tick);
    }
    bool running() const { return m_timer.isActive(); }
    Q_INVOKABLE void start(int intervalMs) {
        if(running() || intervalMs<=0) return;
        m_timer.start(intervalMs);
        emit runningChanged();
    }
    Q_INVOKABLE void stop() {
        if(!running()) return;
        m_timer.stop();
        emit runningChanged();
    }
signals:
    void tick();
    void runningChanged();
private:
    QTimer m_timer;
};
