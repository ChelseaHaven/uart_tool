#include "byteformatter.h"
#include <QDateTime>
#include <QStringDecoder>
#include <algorithm>
#include <iterator>
namespace {
QString formatText(QStringDecoder &decoder, QByteArrayView view) {
    // Decoder retains incomplete trailing UTF-8; the next snapshot includes it again.
    QString text = decoder(view);
    text.replace("\r\n", "\n"); text.replace('\r', '\n');
    // Invisible binary controls get visible symbols; binary data itself stays untouched.
    for (auto &c : text) {
        const ushort code = c.unicode();
        if ((code < 32 && code != 9 && code != 10) || code == 127)
            c = QChar(code == 127 ? 0x2421 : 0x2400 + code);
        else if (code == 0x2028 || code == 0x2029)
            c = QChar(0x2424);
    }
    return text;
}
QString formatStamp(qint64 receivedUs) {
    const auto time = QDateTime::fromMSecsSinceEpoch(receivedUs / 1000).toString("HH:mm:ss.zzz");
    return QStringLiteral("[%1%2] ").arg(time).arg(receivedUs % 1000, 3, 10, QLatin1Char('0'));
}
}

ByteFormatter::Display ByteFormatter::format(const ReceiveBuffer &buffer, const Options &options) {
    using Direction = ReceiveBuffer::Direction;
    constexpr auto Capacity = ReceiveBuffer::Capacity;
    Display display;
    const auto &batches = buffer.batches();
    const QByteArrayView bytes(buffer.bytes());
    const auto start = buffer.startOffset();
    auto endPosition = [start](const auto &batch) { return qsizetype(batch.endOffset - start); };

    QString stamp;
    qint64 stampUs = 0;
    auto appendRow = [&](const ReceiveBuffer::Batch &batch, const QString &data) {
        if (data.isEmpty()) return;
        if (!display.text.isEmpty() && display.text.back() != '\n') display.text += QLatin1Char('\n');
        QString prefix = batch.direction == Direction::Rx ? QStringLiteral("[RX] ") : QStringLiteral("[TX] ");
        if (options.timestamps) {
            if (stamp.isEmpty() || stampUs != batch.timestampUs) {
                stampUs = batch.timestampUs;
                stamp = formatStamp(stampUs);
            }
            prefix += stamp;
        }
        const auto space = Capacity - prefix.size();
        display.text += prefix;
        if (data.size() > space) {
            auto first = data.size() - space;
            if (data[first].isLowSurrogate()) ++first;
            display.text += QStringView(data).sliced(first);
            display.limited = true;
        } else {
            display.text += data;
        }
    };

    QStringDecoder rxDecoder(QStringDecoder::Utf8);
    bool rxEndsWithCr = false;
    qsizetype first = 0;
    for (auto batch = batches.begin(); batch != batches.end();) {
        const bool rx = batch->direction == Direction::Rx;
        auto last = batch;
        if (rx && !options.hex) {
            // Adjacent reads are a continuous text stream. A send starts a new log entry.
            while (std::next(last) != batches.end() && std::next(last)->direction == Direction::Rx) ++last;
        }
        const auto end = endPosition(*last);
        if (rx ? options.showRx : options.showTx) {
            if (options.hex) {
                for (auto pos = first; pos < end;) {
                    const auto length = std::min<qsizetype>(16, end - pos);
                    appendRow(*batch, QString::fromLatin1(bytes.sliced(pos, length).toByteArray().toHex(' ').toUpper()));
                    pos += length;
                }
            } else {
                QStringDecoder txDecoder(QStringDecoder::Utf8);
                auto &decoder = rx ? rxDecoder : txDecoder;
                auto pos = first;
                if (rx && rxEndsWithCr && pos < end && bytes[pos] == '\n') ++pos;
                if (rx) rxEndsWithCr = bytes[end - 1] == '\r';
                if (first == 0 && buffer.truncated()) {
                    // The retained range can start in the middle of a UTF-8 character.
                    while (pos < end && (static_cast<unsigned char>(bytes[pos]) & 0xc0) == 0x80) ++pos;
                }
                for (auto rowBatch = batch; pos < end;) {
                    auto next = pos;
                    while (next < end && bytes[next] != '\r' && bytes[next] != '\n') ++next;
                    if (next < end && bytes[next++] == '\r' && next < end && bytes[next] == '\n') ++next;
                    while (pos >= endPosition(*rowBatch)) ++rowBatch;
                    appendRow(*rowBatch, formatText(decoder, bytes.sliced(pos, next - pos)));
                    pos = next;
                }
            }
        }
        first = end;
        batch = std::next(last);
    }

    if (display.text.size() > Capacity) {
        const auto firstKept = display.text.indexOf(QLatin1Char('\n'), display.text.size() - Capacity - 1) + 1;
        display.text.remove(0, firstKept);
        display.limited = true;
    }
    return display;
}
