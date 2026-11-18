#include "sendsession.h"

bool SendSession::status(const QString &text,bool failed) {
    m_status=text; m_failed=failed; emit statusChanged(); return !failed;
}
void SendSession::transportChanged() {
    m_status.clear(); m_failed=false; emit connectionChanged(); emit statusChanged();
}
bool SendSession::send(const QString &input,bool hexMode,const QString &ending) {
    if(!connected()) return status(QStringLiteral("请先连接串口。"),true);
    if(input.size()>MaxPayload*4) return status(QStringLiteral("发送内容过长，单次最多 64 KiB。"),true);
    QByteArray suffix;
    if(ending==QStringLiteral("lf")) suffix="\n";
    else if(ending==QStringLiteral("crlf")) suffix="\r\n";
    else if(ending==QStringLiteral("cr")) suffix="\r";
    else if(ending!=QStringLiteral("none")) return status(QStringLiteral("无效的发送结尾。"),true);
    QByteArray payload;
    if(hexMode) {
        QByteArray digits; digits.reserve(input.size());
        for(const QChar ch:input) {
            if(ch.isSpace()) continue;
            const auto c=ch.unicode();
            if(!((c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F')))
                return status(QStringLiteral("HEX 只允许 0–9、A–F 和空白，例如 AA 01 FF。"),true);
            digits.append(char(c));
        }
        if(digits.size()%2) return status(QStringLiteral("HEX 每个字节需要两位，不能只有半个字节。"),true);
        payload=QByteArray::fromHex(digits);
    } else payload=input.toUtf8();
    payload.append(suffix);
    if(payload.isEmpty()) return status(QStringLiteral("请输入发送内容。"),true);
    if(payload.size()>MaxPayload) return status(QStringLiteral("发送内容过长，单次最多 64 KiB（含结尾）。"),true);
    const auto result=m_sink.writeBytes(payload);
    if (result.accepted > 0) emit bytesSubmitted(payload.left(qMin<qint64>(result.accepted, payload.size())));
    if(result.accepted!=payload.size()) {
        if(!result.error.isEmpty()) return status(result.error,true);
        return status(QStringLiteral("发送未完整提交（%1 / %2 字节）。").arg(qMax(qint64(0),result.accepted)).arg(payload.size()),true);
    }
    return status(QStringLiteral("已提交 %1 字节。").arg(payload.size()),false);
}
