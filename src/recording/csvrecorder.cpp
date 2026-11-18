#include "csvrecorder.h"
#include "protocols/protocolparser.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QSet>
#include <QThread>
#include <QTimeZone>
#include <QWaitCondition>
#include <chrono>
#include <cmath>
#include <deque>
#include <limits>

namespace {
constexpr qsizetype MaxQueueBytes = 16 * 1024 * 1024;
constexpr size_t MaxQueuedFrames = 4096;
constexpr qsizetype MaxColumns = 4096;

struct PendingFrame {
    QVector<DecodedField> fields;
    qint64 utcUs = 0;
    qint64 elapsedUs = 0;
    qsizetype size = 0;
};

bool isNumeric(const DecodedField &field) {
    return field.type.startsWith("float") || field.type.startsWith("int") || field.type.startsWith("uint");
}

QByteArray number(const QVariant &value) {
    switch (value.metaType().id()) {
    case QMetaType::UInt:
    case QMetaType::ULongLong:
        return QByteArray::number(value.toULongLong());
    case QMetaType::Int:
    case QMetaType::LongLong:
        return QByteArray::number(value.toLongLong());
    default: {
        const auto n = value.toDouble();
        if (std::isfinite(n)) return QByteArray::number(n, 'g', std::numeric_limits<double>::max_digits10);
        return QByteArray(); // Missing scalar value. Arrays use JSON null for the same case.
    }
    }
}

QByteArray cell(const QVariant &value) {
    if (value.metaType().id() != QMetaType::QVariantList) return number(value);
    QByteArray array("[");
    const auto elements = value.toList();
    for (qsizetype i = 0; i < elements.size(); ++i) {
        if (i) array += ',';
        const auto element = number(elements.at(i));
        array += element.isEmpty() ? QByteArray("null") : element;
    }
    return array + ']';
}

QByteArray escapeCsv(QByteArray value) {
    if (!value.contains(',') && !value.contains('"') && !value.contains('\r') && !value.contains('\n')) return value;
    value.replace("\"", "\"\"");
    return '"' + value + '"';
}

QByteArray timestamp(qint64 epochUs) {
    const auto utc = QDateTime::fromMSecsSinceEpoch(epochUs / 1000, QTimeZone::utc());
    return utc.toString("yyyy-MM-dd'T'HH:mm:ss.zzz").toLatin1()
        + QByteArray::number(epochUs % 1000).rightJustified(3, '0') + 'Z';
}

QString createDirectory(const QString &parent) {
    QDir dir(parent);
    const auto name = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    for (int suffix = 1; suffix <= 1000; ++suffix) {
        const auto child = suffix == 1 ? name : name + '-' + QString::number(suffix);
        if (dir.mkdir(child)) return dir.absoluteFilePath(child);
        if (!dir.exists(child)) break;
    }
    return {};
}

class CsvFiles {
public:
    explicit CsvFiles(const QString &directory) : m_directory(directory) {}

    bool append(const PendingFrame &frame, qulonglong frameNumber) {
        const auto previousColumns = m_columns.size();
        QHash<QString, const DecodedField *> values;
        for (const auto &field : frame.fields) {
            if (!isNumeric(field)) continue;
            values.insert(field.name, &field);
            if (!m_knownColumns.contains(field.name)) {
                m_columns.append(field.name);
                m_knownColumns.insert(field.name);
            }
        }

        if (m_columns.size() > MaxColumns) {
            error = QStringLiteral("CSV 参数列超过 4096，已结束录制。");
            return false;
        }
        if ((!m_file.isOpen() || m_columns.size() != previousColumns) && !openSegment()) return false;

        QByteArray row = timestamp(frame.utcUs) + ',' + QByteArray::number(frame.elapsedUs)
            + ',' + QByteArray::number(frameNumber);
        for (const auto &field : m_columns) {
            row += ',';
            if (const auto *decoded = values.value(field)) row += escapeCsv(cell(decoded->value));
        }
        row += "\r\n";
        return write(row);
    }

    QString error;

private:
    bool openSegment() {
        m_file.close();
        ++m_segment;
        const auto suffix = m_segment == 1 ? QString()
            : QStringLiteral("_%1").arg(m_segment, 3, 10, QLatin1Char('0'));
        const auto fileName = m_directory.dirName() + suffix + ".csv";
        m_file.setFileName(m_directory.filePath(fileName));
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly | QIODevice::Unbuffered)) {
            error = QStringLiteral("无法创建 %1：%2").arg(fileName, m_file.errorString());
            return false;
        }
        QByteArray header("\xef\xbb\xbftimestamp_utc,elapsed_us,frame");
        // A fixed prefix keeps parameter names distinct from metadata and safe as spreadsheet headers.
        for (const auto &field : m_columns) header += ',' + escapeCsv("data." + field.toUtf8());
        header += "\r\n";
        if (write(header)) return true;
        m_file.remove();
        return false;
    }

    bool write(const QByteArray &data) {
        const auto offset = m_file.pos();
        if (m_file.write(data) == data.size()) return true;
        error = QStringLiteral("写入 %1 失败：%2").arg(QFileInfo(m_file).fileName(), m_file.errorString());
        // Keep only complete records if the filesystem accepts a partial write.
        m_file.resize(offset);
        return false;
    }

    const QDir m_directory;
    QFile m_file;
    QStringList m_columns;
    QSet<QString> m_knownColumns;
    int m_segment = 0;
};
}

struct CsvRecorder::Queue {
    QMutex mutex;
    QWaitCondition ready;
    std::deque<PendingFrame> frames;
    qsizetype bytes = 0;
    qulonglong written = 0;
    QString error;
    bool accepting = true;
    bool finished = false;
};

CsvRecorder::CsvRecorder(QObject *parent) : QObject(parent) {
    m_statusTimer.setInterval(100);
    connect(&m_statusTimer, &QTimer::timeout, this, &CsvRecorder::poll);
}

CsvRecorder::~CsvRecorder() {
    if (!m_worker) return;
    stop();
    m_worker->wait();
    delete m_worker;
}

void CsvRecorder::setAvailable(bool available) {
    if (m_available == available) return;
    m_available = available;
    emit changed();
}

bool CsvRecorder::startFailed(const QString &error) {
    m_failed = true;
    m_status = error;
    emit changed();
    return false;
}

bool CsvRecorder::start(const QUrl &parentDirectory) {
    if (m_worker) return false;
    m_directory.clear();
    m_written = 0;
    m_elapsed = 0;
    if (!m_available) return startFailed(QStringLiteral("请先连接串口并选择数据协议。"));
    if (!parentDirectory.isLocalFile() || !QDir(parentDirectory.toLocalFile()).exists())
        return startFailed(QStringLiteral("请选择存在的本地录制目录。"));

    m_directory = createDirectory(parentDirectory.toLocalFile());
    if (m_directory.isEmpty()) return startFailed(QStringLiteral("无法创建录制目录，请检查写入权限。"));

    m_queue = std::make_unique<Queue>();
    m_stopping = false;
    m_failed = false;
    m_stopReason.clear();
    m_status = QStringLiteral("录制中");
    m_clock.start();
    m_worker = QThread::create([queue = m_queue.get(), directory = m_directory] { writeFrames(*queue, directory); });
    m_worker->start();
    m_statusTimer.start();
    emit changed();
    return true;
}

void CsvRecorder::appendFrame(const DecodedFrame &frame) {
    if (!recording()) return;
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    PendingFrame pending{frame.fields, std::chrono::duration_cast<std::chrono::microseconds>(now).count(),
                         m_clock.nsecsElapsed() / 1000, qsizetype(sizeof(PendingFrame))};
    for (const auto &field : pending.fields) {
        pending.size += sizeof(DecodedField) + (field.name.size() + field.type.size()) * sizeof(QChar);
        pending.size += field.value.metaType().id() == QMetaType::QByteArray
            ? field.byteCount : qsizetype(field.elementCount) * sizeof(QVariant);
    }

    bool overflow = false;
    {
        QMutexLocker lock(&m_queue->mutex);
        if (!m_queue->accepting) return;
        overflow = m_queue->frames.size() >= MaxQueuedFrames || pending.size > MaxQueueBytes - m_queue->bytes;
        if (overflow) {
            m_queue->error = QStringLiteral("录制队列已满，已停止录制。");
            m_queue->accepting = false;
        } else {
            m_queue->bytes += pending.size;
            m_queue->frames.push_back(std::move(pending));
        }
        m_queue->ready.wakeOne();
    }
    if (overflow) stop(QStringLiteral("录制队列已满"));
}

void CsvRecorder::stop() {
    stop(QStringLiteral("录制已停止"));
}

void CsvRecorder::stop(const QString &reason) {
    if (!recording()) return;
    m_elapsed = m_clock.elapsed();
    m_stopping = true;
    m_stopReason = reason;
    m_status = QStringLiteral("正在保存录制");
    {
        QMutexLocker lock(&m_queue->mutex);
        m_queue->accepting = false;
        m_queue->ready.wakeOne();
    }
    emit changed();
}

void CsvRecorder::poll() {
    bool finished;
    QString error;
    {
        QMutexLocker lock(&m_queue->mutex);
        m_written = m_queue->written;
        finished = m_queue->finished;
        error = m_queue->error;
    }

    if (finished) {
        if (!m_stopping) m_elapsed = m_clock.elapsed();
        m_statusTimer.stop();
        m_worker->wait();
        delete m_worker;
        m_worker = nullptr;
        m_queue.reset();
        m_stopping = false;
        m_failed = !error.isEmpty();
        m_status = m_failed ? error : m_stopReason;
        if (!m_failed && m_written == 0) {
            m_status += QStringLiteral("，未收到有效帧");
        }
    }
    emit changed();
}

void CsvRecorder::writeFrames(Queue &queue, const QString &directory) {
    qulonglong written = 0;
    QString error;
    {
        CsvFiles files(directory);
        for (;;) {
            PendingFrame frame;
            {
                QMutexLocker lock(&queue.mutex);
                while (queue.frames.empty() && queue.accepting) queue.ready.wait(&queue.mutex);
                if (queue.frames.empty()) break;
                frame = std::move(queue.frames.front());
                queue.frames.pop_front();
                queue.bytes -= frame.size;
            }
            if (!files.append(frame, written + 1)) {
                error = files.error;
                break;
            }
            QMutexLocker lock(&queue.mutex);
            queue.written = ++written;
        }
    }
    QMutexLocker lock(&queue.mutex);
    queue.accepting = false;
    queue.frames.clear();
    if (!error.isEmpty()) queue.error = error;
    queue.finished = true;
}
