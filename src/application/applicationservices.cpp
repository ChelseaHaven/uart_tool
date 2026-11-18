#include "applicationservices.h"
#include <utility>
ApplicationServices::ApplicationServices(SerialSource::PortScanner scanner)
    : m_source(std::move(scanner)), m_session(m_source), m_sender(m_source), m_connection(m_source) {
    QObject::connect(&m_source, &ByteSource::opened, &m_recorder, [this] { syncRecordingProtocol(); });
    QObject::connect(&m_source, &ByteSource::closed, &m_recorder, [this] {
        m_recorder.stop(QStringLiteral("串口已断开，录制已停止"));
        m_recorder.setAvailable(false);
    });
    QObject::connect(&m_protocol, &ProtocolSession::selectionChanged, &m_recorder, [this] { syncRecordingProtocol(); });
    QObject::connect(&m_protocol, &ProtocolSession::configurationChanged, &m_recorder, [this] { syncRecordingProtocol(); });
    QObject::connect(&m_protocol, &ProtocolSession::frameDecoded, &m_recorder, &CsvRecorder::appendFrame);
    syncRecordingProtocol();

    QObject::connect(&m_source, &ByteSource::opened, &m_sender, &SendSession::transportChanged);
    QObject::connect(&m_source, &ByteSource::closed, &m_sender, &SendSession::transportChanged);
    QObject::connect(&m_sender, &SendSession::bytesSubmitted, &m_session, &ReceiveSession::recordTransmission);
    QObject::connect(&m_source, &ByteSource::bytesReceived, &m_protocol, &ProtocolSession::feedBytes);
    QObject::connect(&m_protocol, &ProtocolSession::frameDecoded, &m_numericStream, &NumericStream::appendFrame);
    QObject::connect(&m_protocol, &ProtocolSession::sessionReset, &m_numericStream, &NumericStream::clear);
    // Successful opens already clear ReceiveSession via ByteSource::opened.
    // One clear signal therefore covers both explicit clears and new connections.
    QObject::connect(&m_session, &ReceiveSession::cleared, &m_protocol, &ProtocolSession::reset);
}

void ApplicationServices::syncRecordingProtocol() {
    const auto protocol = m_protocol.selectedProtocolId();
    const auto configuration = protocol == "binary" ? m_protocol.binaryConfiguration() : QVariantMap();
    if (protocol != m_recordingProtocol) {
        m_recorder.stop(QStringLiteral("数据协议已切换，录制已停止"));
    } else if (configuration != m_recordingConfiguration) {
        m_recorder.stop(QStringLiteral("二进制模板、配置或命令字已变更，录制已停止"));
    }
    m_recordingProtocol = protocol;
    m_recordingConfiguration = configuration;
    m_recorder.setAvailable(m_source.isOpen() && protocol != "none");
}
