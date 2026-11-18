import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: panel
    required property QtObject protocol
    readonly property bool binarySelected: protocol.selectedProtocolId==="binary"
    readonly property bool hasProtocol: protocol.selectedProtocolId!=="none"
    ListModel { id: displayRows }
    function updateRows() {
        const values=panel.protocol.latestFields
        for(let i=0;i<values.length;i++) {
            const value=values[i]
            const row={fieldName:value.name,fieldType:value.type,elementCount:Number(value.elementCount),byteCount:Number(value.byteCount),displayValue:value.valueText}
            if(i>=displayRows.count) displayRows.append(row)
            else for(const key in row) if(displayRows.get(i)[key]!==row[key]) displayRows.setProperty(i,key,row[key])
        }
        if(displayRows.count>values.length) displayRows.remove(values.length,displayRows.count-values.length)
    }
    onVisibleChanged: { if(visible) updateRows() }
    Component.onCompleted: { if(visible) updateRows() }
    ColumnLayout {
        anchors.fill: parent; spacing: 12
        RowLayout {
            objectName: "protocolToolbar"; Layout.fillWidth: true
            spacing: 8
            ProtocolSelector {
                id: protocolCombo
                protocol: panel.protocol
                Layout.preferredWidth: commandSelector.visible
                    ? Math.max(128,Math.min(200,panel.width*0.42))
                    : Math.max(140,Math.min(200,panel.width-105))
            }
            BinaryCommandSelector {
                id: commandSelector
                leftPadding: protocolCombo.leftPadding
                protocol: panel.protocol
                visible: panel.binarySelected && hasCommands
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: protocolCombo.width
                Layout.maximumWidth: protocolCombo.width
            }
            Item { Layout.fillWidth: true; Layout.preferredWidth: 0 }
            AppButton {
                objectName: "editBinaryProtocol"
                visible: panel.binarySelected
                text: "编辑协议"; onClicked: editor.open()
            }
        }
        Text {
            visible: panel.hasProtocol && text.length>0
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            text: protocolCombo.errorText || panel.protocol.lastError
            color: Theme.error; font.pixelSize: 11
        }
        Rectangle {
            objectName: "protocolResults"
            Layout.fillWidth: true; Layout.fillHeight: true
            color: Theme.input; radius: 7; border.color: Theme.border
            Column {
                anchors.centerIn: parent; width: parent.width-40; spacing: 13
                visible: panel.protocol.latestFields.length===0
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "≡"; color: Theme.accentSoft; font.pixelSize: 42 }
                Text { objectName: "protocolStatus"; width: parent.width; horizontalAlignment: Text.AlignHCenter; text: panel.protocol.statusText; color: Theme.secondary; font.pixelSize: 18 }
                Text { visible: !panel.hasProtocol; width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: "从上方选择要使用的协议。"; color: Theme.muted; font.pixelSize: 12 }
            }
            ListView {
                id: fieldsView
                anchors.fill: parent; anchors.margins: 14; clip: true; spacing: 8
                visible: panel.protocol.latestFields.length>0
                model: displayRows
                acceptedButtons: Qt.NoButton
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }
                delegate: Rectangle {
                    id: fieldRow
                    required property string fieldName
                    required property string fieldType
                    required property int elementCount
                    required property int byteCount
                    required property string displayValue
                    width: Math.max(0,ListView.view.width-14); height: Math.max(70,valueText.implicitHeight+42)
                    radius: 6; color: Theme.surface; border.color: Theme.border
                    Column {
                        anchors.fill: parent; anchors.margins: 11; spacing: 6
                        Text { text: fieldRow.fieldName+"   ·   "+fieldRow.fieldType+"   ·   "+fieldRow.elementCount+" 项 / "+fieldRow.byteCount+" 字节"; color: Theme.secondary; font.pixelSize: 11 }
                        ReceiveTextEdit {
                            id: valueText; objectName: "decodedValue"
                            width: parent.width
                            font.pixelSize: 14
                            Binding {
                                target: valueText; property: "text"
                                value: fieldRow.displayValue
                                // Keep the selected value stable while frames continue arriving.
                                when: !valueText.selecting && (!valueText.hasSelection
                                    || (!valueText.activeFocus && !valueText.copyMenuOpen))
                                restoreMode: Binding.RestoreNone
                            }
                        }
                    }
                }
            }
        }
        RowLayout {
            visible: panel.hasProtocol; Layout.fillWidth: true
            Text { text: "有效帧  "+panel.protocol.frameCount; color: Theme.success }
            Text { text: "    错误帧  "+panel.protocol.invalidFrames; color: Theme.warning }
            Text { text: "    丢弃字节  "+panel.protocol.discardedBytes; color: Theme.muted }
            Item { Layout.fillWidth: true }
            Text { text: panel.protocol.lastFrameTime; color: Theme.muted; font.pixelSize: 11 }
        }
    }
    BinaryProtocolEditor { id: editor; objectName: "binaryProtocolEditor"; protocol: panel.protocol; popupLayer: panel.Window.window?panel.Window.window.Overlay.overlay:null }
    Connections {
        target: panel.protocol
        // Rows update in place. Leave contentY entirely to the user's scrolling
        // and Flickable's inertia instead of restoring it on every data refresh.
        function onLatestFrameChanged() { if(panel.visible) panel.updateRows() }
    }
}
