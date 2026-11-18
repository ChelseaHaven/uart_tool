import QtQuick
import QtQuick.Controls

// Three live panels, grouped without recreating their content or sessions.
Item {
    id: layout
    required property Item firstPanel
    required property Item secondPanel
    required property Item thirdPanel
    property var groupPanels: [secondPanel,thirdPanel]
    property bool initialized: false
    readonly property var shownPanels: [firstPanel,secondPanel,thirdPanel].filter(panel=>panelShown(panel))
    function panelShown(panel) { return !panel.floating }
    onShownPanelsChanged: { if(initialized) Qt.callLater(configureFill) }
    function groupMinimum(horizontal) {
        const items=groupPanels.filter(panel=>panelShown(panel))
        const sizes=items.map(panel=>horizontal?panel.SplitView.minimumWidth:panel.SplitView.minimumHeight)
        if(!sizes.length) return 0
        return (group.orientation===Qt.Horizontal)===horizontal?sizes.reduce((total,size)=>total+size,0)+12*(sizes.length-1):Math.max(...sizes)
    }
    component Divider: Rectangle {
        implicitWidth: 12; implicitHeight: 12; color: "transparent"
        Rectangle {
            anchors.centerIn: parent; radius: 1
            width: parent.width>parent.height?42:2; height: parent.width>parent.height?2:42
            color: parent.SplitHandle.pressed?Theme.accent:Theme.border
        }
    }
    SplitView {
        id: outer; objectName: "dockSplit"; anchors.fill: parent; orientation: Qt.Horizontal
        handle: Divider { implicitWidth: layout.firstPanel.fixedSidebar?layout.firstPanel.sidebarDividerWidth:12 }
    }
    SidebarFoldHandle {
        objectName: "sidebarFoldHandle"
        visible: layout.firstPanel.fixedSidebar && !layout.firstPanel.floating
        z: 100; edge: layout.firstPanel.sidebarEdge; collapsed: layout.firstPanel.sidebarCollapsed
        width: 12; height: layout.height
        x: edge==="left"?(collapsed?0:layout.firstPanel.width):(collapsed?layout.width-width:layout.width-layout.firstPanel.width-width)
        onCollapseRequested: function(collapse) { layout.firstPanel.sidebarCollapsed=collapse }
    }
    Connections {
        target: layout.firstPanel
        function onSidebarCollapsedChanged() { if(layout.initialized) layout.configureFill() }
    }
    SplitView {
        id: group; orientation: Qt.Horizontal
        visible: layout.groupPanels.some(panel=>layout.panelShown(panel))
        SplitView.fillWidth: true; SplitView.fillHeight: true
        SplitView.minimumWidth: layout.groupMinimum(true); SplitView.minimumHeight: layout.groupMinimum(false)
        handle: Divider {}
    }
    Component.onCompleted: {
        outer.addItem(firstPanel); outer.addItem(group)
        group.addItem(secondPanel); group.addItem(thirdPanel)
        secondPanel.SplitView.preferredWidth=Qt.binding(function() {
            return Math.max(secondPanel.minimumDockWidth,(layout.width-firstPanel.effectiveSidebarWidth-firstPanel.sidebarDividerWidth-12)*0.56)
        })
        initialized=true; configureFill()
    }
    function configureFill() {
        const panels=[firstPanel,secondPanel,thirdPanel]
        panels.forEach(panel=>{
            panel.SplitView.fillWidth=false; panel.SplitView.fillHeight=false
            if(panel.fixedSidebar) {
                panel.SplitView.minimumWidth=panel.effectiveSidebarWidth; panel.SplitView.maximumWidth=panel.effectiveSidebarWidth
                panel.SplitView.preferredWidth=panel.effectiveSidebarWidth
            }
        })
        const visibleGroup=groupPanels.filter(panel=>panelShown(panel))
        const fill=visibleGroup.length?visibleGroup[visibleGroup.length-1]:null
        if(fill) {
            fill.SplitView.fillWidth=group.orientation===Qt.Horizontal
            fill.SplitView.fillHeight=group.orientation===Qt.Vertical
        } else {
            const isolated=outer.itemAt(0)===group?outer.itemAt(1):outer.itemAt(0)
            if(isolated) { isolated.SplitView.fillWidth=!isolated.fixedSidebar; isolated.SplitView.fillHeight=true }
        }
    }
    function install(isolated,pair,outerOrientation,innerOrientation,before) {
        while(group.count) group.takeItem(0)
        while(outer.count) outer.takeItem(0)
        outer.orientation=outerOrientation; group.orientation=innerOrientation; groupPanels=pair
        pair.forEach(item=>group.addItem(item))
        if(before) { outer.addItem(isolated); outer.addItem(group) }
        else { outer.addItem(group); outer.addItem(isolated) }
        configureFill()
    }
    function indexOf(split,item) {
        for(let i=0;i<split.count;++i) if(split.itemAt(i)===item) return i
        return -1
    }
    function placePanel(panel,direction,targetPanel,placement) {
        if(!["left","right","top","bottom"].includes(direction)) return
        const panels=[firstPanel,secondPanel,thirdPanel]
        if(firstPanel.fixedSidebar) {
            if(panel===firstPanel) {
                if(direction!=="left" && direction!=="right") return
                firstPanel.sidebarEdge=direction
                install(firstPanel,groupPanels.filter(item=>item!==firstPanel).length===2?groupPanels:[secondPanel,thirdPanel],Qt.Horizontal,group.orientation,direction==="left")
            } else {
                const other=panel===secondPanel?thirdPanel:secondPanel
                const vertical=direction==="top" || direction==="bottom"
                const pair=direction==="left" || direction==="top"?[panel,other]:[other,panel]
                pair.forEach(item=>{
                    item.SplitView.minimumWidth=item.minimumDockWidth
                    item.SplitView.minimumHeight=Math.min(item.minimumDockHeight,placement.height)
                    item.SplitView.preferredWidth=placement.width; item.SplitView.preferredHeight=placement.height
                })
                install(firstPanel,pair,Qt.Horizontal,vertical?Qt.Vertical:Qt.Horizontal,firstPanel.sidebarEdge==="left")
            }
            return
        }
        const local=targetPanel && targetPanel!==panel && panels.includes(targetPanel) && !targetPanel.floating
        const vertical=direction==="top" || direction==="bottom"
        const before=direction==="left" || direction==="top"
        let isolated, pair, orientation, isolatedBefore
        if(local) {
            isolated=panels.find(item=>item!==panel && item!==targetPanel)
            pair=before?[panel,targetPanel]:[targetPanel,panel]
            // Replace only the target leaf with a pair, retaining the untouched
            // panel's position relative to the old target.
            const targetInGroup=indexOf(group,targetPanel)>=0, otherInGroup=indexOf(group,isolated)>=0
            const common=targetInGroup && otherInGroup?group:outer
            orientation=common.orientation
            isolatedBefore=indexOf(common,common===outer && otherInGroup?group:isolated)<indexOf(common,common===outer && targetInGroup?group:targetPanel)
        } else {
            isolated=panel; pair=panels.filter(item=>item!==panel)
            orientation=vertical?Qt.Vertical:Qt.Horizontal; isolatedBefore=before
        }
        panels.forEach(item=>{
            item.SplitView.minimumWidth=item.minimumDockWidth
            item.SplitView.minimumHeight=item.minimumDockHeight
        })
        // Equal halves include the same 12 px divider as the preview. Minimum
        // sizes may shrink to the assigned half, never silently change the ratio.
        const extent=vertical?placement.height:placement.width
        const participants=local?pair:panels
        participants.forEach(item=>{
            if(vertical) item.SplitView.minimumHeight=Math.min(item.minimumDockHeight,extent)
            else item.SplitView.minimumWidth=item.minimumDockWidth
        })
        panel.SplitView.preferredWidth=placement.width; panel.SplitView.preferredHeight=placement.height
        if(local) {
            targetPanel.SplitView.preferredWidth=placement.width
            targetPanel.SplitView.preferredHeight=placement.height
            install(isolated,pair,orientation,vertical?Qt.Vertical:Qt.Horizontal,isolatedBefore)
        } else install(isolated,pair,orientation,vertical?Qt.Horizontal:Qt.Vertical,before)
    }
}
