import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Uart.Ui 1.0

Item {
    id: panel
    objectName: "sendPanel"
    required property QtObject sender
    required property int formatIndex
    readonly property bool tuning: formatIndex===2
    property string lastStatus: ""
    property bool lastFailed: false
    readonly property bool canSend: sender!==null && sender.connected
        && (tuning || input.length>0 || endingCombo.currentValue!=="none")
    readonly property bool intervalValid: intervalInput.acceptableInput
        && Number(intervalInput.text)>0 && Number(intervalInput.text)<=2147483647
    readonly property bool sendEnabled: sendTimer.running || (canSend && (!repeatToggle.checked || intervalValid))
    readonly property string sendLabel: sendTimer.running?"停止":"发送"
    function submit() {
        if(sendTimer.running) {
            sendTimer.stop()
            return
        }
        if(!sendEnabled || !sendOnce()) return
        if(repeatToggle.checked && sender.connected) sendTimer.start(Number(intervalInput.text))
    }
    function sendOnce() {
        if(!canSend) {
            if(sender && sender.connected) {
                lastStatus="请输入发送内容。"
                lastFailed=true
            }
            return false
        }
        let payload=input.text
        if(tuning) {
            const encoded=parameterEditor.encode()
            if(encoded.error) {
                lastStatus=encoded.error
                lastFailed=true
                return false
            }
            payload=encoded.text
        }
        const accepted=sender.send(payload,formatIndex===1,endingCombo.currentValue)
        lastStatus=sender.statusText
        lastFailed=sender.failed
        return accepted
    }
    SendTimer {
        id: sendTimer
        onTick: { if(!panel.sendOnce()) stop() }
    }
    onSenderChanged: sendTimer.stop()
    onFormatIndexChanged: {
        sendTimer.stop()
        panel.lastStatus=""
        panel.lastFailed=false
    }
    Connections {
        target: panel.sender
        function onConnectionChanged() {
            sendTimer.stop()
            panel.lastStatus=""
            panel.lastFailed=false
        }
    }
    ColumnLayout {
        anchors.fill: parent; spacing: 6
        ParameterSendEditor {
            id: parameterEditor
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: panel.tuning
            onSubmitRequested: panel.submit()
        }
        Rectangle {
            visible: !panel.tuning
            Layout.fillWidth: true; Layout.fillHeight: true
            color: Theme.input; border.color: input.activeFocus?Theme.accent:Theme.border; radius: 5
            ScrollView {
                anchors.fill: parent; anchors.margins: 1; clip: true
                TextArea {
                    id: input
                    objectName: "sendInput"
                    textFormat: TextEdit.PlainText; wrapMode: TextEdit.WrapAnywhere; selectByMouse: true
                    placeholderText: panel.formatIndex===1?"AA 01 FF":"输入要发送的内容"
                    color: Theme.text; selectionColor: Theme.selection; selectedTextColor: Theme.selectionText
                    font.family: "monospace"; font.pixelSize: 13; padding: 8
                    background: null
                    Keys.onPressed: function(event) {
                        if((event.key===Qt.Key_Return || event.key===Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                            panel.submit()
                            event.accepted=true
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            AppButton {
                objectName: "addSendParameter"
                visible: panel.tuning
                text: "+ 参数"; implicitWidth: 56; implicitHeight: 27
                enabled: parameterEditor.canAddParameter
                onClicked: parameterEditor.addParameter()
            }
            Item { Layout.fillWidth: true }
            SendEndingCombo {
                id: endingCombo
                Layout.preferredWidth: 80
                implicitHeight: 27
                currentIndex: 1
            }
            CheckBox {
                id: repeatToggle
                objectName: "timedSendToggle"
                text: "定时发送"; padding: 0; spacing: 4; font.pixelSize: 12
                enabled: !sendTimer.running
            }
            TextField {
                id: intervalInput
                objectName: "timedSendInterval"
                Layout.preferredWidth: 52
                implicitHeight: 27
                leftPadding: 5; rightPadding: 5
                text: "1000"
                Accessible.name: "发送间隔（毫秒）"
                enabled: !sendTimer.running
                selectByMouse: true
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: 2147483647 }
            }
            Text { text: "ms"; color: Theme.secondary; font.pixelSize: 12 }
            AppButton {
                objectName: "sendButton"
                text: panel.sendLabel; primary: true; implicitWidth: 54; implicitHeight: 27
                enabled: panel.sendEnabled
                onClicked: panel.submit()
            }
        }
        Text {
            Layout.fillWidth: true; font.pixelSize: 11; elide: Text.ElideRight
            visible: text.length>0
            text: panel.lastStatus
            color: panel.lastFailed?Theme.error:Theme.muted
            HoverHandler { id: statusHover }
            ToolTip.visible: statusHover.hovered; ToolTip.text: text
        }
    }
}
