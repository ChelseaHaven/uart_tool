import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Uart.Ui 1.0

Item {
    id: menu
    objectName: "componentMenu"
    required property Item targetArea
    readonly property bool dragOver: tracker.hovering
    signal componentDropped(string type, point position)
    ComponentDragController {
        id: tracker; targetArea: menu.targetArea
        onDropped: function(type,position) { menu.componentDropped(type,position) }
    }
    property string dragTitle: ""
    component MenuTile: Rectangle {
        id: tile
        required property string componentType
        required property string title
        required property Component thumbnail
        Layout.fillWidth: true; implicitHeight: 154; radius: 8
        color: mouse.containsMouse?Theme.hover:Theme.panel
        border.color: tracker.active && menu.dragTitle===title?Theme.accent:Theme.border
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 14; spacing: 8
            Text { text: tile.title; font.pixelSize: 14; font.bold: true; color: Theme.accentText }
            Loader { sourceComponent: tile.thumbnail; Layout.fillWidth: true; Layout.fillHeight: true }
        }
        MouseArea {
            id: mouse; anchors.fill: parent; hoverEnabled: true; preventStealing: true
            cursorShape: pressed?Qt.ClosedHandCursor:Qt.OpenHandCursor
            property point pressedAt
            property bool started: false
            onPressed: function(event) { started=false; pressedAt=mapToGlobal(event.x,event.y) }
            onPositionChanged: function(event) {
                if(!pressed || started) return
                const position=mapToGlobal(event.x,event.y)
                if(Math.abs(position.x-pressedAt.x)+Math.abs(position.y-pressedAt.y)>12) {
                    started=true
                    menu.dragTitle=tile.title
                    tracker.begin(Window.window,tile.componentType,position)
                }
            }
            onReleased: function(event) { if(tracker.active) tracker.finish(mapToGlobal(event.x,event.y)) }
        }
    }
    ScrollView {
        id: list
        anchors.fill: parent; anchors.margins: 18; clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: list.availableWidth; spacing: 12
            Text { text: "将组件拖入“组件工作区”。"; color: Theme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            MenuTile {
                objectName: "timePlotComponent"; componentType: "time-plot"; title: "时序图"
                thumbnail: PlotPreview {}
            }
            MenuTile {
                objectName: "xyPlotComponent"; componentType: "xy-plot"; title: "XY 轨迹图"
                thumbnail: PlotPreview { trajectory: true }
            }
            MenuTile {
                objectName: "quickSendComponent"; componentType: "quick-send"; title: "快捷发送"
                thumbnail: Rectangle {
                    radius: 6; color: Theme.input; border.color: Theme.border
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 9; spacing: 6
                        Text {
                            Layout.fillWidth: true
                            text: "kp:1.2,ki:0.05,kd:0.01"; color: Theme.text
                            font.family: "monospace"; font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "\\n"; color: Theme.secondary; font.pixelSize: 11 }
                            Item { Layout.fillWidth: true }
                            Rectangle {
                                implicitWidth: 52; implicitHeight: 24; radius: 4; color: Theme.accent
                                Text { anchors.centerIn: parent; text: "发送"; color: Theme.contrastText; font.pixelSize: 11 }
                            }
                        }
                    }
                }
            }
        }
    }
    Window {
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
        transientParent: menu.Window.window; color: "transparent"
        visible: tracker.active; x: tracker.globalPosition.x+14; y: tracker.globalPosition.y+14
        width: 170; height: 60
        onVisibleChanged: { if(visible) raise() }
        Rectangle {
            anchors.fill: parent; anchors.margins: 1; radius: 8; color: Theme.surface; border.color: Theme.accent
            Text { anchors.centerIn: parent; text: "＋  "+menu.dragTitle; color: Theme.accentText; font.bold: true }
        }
    }
}
