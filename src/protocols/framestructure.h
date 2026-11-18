#pragma once
#include <QVariantMap>

// Compiles an ordered wire layout into transport-independent parser settings.
// Payload modules own their fields; repeated command modules retain independent choices.
class FrameStructure {
public:
    static QVariantMap compileMultiple(const QVariantList &blocks, const QVariantMap &base, QString &error);
    static QVariantMap compile(const QVariantList &blocks, const QVariantMap &base, QString &error);
};
