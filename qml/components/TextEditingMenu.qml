import QtQuick
import QtQuick.Controls

Menu {
    id: menu
    required property Item editor
    property string extraActionText: ""
    signal extraActionRequested()

    objectName: "textEditingMenu"
    parent: editor
    popupType: Popup.Item
    width: 156
    padding: 4
    background: Rectangle {
        color: Theme.surface
        border.color: Theme.border
        radius: 7
    }

    // The editor owns right-clicks even when it sits inside a component card.
    property Item rightClickArea: MouseArea {
        parent: menu.editor
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        cursorShape: Qt.IBeamCursor
        onPressed: menu.editor.forceActiveFocus(Qt.MouseFocusReason)
        onClicked: function(mouse) {
            menu.x=mouse.x
            menu.y=mouse.y
            menu.open()
        }
    }

    component EditItem: MenuItem {
        icon.width: 16
        icon.height: 16
        icon.color: enabled ? Theme.text : Theme.disabledText
    }

    EditItem {
        text: "撤销"
        icon.source: "../icons/edit-undo.svg"
        enabled: !menu.editor.readOnly && menu.editor.canUndo
        onTriggered: menu.editor.undo()
    }
    EditItem {
        text: "重做"
        icon.source: "../icons/edit-redo.svg"
        enabled: !menu.editor.readOnly && menu.editor.canRedo
        onTriggered: menu.editor.redo()
    }
    MenuSeparator {}
    EditItem {
        objectName: "cutInputText"
        text: "剪切"
        icon.source: "../icons/edit-cut.svg"
        enabled: !menu.editor.readOnly && menu.editor.selectedText.length > 0
        onTriggered: menu.editor.cut()
    }
    EditItem {
        objectName: "copyInputText"
        text: "复制"
        icon.source: "../icons/edit-copy.svg"
        enabled: menu.editor.selectedText.length > 0
        onTriggered: menu.editor.copy()
    }
    EditItem {
        objectName: "pasteInputText"
        text: "粘贴"
        icon.source: "../icons/edit-paste.svg"
        enabled: !menu.editor.readOnly && menu.editor.canPaste
        onTriggered: menu.editor.paste()
    }
    EditItem {
        objectName: "deleteInputText"
        text: "删除"
        icon.source: "../icons/edit-delete.svg"
        enabled: !menu.editor.readOnly && menu.editor.selectedText.length > 0
        onTriggered: menu.editor.remove(menu.editor.selectionStart, menu.editor.selectionEnd)
    }
    MenuSeparator {}
    EditItem {
        objectName: "selectAllInputText"
        text: "全选"
        icon.source: "../icons/edit-select-all.svg"
        enabled: menu.editor.length > 0
        onTriggered: menu.editor.selectAll()
    }
    MenuSeparator {
        visible: menu.extraActionText.length > 0
        height: visible ? implicitHeight : 0
    }
    EditItem {
        objectName: "extraInputAction"
        text: menu.extraActionText
        icon.source: "../icons/settings.svg"
        visible: text.length > 0
        height: visible ? implicitHeight : 0
        onTriggered: Qt.callLater(menu.extraActionRequested)
    }
}
