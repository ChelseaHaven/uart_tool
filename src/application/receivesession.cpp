#include "receivesession.h"
#include <QVector>
#include <chrono>

namespace {
// Longest suffix of the previous view that is a prefix of the next view.
// This lets QML keep its text document and selection as the ring discards old bytes.
qsizetype retainedPrefix(const QString &previous, const QString &next) {
    if (next.startsWith(previous)) return previous.size();
    if (next.isEmpty()) return 0;
    QVector<qsizetype> prefix(next.size());
    for (qsizetype i = 1, j = 0; i < next.size(); ++i) {
        while (j && next[i] != next[j]) j = prefix[j-1];
        if (next[i] == next[j]) ++j;
        prefix[i] = j;
    }
    qsizetype matched = 0;
    for (const QChar c : previous) {
        while (matched && (matched == next.size() || c != next[matched])) matched = prefix[matched-1];
        if (matched < next.size() && c == next[matched]) ++matched;
    }
    return matched;
}
}
ReceiveSession::ReceiveSession(ByteSource &source, QObject *parent) : QObject(parent) {
    connect(&source, &ByteSource::bytesReceived, this, &ReceiveSession::acceptBytes);
    connect(&source, &ByteSource::opened, this, [this] { clear(); setDisplayPaused(false); });
    connect(&source, &ByteSource::closed, this, &ReceiveSession::flush);
    m_displayTimer.setInterval(50);
    connect(&m_displayTimer, &QTimer::timeout, this, &ReceiveSession::flush);
    m_displayTimer.start();
}
void ReceiveSession::acceptBytes(const QByteArray &bytes) {
    if (bytes.isEmpty()) return;
    appendLog(bytes, ReceiveBuffer::Direction::Rx);
    m_received += static_cast<qulonglong>(bytes.size());
    emit rawBytesReceived(bytes);
}
void ReceiveSession::recordTransmission(const QByteArray &bytes) {
    if (!bytes.isEmpty()) appendLog(bytes, ReceiveBuffer::Direction::Tx);
}
void ReceiveSession::appendLog(const QByteArray &bytes, ReceiveBuffer::Direction direction) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto timestampUs = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    m_buffer.append(bytes, timestampUs, direction);
    m_dirty = true;
}
void ReceiveSession::flush() {
    if (m_lastCount != m_received) { m_lastCount = m_received; emit receivedBytesChanged(); }
    if (m_dirty && !m_paused) publish(m_forceReset);
    else updateCacheLimit();
}
void ReceiveSession::updateCacheLimit() {
    if (m_lastLimited != cacheLimited()) { m_lastLimited = cacheLimited(); emit cacheLimitedChanged(); }
}
void ReceiveSession::publish(bool reset) {
    const auto display = ByteFormatter::format(m_buffer, m_options);
    const QString &next = display.text;
    m_displayLimited = display.limited;
    updateCacheLimit();
    const qsizetype retained = reset ? 0 : retainedPrefix(m_published, next);
    const int removed = reset ? 0 : int(m_published.size() - retained);
    const QString inserted = reset ? next : next.sliced(retained);
    const bool changed = next != m_published;
    m_published = next; m_dirty = false; m_forceReset = false;
    if (changed || reset) {
        emit displayUpdate(inserted, removed, reset);
        emit displayTextChanged();
    }
}
void ReceiveSession::refreshFormat() {
    m_dirty = true;
    m_forceReset = true;
    flush();
}
void ReceiveSession::setShowTimestamps(bool value) {
    if (m_options.timestamps == value) return;
    m_options.timestamps = value;
    emit showTimestampsChanged();
    refreshFormat();
}
void ReceiveSession::setShowRx(bool value) {
    if (m_options.showRx == value) return;
    m_options.showRx = value;
    emit directionsChanged();
    refreshFormat();
}
void ReceiveSession::setShowTx(bool value) {
    if (m_options.showTx == value) return;
    m_options.showTx = value;
    emit directionsChanged();
    refreshFormat();
}
void ReceiveSession::setHexMode(bool value) {
    if (m_options.hex == value) return;
    m_options.hex = value;
    emit hexModeChanged();
    refreshFormat();
}
void ReceiveSession::setDisplayPaused(bool value) {
    if (m_paused == value) return;
    m_paused = value; emit displayPausedChanged();
    if (!m_paused) { m_dirty = true; publish(true); }
}
void ReceiveSession::clear() {
    m_buffer.clear(); m_received = m_lastCount = 0; m_lastLimited = m_displayLimited = false;
    emit receivedBytesChanged(); emit cacheLimitedChanged();
    // Clear is explicit and takes effect even while display is paused.
    m_dirty = true; publish(true);
    emit cleared();
}
