import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Uart.Ui 1.0

Item {
    id: panel
    required property string panelKey
    required property string title
    required property var ownerWindow
    required property Item dockArea
    property var dockPanels: []
    property bool fixedSidebar: false
    property int sidebarWidth: 260
    property bool sidebarCollapsed: false
    readonly property int effectiveSidebarWidth: sidebarCollapsed && !floating?0:sidebarWidth
    readonly property int sidebarDividerWidth: 12
    property string sidebarEdge: "left"
    property int fixedFloatHeight: 420
    property bool workspaceActive: true
    property bool floating: false
    property bool draggingFromDock: false
    property rect capturedHeader: Qt.rect(0,0,0,34)
    property bool shuttingDown: false
    property int minimumDockWidth: 240
    property int minimumDockHeight: 180
    property int minimumFloatWidth: 260
    property int minimumFloatHeight: 420
    readonly property rect guideRect: tracker.guideRect
    readonly property rect previewRect: tracker.previewRect
    readonly property Item dockTarget: tracker.dockTarget
    readonly property bool dockReady: tracker.dockReady
    readonly property string dockEdge: tracker.dockEdge
    readonly property bool dragging: tracker.dragging
    readonly property bool guidesVisible: tracker.guidesVisible
    signal dockPlacementRequested(string direction, var targetPanel, rect placement)
    default property alias content: contentBody.data
    visible: !floating
    objectName: panelKey+"Dock"

    function undock() { createFloating(false) }
    function createFloating(forDrag,position,offset) {
        if(floating) return
        if(fixedSidebar) sidebarCollapsed=false
        const location=frame.mapToGlobal(0,0)
        if(fixedSidebar) fixedFloatHeight=Math.max(minimumFloatHeight,height)
        floatWindow.width=fixedSidebar?sidebarWidth:Math.max(minimumFloatWidth,width)
        floatWindow.height=fixedSidebar?fixedFloatHeight:Math.max(minimumFloatHeight,height)
        // Set final geometry before mapping the window; never show at a
        // temporary position and then jump beneath the source cursor.
        floatWindow.x=forDrag?position.x-offset.x:location.x+32
        floatWindow.y=forDrag?position.y-offset.y:location.y+32
        floating=true
        if(!forDrag) { floatWindow.raise(); floatWindow.requestActivate() }
    }
    function floatingGeometry() {
        return {x: floatWindow.x, y: floatWindow.y, width: floatWindow.width, height: floatWindow.height}
    }
    function restoreFloatingGeometry(geometry) {
        if (![geometry.x, geometry.y, geometry.width, geometry.height].every(Number.isFinite)) return

        const screens = Array.from(Qt.application.screens)
        const screen = screens.find(candidate => geometry.x >= candidate.virtualX
                                   && geometry.x < candidate.virtualX + candidate.width
                                   && geometry.y >= candidate.virtualY
                                   && geometry.y < candidate.virtualY + candidate.height) || ownerWindow.screen
        if (!screen) return

        if (fixedSidebar) {
            sidebarCollapsed=false
            fixedFloatHeight=Math.max(minimumFloatHeight,Math.min(geometry.height,screen.height))
        }
        floatWindow.width=fixedSidebar ? sidebarWidth : Math.max(minimumFloatWidth,Math.min(geometry.width,screen.width))
        floatWindow.height=fixedSidebar ? fixedFloatHeight : Math.max(minimumFloatHeight,Math.min(geometry.height,screen.height))
        floatWindow.x=Math.max(screen.virtualX,Math.min(geometry.x,screen.virtualX+screen.width-floatWindow.width))
        floatWindow.y=Math.max(screen.virtualY,Math.min(geometry.y,screen.virtualY+screen.height-floatWindow.height))
        floating=true
    }
    function dock(direction,targetPanel,placement) {
        if(fixedSidebar) {
            if(direction && direction!=="left" && direction!=="right") return
            sidebarCollapsed=false
            if(direction) { sidebarEdge=direction; targetPanel=null }
        }
        // Closing simply reveals the retained dock position. Only an actual
        // drag/drop request carries a direction and needs to rearrange layout.
        if(direction) dockPlacementRequested(direction,targetPanel,placement)
        floating=false
        ownerWindow.requestActivate()
    }
    function detachForDrag(globalPosition,offset,sourceWindow) {
        let dragOffset=offset
        if(!floating) {
            const origin=panel.mapToItem(ownerWindow.contentItem,0,0)
            capturedHeader=Qt.rect(origin.x,origin.y,panel.width,34)
            draggingFromDock=true
            createFloating(true,globalPosition,offset)
        } else {
            if(floatWindow.visibility===Window.Maximized) {
                const horizontalRatio=(globalPosition.x-floatWindow.x)/floatWindow.width
                floatWindow.showNormal()
                floatWindow.x=globalPosition.x-Math.min(floatWindow.width-116,horizontalRatio*floatWindow.width)
                floatWindow.y=globalPosition.y-offset.y
            }
            dragOffset=Qt.point(globalPosition.x-floatWindow.x,globalPosition.y-floatWindow.y)
        }
        tracker.beginDrag(globalPosition,dragOffset,sourceWindow)
    }
    function toggleMaximized() {
        if(fixedSidebar) return
        if(floatWindow.visibility===Window.Maximized) floatWindow.showNormal()
        else floatWindow.showMaximized()
    }
    function shutdown() { shuttingDown=true }

    property QtObject movement: DockWindowController {
        id: tracker; objectName: panel.panelKey+"DockController"
        floatingWindow: floatWindow; dockArea: panel.dockArea; dockPanels: panel.dockPanels; dockPanel: panel
        enabled: panel.floating
        onDraggingChanged: { if(!dragging) panel.draggingFromDock=false }
        onDockRequested: function(edge,target,placement) { panel.dock(edge,target,placement) }
    }
    property Item frameItem: Rectangle {
        id: frame
        visible: !panel.fixedSidebar || !panel.sidebarCollapsed || panel.floating
        parent: panel.floating?floatWindow.contentItem:panel
        anchors.fill: parent
        radius: panel.floating?0:11; color: Theme.surface; border.color: Theme.border
        Item {
            id: handle; x: 1; y: 1; width: parent.width-2; height: 34
            Text {
                x: 15; anchors.verticalCenter: parent.verticalCenter
                width: panel.floating?parent.width-(panel.fixedSidebar?96:130):parent.width-25; elide: Text.ElideRight
                text: "⠿  "+panel.title+(panel.floating?" · UART Tool":""); color: Theme.secondary; font.pixelSize: 12; font.bold: true
            }

            Row {
                visible: panel.floating; anchors.right: parent.right; anchors.rightMargin: 6; height: 34
                ToolButton {
                    width: 34; height: 32; text: "−"; Accessible.name: "最小化窗口"
                    onClicked: floatWindow.showMinimized()
                }
                ToolButton {
                    objectName: panel.panelKey+"MaximizeButton"
                    visible: !panel.fixedSidebar
                    width: 34; height: 32; text: floatWindow.visibility===Window.Maximized?"❐":"□"
                    Accessible.name: "最大化或还原窗口"
                    onClicked: panel.toggleMaximized()
                }
                ToolButton {
                    objectName: panel.panelKey+"CloseButton"
                    width: 34; height: 32; text: "×"; Accessible.name: "关闭并吸附面板"
                    onClicked: panel.dock()
                }
            }
        }
        Rectangle { x: 12; y: handle.height+1; width: parent.width-24; height: 1; color: Theme.divider }
        Item {
            id: contentBody; objectName: panel.panelKey+"DockContent"
            clip: true; visible: height>2
            anchors.left: parent.left; anchors.right: parent.right
            anchors.top: handle.bottom; anchors.bottom: parent.bottom
            anchors.margins: 1
        }
    }
    component DragStrip: MouseArea {
        height: 34; z: 20
        cursorShape: pressed?Qt.ClosedHandCursor:Qt.OpenHandCursor
        property point pressedAt
        property point windowOffset
        onPressed: function(mouse) {
            pressedAt=mapToGlobal(mouse.x,mouse.y)
            windowOffset=Qt.point(mouse.x,mouse.y)
        }
        onPositionChanged: function(mouse) {
            if(!pressed || tracker.dragging) return
            const location=mapToGlobal(mouse.x,mouse.y)
            if(Math.abs(location.x-pressedAt.x)+Math.abs(location.y-pressedAt.y)>=12)
                panel.detachForDrag(location,windowOffset,Window.window)
        }
        onReleased: function(mouse) { if(tracker.dragging) tracker.endDrag(mapToGlobal(mouse.x,mouse.y)) }
        onDoubleClicked: { if(panel.floating) panel.toggleMaximized() }
    }
    // These input strips never move between windows, so their press/grab survives
    // moving the visual frame and its live content to the detached window.
    property Item dockedInput: DragStrip {
        // Reparent only within the original window before hiding its panel.
        // The native mouse capture and input strip survive the visual detach.
        parent: panel.draggingFromDock?panel.ownerWindow.contentItem:panel
        x: panel.draggingFromDock?panel.capturedHeader.x:0
        y: panel.draggingFromDock?panel.capturedHeader.y:0
        width: panel.draggingFromDock?panel.capturedHeader.width:panel.width
        objectName: panel.panelKey+"DragHandle"
        visible: (!panel.floating && !panel.sidebarCollapsed) || panel.draggingFromDock
    }
    property Item detachedInput: DragStrip {
        parent: floatWindow.contentItem; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.rightMargin: panel.fixedSidebar?82:116
        objectName: panel.panelKey+"FloatingDragHandle"
        visible: panel.floating && !panel.draggingFromDock
    }
    property Window detachedWindow: ApplicationWindow {
        id: floatWindow
        objectName: panel.panelKey+"FloatingWindow"
        // Normal transient windows support native maximize/restore. Utility
        // (Tool) windows can report Maximized while the desktop ignores sizing.
        // Keep these flags fixed during a drag to avoid remapping/flicker.
        flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowCloseButtonHint | (panel.fixedSidebar?Qt.WindowMinimizeButtonHint:Qt.WindowMinMaxButtonsHint)
        opacity: panel.dragging?0.72:1
        visible: panel.floating && panel.workspaceActive && panel.ownerWindow.visible && !panel.shuttingDown
        title: panel.title+" · UART Tool"
        minimumWidth: panel.fixedSidebar?panel.sidebarWidth:panel.minimumFloatWidth
        maximumWidth: panel.fixedSidebar?panel.sidebarWidth:16777215
        minimumHeight: panel.fixedSidebar?panel.fixedFloatHeight:panel.minimumFloatHeight
        maximumHeight: panel.fixedSidebar?panel.fixedFloatHeight:16777215
        transientParent: panel.ownerWindow
        color: Theme.surface; font: panel.ownerWindow.font; palette: panel.ownerWindow.palette
        WindowResizeHandles {
            parent: floatWindow.contentItem
            ownerWindow: floatWindow
            namePrefix: panel.panelKey
            visible: !panel.fixedSidebar && floatWindow.visibility !== Window.Maximized
        }
        onClosing: function(close) {
            if(!panel.shuttingDown) { close.accepted=false; panel.dock() }
        }
    }
}
