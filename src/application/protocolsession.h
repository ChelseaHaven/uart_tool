#pragma once
#include "protocols/protocolcatalog.h"
#include "protocols/protocolparser.h"
#include "protocols/framestructure.h"
#include "protocols/protocoltemplatestore.h"
#include "protocols/binarychannels.h"
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>
class ProtocolSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList protocols READ protocols CONSTANT)
    Q_PROPERTY(ProtocolTemplateStore *templateStore READ templateStore CONSTANT)
    Q_PROPERTY(QString selectedProtocolId READ selectedProtocolId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedDescription READ selectedDescription NOTIFY selectionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString binaryTemplateName READ binaryTemplateName NOTIFY configurationChanged)
    Q_PROPERTY(QVariantMap binaryConfiguration READ binaryConfiguration NOTIFY configurationChanged)
    Q_PROPERTY(QString configurationError READ configurationError NOTIFY configurationErrorChanged)
    Q_PROPERTY(qulonglong frameCount READ frameCount NOTIFY statisticsChanged)
    Q_PROPERTY(qulonglong invalidFrames READ invalidFrames NOTIFY statisticsChanged)
    Q_PROPERTY(qulonglong discardedBytes READ discardedBytes NOTIFY statisticsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY statisticsChanged)
    Q_PROPERTY(QVariantList latestFields READ latestFields NOTIFY latestFrameChanged)
    Q_PROPERTY(QString lastFrameTime READ lastFrameTime NOTIFY latestFrameChanged)
    Q_PROPERTY(bool displayPaused READ displayPaused WRITE setDisplayPaused NOTIFY displayPausedChanged)
    Q_PROPERTY(QVariantList binaryCommandGroups READ binaryCommandGroups NOTIFY configurationChanged)
    Q_PROPERTY(QVariantList binaryChannels READ binaryChannels NOTIFY binaryChannelsChanged)
    Q_PROPERTY(QString selectedBinaryChannelId READ selectedBinaryChannelId NOTIFY binaryChannelChanged)
public:
    explicit ProtocolSession(QObject *parent=nullptr);
    QVariantList protocols() const { return m_choices; }
    ProtocolTemplateStore *templateStore() { return &m_templates; }
    QString selectedProtocolId() const { return m_selected; }
    QString selectedDescription() const;
    QString statusText() const { return m_status; }
    QVariantMap binaryConfiguration() const;
    QString binaryTemplateName() const { return m_binaryTemplateName; }
    QVariantList binaryCommandGroups() const;
    Q_INVOKABLE bool selectBinaryCommand(int blockIndex,const QString &value);
    QVariantList binaryChannels() const { return m_binaryChannels.choices(); }
    QString selectedBinaryChannelId() const { return m_selectedBinaryChannel; }
    QString configurationError() const { return m_configError; }
    qulonglong frameCount() const;
    qulonglong invalidFrames() const;
    qulonglong discardedBytes() const;
    QString lastError() const;
    QVariantList latestFields() const { return m_latestRows; }
    QString lastFrameTime() const { return m_lastTime; }
    bool displayPaused() const { return m_displayPaused; }
    void setDisplayPaused(bool paused);
    Q_INVOKABLE QString validateChecksumRule(const QVariantMap &value) const {
        ChecksumRule rule; QString error; ChecksumRule::fromMap(value,rule,error); return error;
    }
    Q_INVOKABLE QVariantMap checksumPreset(const QString &algorithm) const {
        ChecksumRule rule; QString error;
        if(!ChecksumRule::fromMap({{"algorithm",algorithm}},rule,error)) return {};
        return rule.toMap();
    }
    Q_INVOKABLE QVariantMap binaryTemplate(const QString &id) { return m_templates.loadTemplate(id).value("configuration").toMap(); }
    Q_INVOKABLE QVariantMap newBinaryConfiguration() const {
        auto configuration=BinaryConfig::preset("generic");
        configuration["templateId"]="custom";
        return configuration;
    }
    Q_INVOKABLE QVariantMap compileFrameStructure(const QVariantList &blocks,const QVariantMap &base) const {
        QString error; const auto configuration=FrameStructure::compile(blocks,base,error);
        return {{"configuration",configuration},{"error",error}};
    }
    Q_INVOKABLE bool selectProtocol(const QString &id);
    Q_INVOKABLE bool selectBinaryChannel(const QString &id);
    Q_INVOKABLE bool applyBinaryConfiguration(const QVariantMap &configuration);
    void feedBytes(const QByteArray &bytes);
    void reset();
signals:
    void frameDecoded(const DecodedFrame &frame);
    void selectionChanged(); void statusChanged(); void sessionReset();
    void configurationChanged(); void configurationErrorChanged();
    void statisticsChanged(); void latestFrameChanged(); void latestFrameAboutToChange();
    void displayPausedChanged();
    void binaryChannelsChanged(); void binaryChannelChanged();
private:
    ProtocolCatalog m_catalog;
    ProtocolTemplateStore m_templates;
    QVariantList m_choices,m_latestRows;
    QString m_selected="none",m_status=QStringLiteral("未启用协议解析"),m_configError,m_lastTime;
    BinaryConfig m_binary;
    QString m_binaryTemplateName;
    BinaryChannels m_binaryChannels;
    QString m_selectedBinaryChannel;
    quint64 m_channelVersion=0;
    std::unique_ptr<ProtocolParser> m_parser;
    DecodedFrame m_latest;
    QHash<QString, qsizetype> m_dictionaryFieldIndices;
    QTimer m_publishTimer;
    quint64 m_frames=0;
    bool m_dirty=false,m_frameDirty=false;
    bool m_displayPaused=false;
    void publish();
    void rebuildBinaryChannels(bool selectCurrent=false);
    void syncChannel(bool force=false);
    void storeLatestFrame(DecodedFrame frame);
    void publishFrame();
};
