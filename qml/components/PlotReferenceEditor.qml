import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: editor
    required property string nameSuffix
    required property string axis
    required property string label
    required property bool lineEnabled
    required property real value
    signal enabledEdited(bool enabled)
    signal valueEdited(real value)

    Layout.fillWidth: true

    CheckBox {
        objectName: "plotReferenceToggle_" + editor.nameSuffix
        text: editor.label
        checked: editor.lineEnabled
        onToggled: editor.enabledEdited(checked)
    }
    Item { Layout.fillWidth: true }
    Text { text: editor.axis + " ="; color: Theme.secondary }
    AppTextField {
        objectName: "plotReferenceValue_" + editor.nameSuffix
        Layout.preferredWidth: 86
        enabled: editor.lineEnabled
        Accessible.name: editor.label + "数值"
        selectByMouse: true
        text: String(editor.value)
        onEditingFinished: {
            const input = text.trim()
            const value = Number(input)
            if (/^[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?$/.test(input) && Number.isFinite(value))
                editor.valueEdited(value)
            text = String(editor.value)
        }
    }
}
