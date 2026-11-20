import QtQuick
import QtQuick.Controls

TextField {
    id: field
    property string editingActionText: ""
    signal editingActionRequested()
    persistentSelection: true

    ContextMenu.menu: null
    TextEditingMenu {
        editor: field
        extraActionText: field.editingActionText
        onExtraActionRequested: field.editingActionRequested()
    }
}
