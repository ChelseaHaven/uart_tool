import QtQuick

Item {
    id: handles
    required property Window ownerWindow
    required property string namePrefix
    readonly property int gripSize: 5

    anchors.fill: parent
    z: 40

    Repeater {
        model: ["l", "r", "t", "b", "lt", "rt", "lb", "rb"]
        MouseArea {
            required property string modelData
            readonly property bool leftEdge: modelData.includes("l")
            readonly property bool rightEdge: modelData.includes("r")
            readonly property bool topEdge: modelData.includes("t")
            readonly property bool bottomEdge: modelData.includes("b")
            readonly property bool side: leftEdge || rightEdge
            readonly property bool corner: modelData.length === 2
            readonly property int edges: (leftEdge ? Qt.LeftEdge : 0) | (rightEdge ? Qt.RightEdge : 0)
                                         | (topEdge ? Qt.TopEdge : 0) | (bottomEdge ? Qt.BottomEdge : 0)

            objectName: handles.namePrefix + "WindowResize_" + modelData
            x: leftEdge ? 0 : rightEdge ? parent.width - handles.gripSize : handles.gripSize
            y: topEdge ? 0 : bottomEdge ? parent.height - handles.gripSize : handles.gripSize
            width: side ? handles.gripSize : parent.width - 2 * handles.gripSize
            height: side && !corner ? parent.height - 2 * handles.gripSize : handles.gripSize
            cursorShape: corner ? (leftEdge === topEdge ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor)
                                : (side ? Qt.SizeHorCursor : Qt.SizeVerCursor)
            onPressed: handles.ownerWindow.startSystemResize(edges)
        }
    }
}
