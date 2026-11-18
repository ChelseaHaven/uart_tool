import QtQuick
import QtQuick.Controls

// Discrete fold gesture: the sidebar never follows the pointer's width.
Rectangle {
    id: handle
    required property string edge
    required property bool collapsed
    signal collapseRequested(bool collapse)
    color: collapsed?Theme.surface:Theme.window
    radius: collapsed?5:0
    border.width: collapsed?1:0; border.color: Theme.border
    Rectangle {
        anchors.centerIn: parent; width: 2; height: 42; radius: 1
        visible: !handle.collapsed && !mouse.containsMouse
        color: Theme.border
    }
    Text {
        anchors.centerIn: parent
        visible: handle.collapsed || mouse.containsMouse
        text: (handle.edge==="left")===handle.collapsed?"›":"‹"
        color: mouse.containsMouse?Theme.accentText:Theme.secondary; font.pixelSize: 20
    }
    MouseArea {
        id: mouse; anchors.fill: parent; hoverEnabled: true; preventStealing: true
        cursorShape: pressed?Qt.ClosedHandCursor:Qt.SizeHorCursor
        property real pressedX: 0
        property bool snapped: false
        onPressed: function(event) { pressedX=mapToGlobal(event.x,event.y).x; snapped=false }
        onPositionChanged: function(event) {
            if(!pressed || snapped) return
            const delta=mapToGlobal(event.x,event.y).x-pressedX
            const outward=handle.edge==="left"?-delta:delta
            if((!handle.collapsed && outward>=24) || (handle.collapsed && outward<=-24)) {
                snapped=true; handle.collapseRequested(!handle.collapsed)
            }
        }
        onReleased: function(event) {
            if(!snapped && handle.collapsed && Math.abs(mapToGlobal(event.x,event.y).x-pressedX)<6)
                handle.collapseRequested(false)
        }
    }
    ToolTip.visible: mouse.containsMouse && !mouse.pressed
    ToolTip.text: collapsed?"向内拖动展开侧栏":"向外拖动收起侧栏"
}
