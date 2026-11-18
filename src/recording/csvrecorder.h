#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <memory>

class QThread;
struct DecodedFrame;

// Consumes parsed frames on the application thread. File I/O belongs to the worker.
class CsvRecorder final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canStart READ canStart NOTIFY changed)
    Q_PROPERTY(bool recording READ recording NOTIFY changed)
    Q_PROPERTY(bool stopping READ stopping NOTIFY changed)
    Q_PROPERTY(bool failed READ failed NOTIFY changed)
    Q_PROPERTY(qulonglong writtenFrames READ writtenFrames NOTIFY changed)
    Q_PROPERTY(qint64 elapsedMs READ elapsedMs NOTIFY changed)
    Q_PROPERTY(QString directory READ directory NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    explicit CsvRecorder(QObject *parent = nullptr);
    ~CsvRecorder() override;

    bool canStart() const { return m_available && !m_worker; }
    bool recording() const { return m_worker && !m_stopping; }
    bool stopping() const { return m_stopping; }
    bool failed() const { return m_failed; }
    qulonglong writtenFrames() const { return m_written; }
    qint64 elapsedMs() const { return recording() ? m_clock.elapsed() : m_elapsed; }
    QString directory() const { return m_directory; }
    QString status() const { return m_status; }

    void setAvailable(bool available);
    void appendFrame(const DecodedFrame &frame);
    Q_INVOKABLE bool start(const QUrl &parentDirectory);
    Q_INVOKABLE void stop();
    void stop(const QString &reason);

signals:
    void changed();

private:
    struct Queue;
    static void writeFrames(Queue &queue, const QString &directory);
    void poll();
    bool startFailed(const QString &error);

    QTimer m_statusTimer;
    QElapsedTimer m_clock;
    std::unique_ptr<Queue> m_queue;
    QThread *m_worker = nullptr;
    QString m_directory;
    QString m_status;
    QString m_stopReason;
    qulonglong m_written = 0;
    qint64 m_elapsed = 0;
    bool m_available = false;
    bool m_stopping = false;
    bool m_failed = false;
};
