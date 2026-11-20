import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"

ApplicationWindow {
    id: root
    width: 1200; height: 900
    minimumWidth: 1044; minimumHeight: 760
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinMaxButtonsHint | Qt.WindowCloseButtonHint
    visible: true; title: "UART Tool · 串口接收"
    color: Theme.window
    font.family: "Noto Sans CJK SC"
    font.pixelSize: 13
    Binding { target: Theme; property: "dark"; value: appSettings.darkMode }
    palette.window: Theme.surface; palette.windowText: Theme.text
    palette.text: Theme.text; palette.buttonText: Theme.text
    palette.base: Theme.input; palette.alternateBase: Theme.panel; palette.button: Theme.surface
    palette.highlight: Theme.selection; palette.highlightedText: Theme.selectionText
    palette.placeholderText: Theme.muted; palette.mid: Theme.border
    palette.light: Theme.border; palette.dark: Theme.divider; palette.shadow: Theme.divider
    palette.disabled.text: Theme.disabledText; palette.disabled.buttonText: Theme.disabledText
    palette.disabled.windowText: Theme.disabledText; palette.disabled.base: Theme.disabledSurface
    onClosing: {
        componentArea.saveLayout()
        const layout = dockLayout.snapshotLayout()
        layout.leftPage = root.leftPage
        appSettings.saveDockLayout(layout)
        serialDock.shutdown(); componentDock.shutdown(); receiveDock.shutdown()
    }
    property bool settingsOpen: false
    readonly property var savedDockLayout: appSettings.loadDockLayout()
    property int leftPage: savedDockLayout.leftPage === 1 ? 1 : 0
    readonly property var dockPanels: [serialDock,componentDock,receiveDock]
    readonly property var plotSeries: typeof numericStream!=="undefined"?numericStream:null
    readonly property var sender: typeof sendSession!=="undefined"?sendSession:null
    readonly property var recorder: typeof csvRecorder!=="undefined"?csvRecorder:null
    property alias autoScroll: receivePanel.autoScroll
    readonly property string validationError: serialPanel.validationError

    DockOverlay {
        ownerWindow: root; targetArea: dockWorkspace
        activePanel: receiveDock.dragging?receiveDock:componentDock.dragging?componentDock:serialDock.dragging?serialDock:null
    }

    WindowResizeHandles {
        ownerWindow: root
        namePrefix: "main"
        visible: root.visibility === Window.Windowed
    }

    ColumnLayout {
        anchors.fill: parent; spacing: 0
        MainTitleBar {
            Layout.fillWidth: true
            ownerWindow: root
            actions: [
                RecordingControls { recorder: root.recorder },
                TitleActionButton {
                    objectName: "settingsButton"
                    icon.source: "icons/settings.svg"
                    text: root.settingsOpen ? "返回接收" : "设置"
                    onClicked: root.settingsOpen = !root.settingsOpen
                },
                Rectangle {
                    implicitWidth: statusLabel.implicitWidth + 24
                    implicitHeight: 28
                    radius: 14
                    color: serialConnection.connected ? Theme.successSurface : Theme.disabledSurface
                    Text {
                        id: statusLabel
                        anchors.centerIn: parent
                        text: serialConnection.connected ? "●  串口已连接" : "●  尚未连接"
                        color: serialConnection.connected ? Theme.success : Theme.muted
                        font.pixelSize: 11
                    }
                }
            ]
        }
        Item {
            id: dockWorkspace; objectName: "dockWorkspace"
            visible: !root.settingsOpen
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 20
            DockLayout {
                id: dockLayout; anchors.fill: parent
                initialLayout: root.savedDockLayout
                firstPanel: serialDock; secondPanel: componentDock; thirdPanel: receiveDock
            }
            DockPanel {
                id: serialDock; panelKey: "connection"; fixedSidebar: true; minimumDockWidth: sidebarWidth; title: root.leftPage===0?"协议与连接":"组件菜单"
                ownerWindow: root; dockArea: dockWorkspace; dockPanels: root.dockPanels; workspaceActive: !root.settingsOpen
                onDockPlacementRequested: function(direction,targetPanel,placement) { dockLayout.placePanel(serialDock,direction,targetPanel,placement) }
                SplitView.preferredWidth: 260; SplitView.minimumWidth: minimumDockWidth
                SplitView.preferredHeight: 260; SplitView.minimumHeight: minimumDockHeight

                ColumnLayout {
                    anchors.fill: parent; spacing: 0
                    RowLayout {
                        Layout.fillWidth: true; Layout.margins: 12; spacing: 8
                        AppButton { objectName: "connectionMenuTab"; text: "连接"; primary: root.leftPage===0; Layout.fillWidth: true; implicitHeight: 30; onClicked: root.leftPage=0 }
                        AppButton { objectName: "componentMenuTab"; text: "组件"; primary: root.leftPage===1; Layout.fillWidth: true; implicitHeight: 30; onClicked: root.leftPage=1 }
                    }
                    Item {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        SerialConfigPanel {
                            id: serialPanel; anchors.fill: parent; connection: serialConnection; visible: root.leftPage===0
                            showHeader: false; color: "transparent"; border.width: 0
                        }
                        ComponentMenu {
                            id: componentMenu; anchors.fill: parent; visible: root.leftPage===1; targetArea: componentArea
                            onComponentDropped: function(type,position) { componentArea.addWidget(type,position) }
                        }
                    }
                }
            }
            DockPanel {
                id: componentDock; panelKey: "components"; title: "组件工作区"
                ownerWindow: root; dockArea: dockWorkspace; dockPanels: root.dockPanels; workspaceActive: !root.settingsOpen
                minimumFloatWidth: 380; minimumFloatHeight: 300
                onDockPlacementRequested: function(direction,targetPanel,placement) { dockLayout.placePanel(componentDock,direction,targetPanel,placement) }
                minimumDockWidth: 340
                SplitView.minimumWidth: minimumDockWidth
                minimumDockHeight: 220
                SplitView.preferredHeight: 330; SplitView.minimumHeight: minimumDockHeight
                ComponentWorkspace {
                    id: componentArea; anchors.fill: parent; anchors.margins: 10
                    series: root.plotSeries; protocol: protocolSession; sender: root.sender; dragHighlight: componentMenu.dragOver
                    layouts: appSettings.componentLayouts
                }
            }
            DockPanel {
                id: receiveDock; panelKey: "receive"; title: "数据接收"
                ownerWindow: root; dockArea: dockWorkspace; dockPanels: root.dockPanels; workspaceActive: !root.settingsOpen
                minimumFloatWidth: 380; minimumFloatHeight: 38
                onDockPlacementRequested: function(direction,targetPanel,placement) { dockLayout.placePanel(receiveDock,direction,targetPanel,placement) }
                minimumDockWidth: 380
                SplitView.minimumWidth: minimumDockWidth
                minimumDockHeight: 38
                SplitView.preferredHeight: 330; SplitView.minimumHeight: minimumDockHeight
                ReceivePanel {
                    id: receivePanel; anchors.fill: parent
                    session: receiveSession; protocol: protocolSession
                    sourceConnected: serialConnection.connected
                    color: "transparent"; border.width: 0
                }
            }
            Column {
                visible: serialDock.floating && componentDock.floating && receiveDock.floating
                anchors.centerIn: parent; spacing: 16
                Text { text: "面板已浮动，可拖回窗口边缘吸附"; color: Theme.secondary }
                AppButton { anchors.horizontalCenter: parent.horizontalCenter; text: "全部吸附"; onClicked: { serialDock.dock(); componentDock.dock(); receiveDock.dock() } }
            }

        }
        SettingsPage {
            objectName: "settingsPage"
            visible: root.settingsOpen
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 20
            preferences: appSettings
            onResetLayoutRequested: {
                dockLayout.restoreLayout({})
                root.leftPage = 0
                appSettings.saveDockLayout({})
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: errorLabel.visible ? errorLabel.implicitHeight+18 : 0
            visible: errorLabel.visible; color: Theme.errorSurface
            Text {
                id: errorLabel; width: parent.width-44; x: 22; y: 9
                visible: text.length>0; wrapMode: Text.WordWrap
                text: root.validationError || serialConnection.errorText; color: Theme.error; font.pixelSize: 12
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 35; color: Theme.surface
            Rectangle { width: parent.width; height: 1; color: Theme.divider }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 22; anchors.rightMargin: 22
                Text { text: serialConnection.connected ? "● 已连接  " + serialConnection.activePort : serialConnection.activePort ? "○ 已断开  " + serialConnection.activePort : "○ 未连接"; color: serialConnection.connected ? Theme.success : Theme.muted; font.pixelSize: 11 }
                Item { Layout.fillWidth: true }
                Text { text: "累计接收  " + Number(receiveSession.receivedBytes).toLocaleString(Qt.locale("en_US"),'f',0) + " B"; color: Theme.secondary; font.pixelSize: 11 }
                Text { text: "   RX / TX"; color: Theme.muted; font.pixelSize: 10; font.letterSpacing: 1 }
            }
        }
    }
}
