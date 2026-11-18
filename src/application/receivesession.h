#pragma once
#include "core/bytesource.h"
#include "core/receivebuffer.h"
#include "core/byteformatter.h"
#include <QTimer>

// A transport-independent receive session consumed by the receive UI.
class ReceiveSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(qulonglong receivedBytes READ receivedBytes NOTIFY receivedBytesChanged)
    Q_PROPERTY(bool showTimestamps READ showTimestamps WRITE setShowTimestamps NOTIFY showTimestampsChanged)
    Q_PROPERTY(bool showRx READ showRx WRITE setShowRx NOTIFY directionsChanged)
    Q_PROPERTY(bool showTx READ showTx WRITE setShowTx NOTIFY directionsChanged)
    Q_PROPERTY(bool hexMode READ hexMode WRITE setHexMode NOTIFY hexModeChanged)
    Q_PROPERTY(bool displayPaused READ displayPaused WRITE setDisplayPaused NOTIFY displayPausedChanged)
    Q_PROPERTY(bool cacheLimited READ cacheLimited NOTIFY cacheLimitedChanged)
    Q_PROPERTY(QString displayText READ displayText NOTIFY displayTextChanged)
public:
    explicit ReceiveSession(ByteSource &source, QObject *parent = nullptr);
    qulonglong receivedBytes() const { return m_received; }
    bool showTimestamps() const { return m_options.timestamps; }
    bool showRx() const { return m_options.showRx; }
    bool showTx() const { return m_options.showTx; }
    bool hexMode() const { return m_options.hex; }
    bool displayPaused() const { return m_paused; }
    bool cacheLimited() const { return m_buffer.truncated() || m_displayLimited; }
    QString displayText() const { return m_published; }
    void setShowTimestamps(bool value);
    void setShowRx(bool value);
    void setShowTx(bool value);
    void recordTransmission(const QByteArray &bytes);
    void setHexMode(bool value);
    void setDisplayPaused(bool value);
    Q_INVOKABLE void clear();
signals:
    void receivedBytesChanged(); void hexModeChanged(); void displayPausedChanged();
    void showTimestampsChanged();
    void directionsChanged();
    void cacheLimitedChanged(); void displayTextChanged();
    void displayUpdate(const QString &text, int removeCharacters, bool reset);
    void rawBytesReceived(const QByteArray &bytes);
    void cleared();
private:
    QTimer m_displayTimer;
    ReceiveBuffer m_buffer;
    QString m_published;
    qulonglong m_received = 0, m_lastCount = 0;
    bool m_paused = false, m_dirty = false;
    bool m_forceReset = false, m_lastLimited = false;
    bool m_displayLimited = false;
    ByteFormatter::Options m_options;
    void appendLog(const QByteArray &bytes, ReceiveBuffer::Direction direction);
    void refreshFormat();
    void updateCacheLimit();
    void acceptBytes(const QByteArray &bytes);
    void flush();
    void publish(bool reset);
};
