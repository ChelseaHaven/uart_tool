import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ComboBox {
    id: combo
    selectTextByMouse: editable
    Layout.fillWidth: true
    implicitHeight: 37
    font.pixelSize: 13
    leftPadding: 11; rightPadding: 27
    palette.text: Theme.text; palette.buttonText: Theme.text; palette.base: Theme.surface
    contentItem: AppTextField {
        text: combo.editable ? combo.editText : combo.displayText
        font: combo.font
        enabled: combo.editable
        autoScroll: combo.editable
        readOnly: combo.down
        inputMethodHints: combo.inputMethodHints
        validator: combo.validator
        selectByMouse: combo.selectTextByMouse
        color: combo.enabled ? Theme.text : Theme.disabledText
        selectionColor: Theme.selection
        selectedTextColor: Theme.selectionText
        verticalAlignment: Text.AlignVCenter
        padding: 0
        // The outer control owns the complete border and background.
        background: null
    }
    background: Rectangle {
        radius: 6; color: combo.enabled ? Theme.surface : Theme.disabledSurface
        border.color: combo.activeFocus ? Theme.accent : Theme.border
    }
    indicator: Text {
        x: combo.width-width-12; anchors.verticalCenter: parent.verticalCenter
        text: "⌄"; color: combo.enabled ? Theme.secondary : Theme.disabledText; font.pixelSize: 18
    }
    delegate: ItemDelegate {
        id: option
        required property int index
        width: combo.width-8; height: 34
        highlighted: combo.highlightedIndex===index
        hoverEnabled: true
        contentItem: Text {
            text: combo.textAt(option.index); font: combo.font
            color: option.highlighted ? Theme.accentText : Theme.text
            elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 4
            color: option.highlighted || option.hovered ? Theme.accentSoft : Theme.surface
        }
    }
    popup: Popup {
        y: combo.height+4; width: combo.width; padding: 4
        implicitHeight: Math.min(portList.contentHeight+8,260)
        background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: 7 }
        contentItem: ListView {
            id: portList
            clip: true
            implicitHeight: contentHeight
            model: combo.popup.visible ? combo.delegateModel : null
            currentIndex: combo.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
}
