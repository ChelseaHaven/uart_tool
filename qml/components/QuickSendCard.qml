import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ComponentFrame {
    id: card
    required property QtObject sender
    required property int instanceId
    property string commandName: ""
    objectName: "quickSendCard_"+instanceId
    title: commandName || "快捷发送"

    headerActions: [
        AppButton {
            id: renameButton
            objectName: "renameQuickSend"
            text: "命名"; implicitWidth: 50; implicitHeight: 29
            onClicked: renamePopup.open()
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
        anchors.fill: parent
        sender: card.sender
        formatIndex: format.currentIndex
    }

    Popup {
        id: renamePopup
        objectName: "quickSendRenamePopup"
        parent: card
        width: Math.min(258,card.width-16)
        x: Math.max(8,card.width-width-8); y: 42
        padding: 10
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onAboutToShow: nameInput.text=card.commandName
        onOpened: { nameInput.forceActiveFocus(); nameInput.selectAll() }
        function applyName() {
            card.commandName=nameInput.text.trim()
            close()
        }

        background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: 7 }
        contentItem: RowLayout {
            spacing: 8
            TextField {
                id: nameInput
                objectName: "quickSendName"
                focus: true
                Layout.fillWidth: true
                placeholderText: "例如 轮速 PID"
                Accessible.name: "指令名称"
                maximumLength: 40
                color: Theme.text
                selectByMouse: true
                onAccepted: renamePopup.applyName()
            }
            AppButton {
                text: "确定"; implicitWidth: 54; implicitHeight: 32
                onClicked: renamePopup.applyName()
            }
        }
    }
}
