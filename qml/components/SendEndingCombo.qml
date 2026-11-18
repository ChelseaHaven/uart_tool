import QtQuick
import QtQuick.Layouts

ConfigCombo {
    objectName: "sendEnding"
    Accessible.name: "发送结尾"
    Layout.fillWidth: false
    Layout.preferredWidth: 100
    textRole: "label"
    valueRole: "value"
    model: [
        {label:"无结尾",value:"none"},
        {label:"\\n",value:"lf"},
        {label:"\\r\\n",value:"crlf"},
        {label:"\\r",value:"cr"}
    ]
}
