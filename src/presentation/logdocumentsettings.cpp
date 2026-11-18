#include "logdocumentsettings.h"
#include "dockwindowcontroller.h"
#include "componentdragcontroller.h"
#include "sendtimer.h"
#include "plotting/timeseriesplot.h"
#include "plotting/xyplot.h"
#include <QSyntaxHighlighter>
#include <qqml.h>

class LogDocumentSettings::DirectionHighlighter : public QSyntaxHighlighter {
public:
    explicit DirectionHighlighter(QObject *parent) : QSyntaxHighlighter(parent) {}
    QColor rxColor, txColor;

protected:
    void highlightBlock(const QString &line) override {
        const auto color = line.startsWith(QLatin1String("[TX] ")) ? txColor
                         : line.startsWith(QLatin1String("[RX] ")) ? rxColor : QColor();
        if (color.isValid()) setFormat(0, line.size(), color);
    }
};

LogDocumentSettings::LogDocumentSettings(QObject *parent)
    : QObject(parent), m_highlighter(new DirectionHighlighter(this)) {}
QColor LogDocumentSettings::rxColor() const { return m_highlighter->rxColor; }
QColor LogDocumentSettings::txColor() const { return m_highlighter->txColor; }
void LogDocumentSettings::setRxColor(const QColor &color) {
    if (color == rxColor()) return;
    m_highlighter->rxColor = color;
    m_highlighter->rehighlight();
    emit colorsChanged();
}
void LogDocumentSettings::setTxColor(const QColor &color) {
    if (color == txColor()) return;
    m_highlighter->txColor = color;
    m_highlighter->rehighlight();
    emit colorsChanged();
}
void LogDocumentSettings::configure() {
    auto *document = m_document ? m_document->textDocument() : nullptr;
    if (document) document->setUndoRedoEnabled(false);
    m_highlighter->setDocument(document);
}
void LogDocumentSettings::setDocument(QQuickTextDocument *document) {
    if(m_document==document) return;
    if(m_document) disconnect(m_document,nullptr,this,nullptr);
    m_document=document;
    if(document) connect(document,&QQuickTextDocument::textDocumentChanged,this,&LogDocumentSettings::configure);
    configure(); emit documentChanged();
}
void registerUartUiTypes() {
    static const int typeId=qmlRegisterType<LogDocumentSettings>("Uart.Ui",1,0,"LogDocumentSettings");
    Q_UNUSED(typeId)
    static const int dockTypeId=qmlRegisterType<DockWindowController>("Uart.Ui",1,0,"DockWindowController");
    Q_UNUSED(dockTypeId)
    static const int plotType=qmlRegisterType<TimeSeriesPlot>("Uart.Ui",1,0,"TimeSeriesPlot");
    static const int streamType=qmlRegisterUncreatableType<NumericStream>("Uart.Ui",1,0,"NumericStream","Created by ApplicationServices");
    static const int xyPlotType=qmlRegisterType<XYPlot>("Uart.Ui",1,0,"XYPlot");
    Q_UNUSED(xyPlotType)
    Q_UNUSED(plotType)
    Q_UNUSED(streamType)
    static const int componentDrag=qmlRegisterType<ComponentDragController>("Uart.Ui",1,0,"ComponentDragController");
    Q_UNUSED(componentDrag)
    static const int sendTimerType=qmlRegisterType<SendTimer>("Uart.Ui",1,0,"SendTimer");
    Q_UNUSED(sendTimerType)
}
