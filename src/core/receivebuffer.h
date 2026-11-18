#pragma once
#include <QByteArray>
#include <deque>

// Transport-independent bounded raw-byte history. Reading it never consumes bytes.
class ReceiveBuffer {
public:
    static constexpr qsizetype Capacity = 256 * 1024;
    enum class Direction { Rx, Tx };
    struct Batch {
        qulonglong endOffset;
        qint64 timestampUs;
        Direction direction;
    };
    void append(const QByteArray &bytes, qint64 timestampUs = 0, Direction direction = Direction::Rx);
    void clear();
    const QByteArray &bytes() const { return m_bytes; }
    qsizetype size() const { return m_bytes.size(); }
    qulonglong startOffset() const { return m_endOffset - qulonglong(size()); }
    const std::deque<Batch> &batches() const { return m_batches; }
    bool truncated() const { return m_truncated; }
private:
    QByteArray m_bytes;
    std::deque<Batch> m_batches;
    qulonglong m_endOffset = 0;
    bool m_truncated = false;
};
