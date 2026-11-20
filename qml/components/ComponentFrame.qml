import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Shared chrome and geometry gestures for every workspace component.
Rectangle {
    id: frame
    property string title: "组件"
    property int titleLabelWidth: 0
    property bool maximized: false
    property bool selected: false
    property bool pasteAvailable: false
    default property alias content: body.data
    property alias headerActions: extraActions.data
    property alias titleEditor: titleLoader.sourceComponent
    readonly property alias titleEditorItem: titleLoader.item
    signal activated()
    signal moveRequested(real dx,real dy)
    signal resizeRequested(string edges,real dx,real dy)
    signal maximizeRequested()
    signal removeRequested()
    signal copyRequested()
    signal pasteRequested()
    color: Theme.surface; radius: 8
    border.color: selected?Theme.accent:Theme.border
    function activateForKeyboard() {
        forceActiveFocus(Qt.MouseFocusReason)
        activated()
    }
    TapHandler {
        gesturePolicy: TapHandler.DragThreshold
        onPressedChanged: { if(pressed) frame.activated() }
    }
    // Consume presses on blank space and disabled controls before they reach a lower card.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: frame.activateForKeyboard()
        onClicked: function(mouse) {
            if (mouse.button!==Qt.RightButton) return
            componentMenu.x=mouse.x
            componentMenu.y=mouse.y
            componentMenu.open()
        }
    }
    Menu {
        id: componentMenu
        objectName: "componentContextMenu"
        parent: frame
        MenuItem {
            objectName: "copyComponentAction"
            text: "复制"
            onTriggered: frame.copyRequested()
        }
        MenuItem {
            objectName: "pasteComponentAction"
            text: "粘贴"
            enabled: frame.pasteAvailable
            onTriggered: Qt.callLater(frame.pasteRequested)
        }
        MenuItem {
            objectName: "deleteComponentAction"
            text: "删除"
            onTriggered: Qt.callLater(frame.removeRequested)
        }
    }
    Item {
        id: header; x: 10; y: 8; width: parent.width-20; height: 30
        readonly property real titleWidth: Math.max(0, actions.x - 8)
        Text {
            visible: titleLoader.sourceComponent === null
            anchors.verticalCenter: parent.verticalCenter
            width: frame.titleLabelWidth > 0 ? Math.min(header.titleWidth, frame.titleLabelWidth) : header.titleWidth
            horizontalAlignment: frame.titleLabelWidth > 0 ? Text.AlignRight : Text.AlignLeft
            elide: Text.ElideRight
            text: frame.title; font.bold: true; color: Theme.text
        }
        MouseArea {
            objectName: "componentMoveHandle"
            visible: titleLoader.sourceComponent === null
            height: parent.height; width: header.titleWidth
            preventStealing: true; cursorShape: pressed?Qt.ClosedHandCursor:Qt.OpenHandCursor
            property point lastPosition
            onPressed: function(mouse) { lastPosition=mapToGlobal(mouse.x,mouse.y); frame.activateForKeyboard() }
            onPositionChanged: function(mouse) {
                if(!pressed || frame.maximized) return
                const position=mapToGlobal(mouse.x,mouse.y)
                frame.moveRequested(position.x-lastPosition.x,position.y-lastPosition.y); lastPosition=position
            }
            onDoubleClicked: frame.maximizeRequested()
        }
        Loader {
            id: titleLoader
            anchors.verticalCenter: parent.verticalCenter
            width: header.titleWidth
            height: 29
        }
        RowLayout {
            id: actions; anchors.right: parent.right; height: parent.height; spacing: 4
            RowLayout { id: extraActions; spacing: 4 }
            ToolButton {
                text: frame.maximized?"❐":"□"; implicitWidth: 28; implicitHeight: 29
                Accessible.name: frame.maximized?"还原组件":"最大化组件"
                onClicked: { frame.activated(); frame.maximizeRequested() }
            }
            ToolButton {
                text: "×"; implicitWidth: 28; implicitHeight: 29; Accessible.name: "删除组件"
                onClicked: frame.removeRequested()
            }
        }
    }
    Item {
        id: body; anchors.left: parent.left; anchors.right: parent.right
        anchors.top: header.bottom; anchors.bottom: parent.bottom; anchors.margins: 10
    }
    Repeater {
        model: ["l","r","t","b","lt","rt","lb","rb"]
        MouseArea {
            required property string modelData
            objectName: "componentResize_"+modelData
            z: 30; visible: !frame.maximized; preventStealing: true
            readonly property bool corner: modelData.length===2
            x: modelData.includes("l")?0:modelData.includes("r")?frame.width-8:8
            y: modelData.includes("t")?0:modelData.includes("b")?frame.height-8:8
            width: corner || modelData==="l" || modelData==="r"?8:frame.width-16
            height: corner || modelData==="t" || modelData==="b"?8:frame.height-16
            cursorShape: corner?(modelData==="lt" || modelData==="rb"?Qt.SizeFDiagCursor:Qt.SizeBDiagCursor):(modelData==="l" || modelData==="r"?Qt.SizeHorCursor:Qt.SizeVerCursor)
            property point lastPosition
            onPressed: function(mouse) { lastPosition=mapToGlobal(mouse.x,mouse.y); frame.activateForKeyboard() }
            onPositionChanged: function(mouse) {
                if(!pressed) return
                const position=mapToGlobal(mouse.x,mouse.y)
                frame.resizeRequested(modelData,position.x-lastPosition.x,position.y-lastPosition.y); lastPosition=position
            }
        }
    }
}
