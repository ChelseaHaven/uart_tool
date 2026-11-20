import QtQuick
import QtQuick.Controls

// Three live panels, grouped without recreating their content or sessions.
Item {
    id: layout
    required property Item firstPanel
    required property Item secondPanel
    required property Item thirdPanel
    property var initialLayout: ({})
    property var groupPanels: [secondPanel,thirdPanel]
    property bool initialized: false
    property real groupRatio: 0.56
    property real resizeStartExtent: 0
    readonly property int dividerSize: 12
    readonly property bool groupHorizontal: group.orientation === Qt.Horizontal
    readonly property real groupSplitExtent: (groupHorizontal ? group.availableWidth : group.availableHeight) - dividerSize

    onGroupSplitExtentChanged: applyGroupRatio()

    function updateGroupRatio() {
        if (!firstPanel.fixedSidebar || !groupPanels.every(panel => panelShown(panel)))
            return

        const leading = groupPanels[0]
        const extent = groupHorizontal ? leading.width : leading.height
        if (group.resizing) {
            resizeStartExtent = extent
            return
        }
        if (groupSplitExtent > 0 && extent !== resizeStartExtent)
            groupRatio = extent / groupSplitExtent
        applyGroupRatio()
    }

    function applyGroupRatio() {
        if (!initialized || !firstPanel.fixedSidebar || group.resizing || groupSplitExtent <= 0
                || !groupPanels.every(panel => panelShown(panel)))
            return

        const [leading, trailing] = groupPanels
        const leadingMin = groupHorizontal ? leading.SplitView.minimumWidth : leading.SplitView.minimumHeight
        const trailingMin = groupHorizontal ? trailing.SplitView.minimumWidth : trailing.SplitView.minimumHeight
        // Clamp the visible size without changing the ratio chosen by the user.
        const extent = Math.max(leadingMin, Math.min(groupSplitExtent - trailingMin, groupSplitExtent * groupRatio))
        if (groupHorizontal)
            leading.SplitView.preferredWidth = extent
        else
            leading.SplitView.preferredHeight = extent
    }
    readonly property var shownPanels: [firstPanel,secondPanel,thirdPanel].filter(panel=>panelShown(panel))
    function panelShown(panel) { return !panel.floating }
    onShownPanelsChanged: { if(initialized) Qt.callLater(configureFill) }
    function groupMinimum(horizontal) {
        const items=groupPanels.filter(panel=>panelShown(panel))
        const sizes=items.map(panel=>horizontal?panel.SplitView.minimumWidth:panel.SplitView.minimumHeight)
        if(!sizes.length) return 0
        return (group.orientation===Qt.Horizontal)===horizontal?sizes.reduce((total,size)=>total+size,0)+dividerSize*(sizes.length-1):Math.max(...sizes)
    }
    component Divider: Rectangle {
        implicitWidth: layout.dividerSize; implicitHeight: layout.dividerSize; color: "transparent"
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
        onResizingChanged: layout.updateGroupRatio()
    }
    function snapshotLayout() {
        const floating = {}
        for (const panel of [firstPanel, secondPanel, thirdPanel]) {
            if (panel.floating) floating[panel.panelKey] = panel.floatingGeometry()
        }
        return {version: 1, sidebarEdge: firstPanel.sidebarEdge,
                sidebarCollapsed: firstPanel.sidebarCollapsed,
                vertical: !groupHorizontal, receiveFirst: groupPanels[0] === thirdPanel,
                ratio: groupRatio, floating: floating}
    }

    function restoreLayout(saved) {
        const state = saved.version === 1 ? saved : {}
        for (const panel of [firstPanel, secondPanel, thirdPanel]) {
            if (panel.floating) panel.dock()
            panel.SplitView.minimumHeight = panel.minimumDockHeight
        }

        firstPanel.sidebarEdge = state.sidebarEdge === "right" ? "right" : "left"
        firstPanel.sidebarCollapsed = state.sidebarCollapsed === true
        groupRatio = Number.isFinite(state.ratio) && state.ratio > 0 && state.ratio < 1 ? state.ratio : 0.56
        const pair = state.receiveFirst ? [thirdPanel, secondPanel] : [secondPanel, thirdPanel]
        install(firstPanel, pair, Qt.Horizontal, state.vertical ? Qt.Vertical : Qt.Horizontal,
                firstPanel.sidebarEdge === "left")

        for (const panel of [firstPanel, secondPanel, thirdPanel]) {
            const geometry = state.floating?.[panel.panelKey]
            if (geometry) panel.restoreFloatingGeometry(geometry)
        }
    }

    Component.onCompleted: {
        initialized=true
        restoreLayout(initialLayout)
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
        applyGroupRatio()
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
                })
                groupRatio = 0.5
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
