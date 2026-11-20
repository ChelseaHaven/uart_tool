import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: panel
    required property Item trigger
    required property bool focusEnabled
    required property string focusToggleName
    default property alias settings: options.data
    signal focusEdited(bool enabled)

    focus: true
    width: 280
    padding: 10
    font.pixelSize: 13
    onAboutToShow: {
        const position = trigger.mapToItem(parent, 0, trigger.height + 4)
        x = Math.max(0, Math.min(position.x, parent.width - width))
        y = position.y
    }
    background: Rectangle {
        color: Theme.surface
        border.color: Theme.border
        radius: 8
    }
    contentItem: ColumnLayout {
        spacing: 5
        CheckBox {
            objectName: panel.focusToggleName
            text: "焦点（长按显示坐标）"
            checked: panel.focusEnabled
            onToggled: panel.focusEdited(checked)
        }
        ColumnLayout {
            id: options
            Layout.fillWidth: true
            spacing: 5
        }
    }
}
