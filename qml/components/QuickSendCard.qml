import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ComponentFrame {
    id: card
    required property QtObject sender
    required property int instanceId
    property string commandName: ""
    property bool editingName: false
    signal renameRequested(string name)
    objectName: "quickSendCard_"+instanceId
    title: commandName || "快捷发送"
    titleEditor: editingName ? nameEditor : null

    function finishRenaming(save) {
        if (save && titleEditorItem)
            renameRequested(titleEditorItem.text.trim())
        editingName=false
        forceActiveFocus()
    }

    headerActions: [
        AppButton {
            id: renameButton
            objectName: "renameQuickSend"
            text: card.editingName ? "确定" : "命名"
            implicitWidth: 50; implicitHeight: 29
            onClicked: {
                if (card.editingName) card.finishRenaming(true)
                else card.editingName=true
            }
        },
        ConfigCombo {
            id: format
            objectName: "sendMode"
            Accessible.name: "发送格式"
            Layout.fillWidth: false
            Layout.preferredWidth: 86
            implicitHeight: 29
            model: ["文本","HEX","字典"]
            currentIndex: 0
        }
    ]

    SendPanel {
        id: sendEditor
        anchors.fill: parent
        sender: card.sender
        formatIndex: format.currentIndex
    }

    function snapshotSend() { return sendEditor.snapshot() }
    function restoreSend(config) {
        format.currentIndex=config.mode
        sendEditor.restore(config)
    }

    Component {
        id: nameEditor
        AppTextField {
            id: nameInput
            objectName: "quickSendName"
            text: card.commandName
            placeholderText: "输入名称"
            Accessible.name: "指令名称"
            maximumLength: 40
            selectByMouse: true
            padding: 5
            background: Rectangle {
                color: Theme.input
                border.color: nameInput.activeFocus ? Theme.accent : Theme.border
                radius: 5
            }
            Component.onCompleted: {
                forceActiveFocus()
                selectAll()
            }
            onAccepted: card.finishRenaming(true)
            Keys.onEscapePressed: card.finishRenaming(false)
        }
    }
}
