#pragma once
#include <QByteArrayView>
#include <QVariantMap>

// Polynomial is always the normal (unreflected) representation: 0x1021, not 0x8408.
struct ChecksumRule {
    QString name="校验", algorithm="crc16modbus", endian="little";
    bool enabled=true, early=false, reflectInput=true, reflectOutput=true;
    int offset=-2, start=0, end=-2, width=16;
    quint32 polynomial=0x8005, initial=0xffff, xorOut=0;
    int bytes() const { return width/8; }
    quint32 calculate(QByteArrayView data) const;
    QVariantMap toMap() const;
    static bool fromMap(const QVariantMap &,ChecksumRule &,QString &error);
};
