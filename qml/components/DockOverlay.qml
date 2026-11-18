import QtQuick

// Preview is part of the main scene, not another desktop window. It therefore
// survives repeated drags without native window mapping, placement or focus.
Item {
    id: overlay
    objectName: "dockOverlay"
    required property var ownerWindow
    required property Item targetArea
    required property var activePanel
    parent: ownerWindow.contentItem
    z: 100000; enabled: false
    readonly property rect bounds: activePanel?activePanel.guideRect:Qt.rect(0,0,0,0)
    visible: activePanel!==null && activePanel.guidesVisible && ownerWindow.visible && targetArea.visible && bounds.width>0 && bounds.height>0
    x: { const moved=ownerWindow.x; return parent.mapFromGlobal(bounds.x,bounds.y).x }
    y: { const moved=ownerWindow.y; return parent.mapFromGlobal(bounds.x,bounds.y).y }
    width: bounds.width; height: bounds.height
    DockGuides {
        anchors.fill: parent
        direction: overlay.activePanel?overlay.activePanel.dockEdge:""
        previewRect: overlay.activePanel?Qt.rect(overlay.activePanel.previewRect.x-overlay.bounds.x,
            overlay.activePanel.previewRect.y-overlay.bounds.y,overlay.activePanel.previewRect.width,overlay.activePanel.previewRect.height):Qt.rect(0,0,0,0)
    }
}
