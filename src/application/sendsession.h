#pragma once
#include "core/bytesink.h"
#include <QObject>

// Input encoding and send status only. The selected transport owns its queue.
class SendSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool failed READ failed NOTIFY statusChanged)
public:
    static constexpr qsizetype MaxPayload=64*1024;
    explicit SendSession(ByteSink &sink,QObject *parent=nullptr):QObject(parent),m_sink(sink) {}
    bool connected() const { return m_sink.isWritable(); }
    QString statusText() const { return m_status; }
    bool failed() const { return m_failed; }
    Q_INVOKABLE bool send(const QString &input,bool hexMode,const QString &ending);
    void transportChanged();
signals:
    void connectionChanged(); void statusChanged();
    void bytesSubmitted(const QByteArray &bytes);
private:
    ByteSink &m_sink;
    QString m_status;
    bool m_failed=false;
    bool status(const QString &text,bool failed);
};
