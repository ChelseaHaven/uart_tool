import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: bar
    required property Window ownerWindow
    property alias actions: actionRow.data
    readonly property bool maximized: ownerWindow.visibility === Window.Maximized

    implicitHeight: 50
    color: Theme.surface

    function toggleMaximized() {
        if (maximized)
            ownerWindow.showNormal()
        else
            ownerWindow.showMaximized()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 6
        spacing: 14

        Item {
            objectName: "mainTitleDragArea"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: identity.implicitWidth

            RowLayout {
                id: identity
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Rectangle {
                    implicitWidth: 28
                    implicitHeight: 28
                    radius: 8
                    color: Theme.accentSoft
                    Text {
                        anchors.centerIn: parent
                        text: "≋"
                        color: Theme.accent
                        font.pixelSize: 22
                        font.bold: true
                    }
                }
                Text {
                    text: "UART Tool"
                    color: Theme.heading
                    font.pixelSize: 17
                    font.bold: true
                }
            }

            DragHandler {
                target: null
                acceptedButtons: Qt.LeftButton
                onActiveChanged: {
                    if (active)
                        bar.ownerWindow.startSystemMove()
                }
            }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onDoubleTapped: bar.toggleMaximized()
            }
        }

        RowLayout {
            id: actionRow
            spacing: 10
        }

        Row {
            WindowButton {
                objectName: "mainMinimizeButton"
                text: "−"
                description: "最小化"
                onClicked: bar.ownerWindow.showMinimized()
            }
            WindowButton {
                objectName: "mainMaximizeButton"
                text: bar.maximized ? "❐" : "□"
                description: bar.maximized ? "还原" : "最大化"
                onClicked: bar.toggleMaximized()
            }
            WindowButton {
                objectName: "mainCloseButton"
                text: "×"
                description: "关闭"
                onClicked: bar.ownerWindow.close()
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.divider
    }

    component WindowButton: ToolButton {
        required property string description
        width: 36
        height: 36
        hoverEnabled: true
        font.pixelSize: 17
        Accessible.name: description
        ToolTip.visible: hovered
        ToolTip.delay: 600
        ToolTip.text: description
    }
}
