#include "protocolcatalog.h"
ProtocolCatalog::ProtocolCatalog()
    : m_protocols{{QStringLiteral("none"), QStringLiteral("原始数据"),
                   QStringLiteral("直接查看接收内容，支持 UTF-8 文本和 HEX。")},
                  {QStringLiteral("csv"),QStringLiteral("数字文本（CSV）"),QStringLiteral("每行一组逗号分隔的数字，以换行结束。例如 1.2,3.4,5.6；支持跨次接收和科学计数法。")},
                  {QStringLiteral("keyvalue"),QStringLiteral("键值文本（字典）"),QStringLiteral("每行一组 名称:数值，以逗号分隔、换行结束。例如 awa:2.0,  aaa:3；无需引号，忽略两侧空格。")},
                  {QStringLiteral("binary"),QStringLiteral("自定义二进制"),QStringLiteral("按已保存配置处理帧头、固定／变长帧、字段类型与大小端，并执行可选校验。")}} {}
const ProtocolDescriptor *ProtocolCatalog::find(const QString &id) const {
    for (const auto &protocol : m_protocols)
        if (protocol.id == id) return &protocol;
    return nullptr;
}
