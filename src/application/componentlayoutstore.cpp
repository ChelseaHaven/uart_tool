#include "componentlayoutstore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>

namespace {
constexpr qsizetype MaxFileBytes = 16 * 1024 * 1024;
constexpr int MaxComponents = 8;
constexpr int CanvasLimit = 8192;
constexpr auto SettingsKey = "workspace/componentLayout";
constexpr auto Format = "uart-tool.component-layout";

QJsonObject normalizeSend(const QJsonObject &send) {
    const auto mode = send.value("mode").toInt();
    const auto ending = send.value("ending").toString();
    QJsonArray parameters;
    const auto rows = send.value("parameters").toArray();
    for (qsizetype i = 0; i < qMin<qsizetype>(32, rows.size()); ++i) {
        const auto row = rows.at(i).toObject();
        const auto step = row.value("step").toString().left(48);
        bool ok = false;
        const auto stepNumber = step.toDouble(&ok);
        parameters.append(QJsonObject{{"name", row.value("name").toString().left(64)},
                                     {"value", row.value("value").toString().left(48)},
                                     {"step", ok && std::isfinite(stepNumber) && stepNumber > 0 ? step : QStringLiteral("0.1")}});
    }
    return {{"mode", qBound(0, mode, 2)}, {"text", send.value("text").toString()},
            {"ending", ending == "none" || ending == "cr" || ending == "crlf" ? ending : QStringLiteral("lf")},
            {"repeat", send.value("repeat").toBool()},
            {"intervalMs", send.value("intervalMs").toString(QStringLiteral("1000")).left(10)},
            {"parameters", parameters}};
}

QJsonObject normalizeLayout(const QJsonObject &source, QString &error) {
    if (source.value("format") != Format || source.value("version") != 1 || !source.value("components").isArray()) {
        error = QStringLiteral("不支持的组件布局格式或版本。");
        return {};
    }

    const auto input = source.value("components").toArray();
    if (input.size() > MaxComponents) {
        error = QStringLiteral("一个布局最多包含 8 个组件。");
        return {};
    }

    QJsonArray components;
    for (const auto &value : input) {
        const auto item = value.toObject();
        const auto type = item.value("type").toString();
        if (type != "time-plot" && type != "xy-plot" && type != "quick-send") {
            error = QStringLiteral("布局包含未知的组件类型。");
            return {};
        }
        const int minimumWidth = type == "quick-send" ? 384 : 300;
        const auto x = item.value("x").toDouble(-1);
        const auto y = item.value("y").toDouble(-1);
        const auto width = item.value("width").toDouble(-1);
        const auto height = item.value("height").toDouble(-1);
        if (x < 0 || y < 0 || width < minimumWidth || height < 200
            || x + width > CanvasLimit + 0.001 || y + height > CanvasLimit + 0.001) {
            error = QStringLiteral("布局中的组件位置或尺寸无效。");
            return {};
        }
        QJsonObject component{{"type", type}, {"x", x}, {"y", y}, {"width", width}, {"height", height}};
        if (type == "quick-send") {
            component["name"] = item.value("name").toString().trimmed().left(40);
            component["send"] = normalizeSend(item.value("send").toObject());
        } else {
            component["focusEnabled"] = item.value("focusEnabled").toBool(true);
            const auto addReference = [&](const QString &enabledKey, const QString &valueKey) {
                component[enabledKey] = item.value(enabledKey).toBool(true);
                const auto level = item.value(valueKey).toDouble();
                component[valueKey] = std::isfinite(level) ? level : 0.0;
            };
            if (type == "time-plot") {
                addReference(QStringLiteral("referenceEnabled"), QStringLiteral("referenceValue"));
            } else {
                addReference(QStringLiteral("xReferenceEnabled"), QStringLiteral("xReferenceValue"));
                addReference(QStringLiteral("yReferenceEnabled"), QStringLiteral("yReferenceValue"));
            }
        }
        components.append(component);
    }

    QJsonObject result{{"format", Format}, {"version", 1}, {"components", components}};
    const int maximizedIndex = source.value("maximizedIndex").toInt(-1);
    result["maximizedIndex"] = maximizedIndex >= 0 && maximizedIndex < components.size() ? maximizedIndex : -1;
    QJsonArray stackOrder;
    for (const auto &value : source.value("stackOrder").toArray()) {
        const int index = value.toInt(-1);
        if (index >= 0 && index < components.size() && !stackOrder.contains(index))
            stackOrder.append(index);
    }
    if (stackOrder.size() != components.size()) {
        // Older layouts put just the selected component above insertion order.
        stackOrder = {};
        const int activeIndex = source.value("activeIndex").toInt(-1);
        for (int i = 0; i < components.size(); ++i) {
            if (i != activeIndex) stackOrder.append(i);
        }
        if (activeIndex >= 0 && activeIndex < components.size()) stackOrder.append(activeIndex);
    }
    result["stackOrder"] = stackOrder;
    for (const auto &key : {"scrollX", "scrollY"}) {
        result[key] = qBound(0.0, source.value(key).toDouble(), double(CanvasLimit + 16));
    }
    return result;
}

QJsonObject readLayout(const QByteArray &bytes, QString &error) {
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        error = QStringLiteral("布局文件不是有效的 JSON 对象。");
        return {};
    }
    return normalizeLayout(doc.object(), error);
}
}

ComponentLayoutStore::ComponentLayoutStore(QSettings &settings) : m_settings(settings) {}

void ComponentLayoutStore::setError(const QString &error) {
    if (m_error == error) return;
    m_error = error;
    emit errorChanged();
}

QVariantMap ComponentLayoutStore::lastLayout() const {
    const auto bytes = m_settings.value(SettingsKey).toByteArray();
    if (bytes.isEmpty() || bytes.size() > MaxFileBytes) return {};
    QString error;
    return readLayout(bytes, error).toVariantMap();
}

bool ComponentLayoutStore::saveLastLayout(const QVariantMap &layout) {
    QString error;
    const auto normalized = normalizeLayout(QJsonObject::fromVariantMap(layout), error);
    if (normalized.isEmpty()) {
        setError(error);
        return false;
    }
    const auto bytes = QJsonDocument(normalized).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxFileBytes) {
        setError(QStringLiteral("布局文件不能超过 16 MiB。"));
        return false;
    }
    m_settings.setValue(SettingsKey, bytes);
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        setError(QStringLiteral("无法保存上次的组件布局。"));
        return false;
    }
    setError({});
    return true;
}

QVariantMap ComponentLayoutStore::importLayout(const QUrl &url) {
    if (!url.isLocalFile()) {
        setError(QStringLiteral("请选择本地布局文件。"));
        return {};
    }
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("无法打开布局：%1").arg(file.errorString()));
        return {};
    }
    const auto bytes = file.read(MaxFileBytes + 1);
    if (file.error() != QFileDevice::NoError) {
        setError(QStringLiteral("无法读取布局：%1").arg(file.errorString()));
        return {};
    }
    if (bytes.size() > MaxFileBytes) {
        setError(QStringLiteral("布局文件不能超过 16 MiB。"));
        return {};
    }
    QString error;
    const auto layout = readLayout(bytes, error);
    setError(error);
    return layout.toVariantMap();
}

bool ComponentLayoutStore::exportLayout(const QVariantMap &layout, const QUrl &url) {
    if (!url.isLocalFile()) {
        setError(QStringLiteral("请选择本地保存位置。"));
        return false;
    }
    QString error;
    const auto normalized = normalizeLayout(QJsonObject::fromVariantMap(layout), error);
    if (normalized.isEmpty()) {
        setError(error);
        return false;
    }
    QSaveFile file(url.toLocalFile());
    const auto bytes = QJsonDocument(normalized).toJson(QJsonDocument::Indented);
    if (bytes.size() > MaxFileBytes) {
        setError(QStringLiteral("布局文件不能超过 16 MiB。"));
        return false;
    }
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setError(QStringLiteral("无法导出布局：%1").arg(file.errorString()));
        return false;
    }
    setError({});
    return true;
}
