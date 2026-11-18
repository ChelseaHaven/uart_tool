#pragma once
#include "appsettings.h"
#include "plotting/numericstream.h"
#include "receivesession.h"
#include "sendsession.h"
#include "protocolsession.h"
#include "recording/csvrecorder.h"
#include "presentation/serialconnectionmodel.h"

// Composition root: owns lifetimes and chooses the actual source implementation.
// Data travels SerialSource -> ByteSource signals -> ReceiveSession -> receive UI.
class ApplicationServices {
public:
    explicit ApplicationServices(SerialSource::PortScanner scanner = QSerialPortInfo::availablePorts);
    AppSettings &settings() { return m_settings; }
    SerialConnectionModel &serialConnection() { return m_connection; }
    ProtocolSession &protocolSession() { return m_protocol; }
    NumericStream &numericStream() { return m_numericStream; }
    SendSession &sendSession() { return m_sender; }
    CsvRecorder &recorder() { return m_recorder; }
    ReceiveSession &receiveSession() { return m_session; }
private:
    AppSettings m_settings;
    SerialSource m_source;
    ReceiveSession m_session;
    SendSession m_sender;
    ProtocolSession m_protocol;
    NumericStream m_numericStream;
    SerialConnectionModel m_connection;
    CsvRecorder m_recorder;
    QString m_recordingProtocol;
    QVariantMap m_recordingConfiguration;
    void syncRecordingProtocol();
};
