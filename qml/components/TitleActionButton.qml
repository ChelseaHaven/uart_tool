import QtQuick
import QtQuick.Controls

ToolButton {
    id: button
    property bool alertActive: false

    implicitWidth: 36
    implicitHeight: 36
    display: AbstractButton.IconOnly
    hoverEnabled: true
    icon.width: 20
    icon.height: 20
    icon.color: !enabled ? Theme.disabledText : alertActive ? Theme.error : Theme.text
    Accessible.name: text
    ToolTip.visible: pointer.hovered
    ToolTip.delay: 500
    ToolTip.text: text

    HoverHandler { id: pointer }

    background: Rectangle {
        radius: 6
        color: button.alertActive ? Theme.errorSurface
            : button.down ? Theme.pressed : button.hovered ? Theme.hover : "transparent"
    }
}
