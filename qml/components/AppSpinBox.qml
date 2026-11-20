import QtQuick
import QtQuick.Controls

SpinBox {
    id: spin

    contentItem: TextInput {
        id: field
        z: 2
        text: spin.displayText
        clip: width < implicitWidth
        padding: 6
        font: spin.font
        color: spin.palette.text
        selectionColor: spin.palette.highlight
        selectedTextColor: spin.palette.highlightedText
        horizontalAlignment: TextInput.AlignHCenter
        verticalAlignment: TextInput.AlignVCenter
        readOnly: !spin.editable
        validator: spin.validator
        inputMethodHints: spin.inputMethodHints
        selectByMouse: spin.editable
        persistentSelection: true

        ContextMenu.menu: null
        TextEditingMenu { editor: field }

        Rectangle {
            anchors.fill: parent
            visible: spin.activeFocus
            color: "transparent"
            border.color: spin.palette.highlight
            border.width: 2
        }
    }
}
