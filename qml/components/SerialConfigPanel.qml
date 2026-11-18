import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel
    required property QtObject connection
    property bool showHeader: true
    property string validationError: ""
    property string preferredPort: ""
                Layout.preferredWidth: 260; Layout.fillHeight: true
                radius: 11; color: Theme.surface; border.color: Theme.border
                ScrollView {
                    id: configScroll
                    anchors.fill: parent; anchors.margins: 19; clip: true
                    contentWidth: availableWidth; contentHeight: configColumn.implicitHeight
                    ColumnLayout {
                        id: configColumn; width: configScroll.availableWidth; spacing: 10
                        Text { visible: panel.showHeader; text: "协议与连接"; color: Theme.accentText; font.pixelSize: 16; font.bold: true }
                        Text { visible: panel.showHeader; text: "原始串口数据接收"; color: Theme.muted; font.pixelSize: 11 }
                        Rectangle { visible: panel.showHeader; Layout.fillWidth: true; height: 1; color: Theme.divider; Layout.topMargin: 3; Layout.bottomMargin: 4 }
                        RowLayout {
                            Layout.fillWidth: true
                            FieldLabel { text: "端口" }
                            Item { Layout.fillWidth: true }
                            ToolButton { text: "↻ 刷新"; enabled: !panel.connection.connected; onClicked: panel.connection.refreshPorts(); implicitHeight: 27; font.pixelSize: 11 }
                        }
                        ConfigCombo {
                            id: portCombo; objectName: "portCombo"; model: panel.connection.ports; textRole: "label"; valueRole: "path"
                            enabled: !panel.connection.connected && count>0
                            displayText: count>0 ? currentText : "未发现可用串口"
                            onActivated: panel.preferredPort=currentValue
                        }
                        FieldLabel { text: "波特率" }
                        ConfigCombo {
                            id: baudCombo; objectName: "baudCombo"; model: ["9600","19200","38400","57600","115200","230400","460800","921600"]
                            currentIndex: 4; editable: true; enabled: !panel.connection.connected
                            validator: IntValidator { bottom: 1; top: 2147483647 }
                        }
                        GridLayout {
                            columns: 2; columnSpacing: 12; rowSpacing: 8; Layout.fillWidth: true
                            FieldLabel { text: "数据位" }
                            FieldLabel { text: "校验位" }
                            ConfigCombo { id: bitsCombo; model: [5,6,7,8]; currentIndex: 3; enabled: !panel.connection.connected }
                            ConfigCombo {
                                id: parityCombo; enabled: !panel.connection.connected
                                model: [{label:"无",value:0},{label:"偶校验",value:2},{label:"奇校验",value:3},{label:"空格",value:4},{label:"标记",value:5}]
                                textRole: "label"; valueRole: "value"
                            }
                            FieldLabel { text: "停止位" }
                            FieldLabel { text: "流控" }
                            ConfigCombo { id: stopsCombo; model: [1,2]; enabled: !panel.connection.connected }
                            ConfigCombo {
                                id: flowCombo; enabled: !panel.connection.connected
                                model: [{label:"无",value:0},{label:"RTS/CTS",value:1},{label:"XON/XOFF",value:2}]
                                textRole: "label"; valueRole: "value"
                            }
                        }
                        AppButton {
                            Layout.fillWidth: true; Layout.topMargin: 10
                            objectName: "connectionButton"
                            primary: true; text: panel.connection.connected ? "断开连接" : "连接串口"
                            enabled: panel.connection.connected || portCombo.count>0
                            onClicked: {
                                if(panel.connection.connected) { panel.connection.disconnectPort(); return }
                                const value=baudCombo.editText.trim()
                                if(!/^[0-9]+$/.test(value) || Number(value)<1 || Number(value)>2147483647) {
                                    panel.validationError="波特率必须是 1～2147483647 的正整数。"; return
                                }
                                panel.validationError=""
                                panel.connection.openPort(portCombo.currentValue,Number(value),Number(bitsCombo.currentText),parityCombo.currentValue,Number(stopsCombo.currentText),flowCombo.currentValue)
                            }
                        }
                        Text {
                            Layout.fillWidth: true; Layout.topMargin: 6; wrapMode: Text.WordWrap
                            text: panel.connection.connected ? "连接期间参数已锁定。\n先断开连接，再调整串口设置。" : "连接设备后点击刷新。\n默认参数：115200 · 8N1"
                            color: Theme.muted; font.pixelSize: 11; lineHeight: 1.4
                        }
                    }
                }
                Connections {
        target: panel.connection
        function onPortsChanged() {
            Qt.callLater(function() {
                let selected=0
                for(let i=0;i<portCombo.count;i++) {
                    if(portCombo.valueAt(i)===panel.preferredPort) { selected=i; break }
                }
                portCombo.currentIndex=portCombo.count>0?selected:-1
            })
        }
    }
}
