import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: workspace
    objectName: "componentWorkspace"
    required property var series
    required property QtObject protocol
    required property QtObject sender
    readonly property int widgetCount: widgets.count
    property int nextId: 1
    property int activeId: -1
    property Item maximizedWidget: null
    property bool dragHighlight: false
    property string message: ""
    color: (dropArea.containsDrag || dragHighlight)?Theme.accentSoft:Theme.input
    radius: 8; border.color: (dropArea.containsDrag || dragHighlight)?Theme.accent:Theme.border
    ListModel { id: widgets }
    function minimumWidgetWidth(type) {
        return type==="quick-send" ? 384 : 300
    }
    function addWidget(type,position) {
        if(type!=="time-plot" && type!=="xy-plot" && type!=="quick-send") return false
        if(widgets.count>=8) { message="最多放置 8 个组件。"; return false }
        maximizedWidget=null; activeId=nextId
        // Convert the release point into canvas coordinates, including scrolling.
        const point=position?canvas.mapFromItem(workspace,position.x,position.y):Qt.point(0,0)
        const sending=type==="quick-send"
        const itemWidth=Math.max(minimumWidgetWidth(type),Math.min(sending?400:760,viewport.width-24))
        const itemHeight=Math.max(200,Math.min(sending?290:350,viewport.height-24))
        widgets.append({componentType:type,instanceId:nextId++,
            posX:Math.max(0,Math.min(8192-itemWidth,point.x)),posY:Math.max(0,Math.min(8192-itemHeight,point.y)),
            itemWidth:itemWidth,itemHeight:itemHeight})
        message=""; return true
    }
    // Geometry belongs to the workspace, independent of a particular component.
    function moveWidget(index,dx,dy) {
        const item=widgets.get(index)
        widgets.setProperty(index,"posX",Math.max(0,Math.min(8192-item.itemWidth,item.posX+dx)))
        widgets.setProperty(index,"posY",Math.max(0,Math.min(8192-item.itemHeight,item.posY+dy)))
    }
    function resizeWidget(index,edges,dx,dy) {
        const item=widgets.get(index)
        const minimumWidth=minimumWidgetWidth(item.componentType)
        let left=item.posX, top=item.posY, right=left+item.itemWidth, bottom=top+item.itemHeight
        if(edges.includes("l")) left=Math.max(0,Math.min(right-minimumWidth,left+dx))
        if(edges.includes("r")) right=Math.max(left+minimumWidth,Math.min(8192,right+dx))
        if(edges.includes("t")) top=Math.max(0,Math.min(bottom-200,top+dy))
        if(edges.includes("b")) bottom=Math.max(top+200,Math.min(8192,bottom+dy))
        widgets.set(index,{posX:left,posY:top,itemWidth:right-left,itemHeight:bottom-top})
    }
    function toggleMaximize(widget) {
        maximizedWidget=maximizedWidget===widget?null:widget
        viewport.contentX=0; viewport.contentY=0
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Text { text: "组件画布"; color: Theme.secondary; font.bold: true; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            Text { text: workspace.message || "拖动标题移动 · 拖动边缘拉伸"; color: Theme.muted; font.pixelSize: 11 }
        }
        Flickable {
            id: viewport; objectName: "componentCanvas"
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            // Component gestures own the mouse; scrollbars navigate oversized canvas.
            interactive: false; boundsBehavior: Flickable.StopAtBounds
            contentWidth: workspace.maximizedWidget?workspace.maximizedWidget.width:Math.max(width,canvas.childrenRect.width+canvas.childrenRect.x+16)
            contentHeight: workspace.maximizedWidget?height:Math.max(height,canvas.childrenRect.height+canvas.childrenRect.y+16)
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Item {
                id: canvas; width: viewport.width; height: viewport.height
                Repeater {
                    model: widgets
                    Loader {
                        id: loader
                        required property int index
                        required property string componentType
                        required property int instanceId
                        required property real posX
                        required property real posY
                        required property real itemWidth
                        required property real itemHeight
                        readonly property bool maximized: workspace.maximizedWidget===loader
                        x: maximized?0:posX; y: maximized?0:posY
                        width: maximized && componentType==="quick-send"
                            ? Math.max(viewport.width,workspace.minimumWidgetWidth(componentType))
                            : maximized ? viewport.width : itemWidth
                        height: maximized?viewport.height:itemHeight
                        z: workspace.activeId===instanceId?100:index
                        visible: !workspace.maximizedWidget || maximized
                        sourceComponent: {
                            switch(componentType) {
                            case "xy-plot": return xyPlot
                            case "quick-send": return quickSend
                            default: return timePlot
                            }
                        }
                        Connections {
                            target: loader.item
                            function onActivated() { workspace.activeId=loader.instanceId }
                            function onMoveRequested(dx,dy) { workspace.moveWidget(loader.index,dx,dy) }
                            function onResizeRequested(edges,dx,dy) { workspace.resizeWidget(loader.index,edges,dx,dy) }
                            function onMaximizeRequested() { workspace.toggleMaximize(loader) }
                            function onRemoveRequested() {
                                if(loader.maximized) workspace.maximizedWidget=null
                                widgets.remove(loader.index)
                            }
                        }
                        Component {
                            id: timePlot
                            TimePlotCard {
                                instanceId: loader.instanceId; series: workspace.series; paused: workspace.protocol.displayPaused
                                maximized: loader.maximized; selected: workspace.activeId===loader.instanceId
                            }
                        }
                        Component {
                            id: quickSend
                            QuickSendCard {
                                instanceId: loader.instanceId; sender: workspace.sender
                                maximized: loader.maximized; selected: workspace.activeId===loader.instanceId
                            }
                        }
                        Component {
                            id: xyPlot
                            XYPlotCard {
                                instanceId: loader.instanceId; series: workspace.series; paused: workspace.protocol.displayPaused
                                maximized: loader.maximized; selected: workspace.activeId===loader.instanceId
                            }
                        }
                    }
                }
            }
        }
    }
    Text {
        visible: widgets.count===0 || dragHighlight
        anchors.centerIn: parent; width: parent.width-30; z: 200
        horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; lineHeight: 1.6
        text: (dropArea.containsDrag || dragHighlight)?"松开放入组件":"在左侧切换到“组件”，将组件拖到这里"
        color: dragHighlight?Theme.accentText:Theme.muted; font.pixelSize: 12
    }
    DropArea {
        id: dropArea; objectName: "componentDropArea"; anchors.fill: parent; z: 201
        keys: ["application/x-uart-component"]
        onDropped: function(drop) {
            if(workspace.addWidget(drop.getDataAsString("application/x-uart-component"),Qt.point(drop.x,drop.y))) drop.acceptProposedAction()
            else drop.accepted=false
        }
    }
}
