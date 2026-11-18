#include "receivebuffer.h"
void ReceiveBuffer::append(const QByteArray &bytes, qint64 timestampUs, Direction direction) {
    if (bytes.isEmpty()) return;
    if (bytes.size() >= Capacity) {
        m_truncated = m_truncated || !m_bytes.isEmpty() || bytes.size() > Capacity;
        m_bytes = bytes.right(Capacity);
    } else {
        const qsizetype overflow = m_bytes.size() + bytes.size() - Capacity;
        if (overflow > 0) { m_bytes.remove(0, overflow); m_truncated = true; }
        m_bytes.append(bytes);
    }
    m_endOffset += qulonglong(bytes.size());
    m_batches.push_back({m_endOffset, timestampUs, direction});
    while (m_batches.front().endOffset <= startOffset()) m_batches.pop_front();
}
void ReceiveBuffer::clear() {
    m_bytes.clear();
    m_batches.clear();
    m_endOffset = 0;
    m_truncated = false;
}
