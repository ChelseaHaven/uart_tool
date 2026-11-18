#pragma once
#include <QColor>
#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>

// Incremental receive logs are read-only, but Qt still records programmatic edits
// unless undo is explicitly disabled on the underlying document.
class LogDocumentSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor rxColor READ rxColor WRITE setRxColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor txColor READ txColor WRITE setTxColor NOTIFY colorsChanged)
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
public:
    explicit LogDocumentSettings(QObject *parent=nullptr);
    QColor rxColor() const;
    QColor txColor() const;
    void setRxColor(const QColor &color);
    void setTxColor(const QColor &color);
    QQuickTextDocument *document() const { return m_document; }
    void setDocument(QQuickTextDocument *document);
signals:
    void documentChanged();
    void colorsChanged();
private:
    class DirectionHighlighter;
    DirectionHighlighter *m_highlighter;
    QPointer<QQuickTextDocument> m_document;
    void configure();
};
void registerUartUiTypes();
