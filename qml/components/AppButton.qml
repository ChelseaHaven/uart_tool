import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
        id: button
        property bool primary: false
        implicitHeight: 38; implicitWidth: 90
        hoverEnabled: true
        contentItem: Text {
            text: button.text; font: button.font
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            color: !button.enabled ? Theme.disabledText : button.primary ? Theme.contrastText : Theme.text
        }
        background: Rectangle {
            radius: 7
            color: !button.enabled ? Theme.disabledSurface : button.primary ? (button.down ? Theme.accentPressed : button.hovered ? Theme.accentHover : Theme.accent) : button.down ? Theme.pressed : button.hovered ? Theme.hover : Theme.surface
            border.color: button.primary && button.enabled ? Theme.accent : Theme.border
        }
    }
