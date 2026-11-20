import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as Files
import QtQuick.Layouts

Rectangle {
    id: workspace
    objectName: "componentWorkspace"
    required property var series
    required property QtObject protocol
    required property QtObject sender
    required property QtObject layouts
    readonly property int widgetCount: widgets.count
    property int nextId: 1
    property int activeId: -1
    property var stackingOrder: []
    property var copiedWidget: null
    property var blankPastePoint: null
    property Item maximizedWidget: null
    property bool dragHighlight: false
    property string message: ""
    color: (dropArea.containsDrag || dragHighlight)?Theme.accentSoft:Theme.input
    radius: 8; border.color: (dropArea.containsDrag || dragHighlight)?Theme.accent:Theme.border
    ListModel { id: widgets }
    function plotSettings(type) {
        return type === "xy-plot"
            ? ["focusEnabled", "xReferenceEnabled", "xReferenceValue", "yReferenceEnabled", "yReferenceValue"]
            : ["focusEnabled", "referenceEnabled", "referenceValue"]
    }

    function snapshotWidget(index) {
        const row=widgets.get(index)
        const card=instances.itemAt(index)?.item
        const component={type:row.componentType,x:row.posX,y:row.posY,
                         width:row.itemWidth,height:row.itemHeight}
        if(row.componentType==="quick-send") {
            component.name=row.commandName
            if(card) component.send=card.snapshotSend()
        } else if(card) {
            for (const key of plotSettings(row.componentType))
                component[key]=card[key]
        }
        return component
    }

    function snapshotLayout() {
        const components=[]
        const indexesById=new Map()
        for(let i=0;i<widgets.count;++i) {
            indexesById.set(widgets.get(i).instanceId,i)
            components.push(snapshotWidget(i))
        }
        return {format:"uart-tool.component-layout",version:1,components:components,
                maximizedIndex:maximizedWidget?maximizedWidget.index:-1,
                stackOrder:stackingOrder.map(id=>indexesById.get(id)),
                scrollX:viewport.contentX,scrollY:viewport.contentY}
    }

    function appendWidget(component) {
        widgets.append({componentType:component.type,instanceId:nextId++,
                        commandName:component.name || "",
                        posX:component.x,posY:component.y,itemWidth:component.width,itemHeight:component.height})
        const card=instances.itemAt(widgets.count-1)?.item
        if(!card) return null
        if(component.type==="quick-send") {
            if(component.send) card.restoreSend(component.send)
        } else {
            for (const key of plotSettings(component.type)) {
                if(component[key] !== undefined) card[key]=component[key]
            }
        }
        return card
    }

    function saveLayout() {
        if(!layouts.saveLastLayout(snapshotLayout())) console.warn(layouts.lastError)
    }

    function restoreLayout(layout) {
        if(!layout.components) return
        maximizedWidget=null
        activeId=-1
        blankPastePoint=null
        stackingOrder=[]
        widgets.clear()
        nextId=1
        for(const component of layout.components)
            appendWidget(component)
        stackingOrder=Array.from(layout.stackOrder,index=>widgets.get(index).instanceId)
        if(layout.maximizedIndex>=0) maximizedWidget=instances.itemAt(layout.maximizedIndex)
        Qt.callLater(function() {
            viewport.contentX=Math.max(0,Math.min(layout.scrollX,viewport.contentWidth-viewport.width))
            viewport.contentY=Math.max(0,Math.min(layout.scrollY,viewport.contentHeight-viewport.height))
        })
    }

    function importLayoutFrom(file) {
        const layout=layouts.importLayout(file)
        if(layout.components) {
            restoreLayout(layout)
            message="布局已导入"
        } else {
            message=layouts.lastError
        }
    }

    function exportLayoutTo(file) {
        message=layouts.exportLayout(snapshotLayout(),file) ? "布局已导出" : layouts.lastError
    }

    Component.onCompleted: restoreLayout(layouts.lastLayout())

    Files.FileDialog {
        id: importDialog
        objectName: "importComponentLayoutDialog"
        parentWindow: workspace.Window.window
        title: "导入组件布局"
        nameFilters: ["组件布局 (*.json)"]
        onAccepted: workspace.importLayoutFrom(selectedFile)
    }
    Files.FileDialog {
        id: exportDialog
        objectName: "exportComponentLayoutDialog"
        parentWindow: workspace.Window.window
        title: "导出组件布局"
        fileMode: Files.FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["组件布局 (*.json)"]
        onAccepted: workspace.exportLayoutTo(selectedFile)
    }

    function minimumWidgetWidth(type) {
        return type==="quick-send" ? 384 : 300
    }
    function activateWidget(id) {
        blankPastePoint=null
        if (stackingOrder[stackingOrder.length-1]!==id)
            stackingOrder=stackingOrder.filter(existing=>existing!==id).concat(id)
        activeId=id
    }
    function removeWidget(widget) {
        if (widget.maximized) maximizedWidget=null
        stackingOrder=stackingOrder.filter(id=>id!==widget.instanceId)
        if (activeId===widget.instanceId)
            activeId=stackingOrder[stackingOrder.length-1] ?? -1
        widgets.remove(widget.index)
    }
    function activeWidget() {
        for (let index=0;index<widgets.count;++index) {
            if (widgets.get(index).instanceId===activeId) return instances.itemAt(index)
        }
        return null
    }
    function prepareNewWidget() {
        if (widgets.count>=8) {
            message="最多放置 8 个组件。"
            return false
        }
        maximizedWidget=null
        activateWidget(nextId)
        message=""
        return true
    }
    function placeNewWidget(component,point) {
        const right=viewport.contentX+viewport.width-16
        const bottom=viewport.contentY+viewport.height-16
        component.x=Math.min(8192-component.width,Math.max(0,viewport.contentX,Math.min(right,point.x)))
        component.y=Math.min(8192-component.height,Math.max(0,viewport.contentY,Math.min(bottom,point.y)))
        return appendWidget(component)
    }
    function dropPoint(source,x,y) {
        return canvas.mapFromItem(source,x-16,y-16)
    }
    function addWidget(type,position) {
        if (type!=="time-plot" && type!=="xy-plot" && type!=="quick-send") return false
        if (!prepareNewWidget()) return false
        const point=position ? dropPoint(workspace,position.x,position.y) : Qt.point(0,0)
        const sending=type==="quick-send"
        const xyPlot=type==="xy-plot"
        const preferredWidth=xyPlot ? Math.min(360,viewport.height-24) : sending ? 400 : 760
        const itemWidth=Math.max(minimumWidgetWidth(type),Math.min(preferredWidth,viewport.width-24))
        const itemHeight=xyPlot ? itemWidth : Math.max(200,Math.min(sending?290:350,viewport.height-24))
        placeNewWidget({type:type,width:itemWidth,height:itemHeight},point)
        forceActiveFocus(Qt.MouseFocusReason)
        return true
    }
    function copyWidget(widget) {
        const component=snapshotWidget(widget.index)
        const card=widget.item
        if (component.type==="time-plot") component.channels=card.selectedKeys.slice()
        if (component.type==="xy-plot") component.channels=[card.xKey,card.yKey]
        copiedWidget=component
    }
    function pasteWidget() {
        if (!copiedWidget) return
        const anchor=activeWidget()
        const point=blankPastePoint ?? Qt.point((anchor ? anchor.posX : copiedWidget.x)+24,
                                                (anchor ? anchor.posY : copiedWidget.y)+24)
        if (!prepareNewWidget()) return
        const component=Object.assign({},copiedWidget)
        const card=placeNewWidget(component,point)
        if (card && component.channels && series) {
            if (component.type==="time-plot")
                card.selectedKeys=component.channels.filter(key=>series.hasChannel(key))
            else {
                card.xKey=series.hasChannel(component.channels[0]) ? component.channels[0] : ""
                card.yKey=series.hasChannel(component.channels[1]) ? component.channels[1] : ""
            }
        }
        forceActiveFocus()
    }
    Keys.onPressed: function(event) {
        const focused=workspace.Window.window?.activeFocusItem
        if (focused && typeof focused.selectAll==="function" && focused.cursorPosition!==undefined) return
        const widget=activeWidget()
        if (event.matches(StandardKey.Copy) && widget) copyWidget(widget)
        else if (event.matches(StandardKey.Paste) && copiedWidget) pasteWidget()
        else if (event.key===Qt.Key_Delete && !event.modifiers && widget) removeWidget(widget)
        else return
        event.accepted=true
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
            Text {
                id: layoutStatus
                Layout.fillWidth: true
                text: workspace.message || "拖动标题移动 · 拖动边缘拉伸"
                elide: Text.ElideRight; horizontalAlignment: Text.AlignRight
                color: Theme.muted; font.pixelSize: 11
                HoverHandler { id: layoutStatusHover }
                ToolTip.visible: layoutStatusHover.hovered && truncated
                ToolTip.text: text
            }
            AppButton { objectName: "importComponentLayout"; text: "导入布局"; implicitWidth: 76; implicitHeight: 30; onClicked: importDialog.open() }
            AppButton { objectName: "exportComponentLayout"; text: "导出布局"; implicitWidth: 76; implicitHeight: 30; onClicked: exportDialog.open() }
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
            MouseArea {
                id: canvasBackground
                width: viewport.contentWidth
                height: viewport.contentHeight
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function(mouse) {
                    workspace.activeId=-1
                    workspace.blankPastePoint=workspace.dropPoint(canvasBackground,mouse.x,mouse.y)
                    workspace.forceActiveFocus(Qt.MouseFocusReason)
                    if (mouse.button===Qt.RightButton) {
                        const point=workspace.mapFromItem(canvasBackground,mouse.x,mouse.y)
                        canvasMenu.x=point.x
                        canvasMenu.y=point.y
                        canvasMenu.open()
                    }
                }
            }
            Item {
                id: canvas; width: viewport.width; height: viewport.height
                Repeater {
                    id: instances
                    model: widgets
                    Loader {
                        id: loader
                        required property int index
                        required property string componentType
                        required property string commandName
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
                        z: workspace.stackingOrder.indexOf(instanceId)
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
                            function onActivated() { workspace.activateWidget(loader.instanceId) }
                            function onCopyRequested() { workspace.copyWidget(loader) }
                            function onPasteRequested() { workspace.pasteWidget() }
                            function onMoveRequested(dx,dy) { workspace.moveWidget(loader.index,dx,dy) }
                            function onResizeRequested(edges,dx,dy) { workspace.resizeWidget(loader.index,edges,dx,dy) }
                            function onMaximizeRequested() { workspace.toggleMaximize(loader) }
                            function onRemoveRequested() { workspace.removeWidget(loader) }
                        }
                        Component {
                            id: timePlot
                            TimePlotCard {
                                instanceId: loader.instanceId; series: workspace.series; paused: workspace.protocol.displayPaused
                                pasteAvailable: workspace.copiedWidget!==null && workspace.widgetCount<8
                                maximized: loader.maximized; selected: workspace.activeId===loader.instanceId
                            }
                        }
                        Component {
                            id: quickSend
                            QuickSendCard {
                                instanceId: loader.instanceId; sender: workspace.sender
                                commandName: loader.commandName
                                onRenameRequested: function(name) { widgets.setProperty(loader.index,"commandName",name) }
                                pasteAvailable: workspace.copiedWidget!==null && workspace.widgetCount<8
                                maximized: loader.maximized; selected: workspace.activeId===loader.instanceId
                            }
                        }
                        Component {
                            id: xyPlot
                            XYPlotCard {
                                instanceId: loader.instanceId; series: workspace.series; paused: workspace.protocol.displayPaused
                                pasteAvailable: workspace.copiedWidget!==null && workspace.widgetCount<8
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
    Menu {
        id: canvasMenu
        objectName: "componentCanvasMenu"
        parent: workspace
        MenuItem {
            objectName: "pasteOnCanvasAction"
            text: "粘贴"
            enabled: workspace.copiedWidget!==null && workspace.widgetCount<8
            onTriggered: Qt.callLater(workspace.pasteWidget)
        }
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
