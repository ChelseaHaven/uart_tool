#pragma once
#include <QByteArray>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include "checksumrule.h"

struct BinaryField {
    QString name, type, endian = "little";
    int offset = 2, count = 1;
    bool variable = false;
};
struct MessageLayout {
    quint32 command=0;
    QString name;
    int payloadSize=-1;
    QVector<BinaryField> fields;
};
struct BinaryConfig {
    QByteArray header = QByteArray::fromHex("aa55");
    QByteArray tail;
    QVariantList structure;
    QVariantList commandMatches;
    QString mode = "fixed", lengthEndian = "little", lengthMeaning = "payload";
    int frameSize = 6, maxFrameSize = 4096;
    int lengthOffset = 2, lengthBytes = 2, payloadOffset = 4;
    bool lengthIncludesChecksum = false;
    QString checksum = "none", checksumEndian = "little";
    int checksumStart = 0, checksumEnd = -1;
    QVector<BinaryField> fields{{"value", "float32", "little", 2, 1, false}};
    // Trailer layout is independent of whether its checksum checks are enabled.
    int trailerBytes=-1;
    QVector<ChecksumRule> checks;
    int commandOffset=-1, commandBytes=2;
    QString commandEndian="little", templateId="custom";
    quint32 commandValue=0;
    QVector<MessageLayout> messages;
    bool autoLayout=true;
    int legacyChecksumBytes() const;
    int checksumBytes() const;
    static QVariantMap singleCommandConfiguration(const QVariantMap &map);
    static QVariantMap preset(const QString &id);
    QVariantMap toMap() const;
    static int typeBytes(const QString &type);
    static bool fromMap(const QVariantMap &map, BinaryConfig &out, QString &error);
};
