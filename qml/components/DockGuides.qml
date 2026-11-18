import QtQuick

Item {
    id: guides
    required property string direction
    required property rect previewRect
    enabled: false
    Rectangle {
        objectName: "dockPreviewRegion"
        visible: guides.direction.length>0
        x: guides.previewRect.x; y: guides.previewRect.y
        width: guides.previewRect.width; height: guides.previewRect.height
        radius: 8; color: "transparent"
        border.color: Theme.accent; border.width: 2
    }
    Repeater {
        model: ["left","top","right","bottom"]
        delegate: Rectangle {
            required property string modelData
            readonly property bool selected: guides.direction===modelData
            objectName: "dockGuide_"+modelData
            visible: selected
            width: 44; height: 44; radius: 7
            x: modelData==="left"?14:modelData==="right"?guides.width-width-14:(guides.width-width)/2
            y: modelData==="top"?14:modelData==="bottom"?guides.height-height-14:(guides.height-height)/2
            color: selected?Theme.accentSoft:Theme.surface
            border.color: selected?Theme.accent:Theme.border; border.width: selected?2:1
            Rectangle {
                anchors.centerIn: parent; width: 24; height: 24; radius: 2
                color: "transparent"; border.color: Theme.accentText
                Rectangle {
                    x: modelData==="right"?12:2; y: modelData==="bottom"?12:2
                    width: ["left","right"].indexOf(modelData)>=0?10:20
                    height: ["top","bottom"].indexOf(modelData)>=0?10:20
                    color: Theme.accentText; radius: 1
                }
            }
        }
    }
}
