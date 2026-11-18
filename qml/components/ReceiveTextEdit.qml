import QtQuick
import QtQuick.Controls

TextEdit {
    id: editor
    readonly property bool selecting: selectionPress.active
    readonly property bool hasSelection: selectionStart!==selectionEnd
    property bool copyMenuOpen: false

    readOnly: true
    selectByMouse: true
    persistentSelection: true
    selectionColor: Theme.selection
    selectedTextColor: Theme.selectionText
    wrapMode: TextEdit.WrapAnywhere
    textFormat: TextEdit.PlainText
    color: Theme.text
    font.family: "monospace"

    PointHandler {
        id: selectionPress
        acceptedButtons: Qt.LeftButton
    }
    ContextMenu.menu: Menu {
        onAboutToShow: editor.copyMenuOpen=true
        onClosed: editor.copyMenuOpen=false
        MenuItem {
            objectName: "copyReceiveText"
            text: "复制"
            enabled: editor.hasSelection
            onTriggered: editor.copy()
        }
    }
}
