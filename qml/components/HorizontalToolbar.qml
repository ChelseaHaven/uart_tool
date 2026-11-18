import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Shared single-row toolbar: scroll only when its controls do not fit.
Flickable {
    id: viewport
    default property alias controls: row.data
    property alias spacing: row.spacing
    readonly property bool horizontalOverflow: row.implicitWidth>width+1
    implicitHeight: row.implicitHeight
    clip: true; boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.HorizontalFlick
    contentWidth: horizontalOverflow?row.implicitWidth:width; contentHeight: height
    onHorizontalOverflowChanged: { if(!horizontalOverflow) contentX=0 }
    HoverHandler {
        id: scrollHover
        enabled: viewport.horizontalOverflow
    }
    ScrollBar.horizontal: ScrollBar {
        id: bar
        objectName: "horizontalToolbarScrollBar"
        visible: viewport.horizontalOverflow
        z: 1
        // Overlay the bottom edge while keeping the full hit area.
        opacity: scrollHover.hovered || hovered || pressed ? 1 : 0
        policy: ScrollBar.AlwaysOn; implicitHeight: 14; padding: 0
        hoverEnabled: true; minimumSize: 0.12
        contentItem: Item {
            implicitWidth: 40; implicitHeight: 14
            Rectangle {
                anchors.bottom: parent.bottom; anchors.bottomMargin: 1
                width: parent.width; height: 3; radius: 1.5
                color: bar.pressed?Theme.accentText:bar.hovered?Theme.accentHover:Theme.secondary
            }
        }
        background: Item {
            Rectangle {
                anchors.bottom: parent.bottom; anchors.bottomMargin: 1
                width: parent.width; height: 2; radius: 1; color: Theme.divider
            }
        }
    }
    RowLayout {
        id: row; width: Math.max(viewport.width,implicitWidth); spacing: 8
    }
}
