import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel
    objectName: "receiveWorkspace"
    required property QtObject session
    required property QtObject protocol
    required property bool sourceConnected
    property bool showingRaw: true
    property alias autoScroll: rawView.autoScroll
    radius: 11; color: Theme.surface; border.color: Theme.border

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 18; spacing: 12
        HorizontalToolbar {
            objectName: "receiveToolbar"; Layout.fillWidth: true
            AppButton {
                objectName: "rawDataTab"
                text: "原始数据"; implicitWidth: 80; primary: panel.showingRaw
                onClicked: panel.showingRaw=true
            }
            AppButton {
                objectName: "protocolTab"
                text: "数据协议"; implicitWidth: 80; primary: !panel.showingRaw
                onClicked: panel.showingRaw=false
            }
            Item { Layout.fillWidth: true }
            AppButton {
                implicitWidth: 80; text: panel.session.displayPaused?"继续显示":"暂停显示"
                onClicked: {
                    const paused=!panel.session.displayPaused
                    panel.session.displayPaused=paused
                    panel.protocol.displayPaused=paused
                }
            }
            AppButton { text: "清空"; implicitWidth: 56; onClicked: panel.session.clear() }
        }
        HorizontalToolbar {
            objectName: "receiveActions"; Layout.fillWidth: true; spacing: 6
            visible: panel.showingRaw
            AppButton { text: "文本"; implicitWidth: 57; primary: !panel.session.hexMode; onClicked: panel.session.hexMode=false }
            AppButton { text: "HEX"; implicitWidth: 57; primary: panel.session.hexMode; onClicked: panel.session.hexMode=true }
            Item { Layout.fillWidth: true }
            CheckBox {
                objectName: "receiveTimestampToggle"
                text: "时间戳"; checked: panel.session.showTimestamps; font.pixelSize: 12
                onToggled: panel.session.showTimestamps=checked
            }
            CheckBox {
                text: "自动滚动"; checked: panel.autoScroll; font.pixelSize: 12
                onToggled: panel.autoScroll=checked
            }
        }
        Rectangle {
            visible: panel.session.displayPaused; Layout.fillWidth: true; implicitHeight: 31; radius: 5; color: Theme.infoSurface
            Text { anchors.centerIn: parent; text: "显示已暂停，仍在接收和解析；继续后显示最新数据。"; color: Theme.secondary; font.pixelSize: 11 }
        }
        RawReceivePanel {
            id: rawView; objectName: "rawReceivePage"
            visible: panel.showingRaw; Layout.fillWidth: true; Layout.fillHeight: true
            session: panel.session; sourceConnected: panel.sourceConnected
        }
        ProtocolPanel {
            id: protocolView; objectName: "protocolPage"
            visible: !panel.showingRaw; Layout.fillWidth: true; Layout.fillHeight: true
            protocol: panel.protocol
        }
    }
}
