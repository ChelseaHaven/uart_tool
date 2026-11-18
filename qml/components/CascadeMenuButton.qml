import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppButton {
    id: button
    default property alias menuItems: picker.contentData
    property alias menu: picker
    property string categoryPrefix: ""
    readonly property int submenuWidth: 216
    property int rightCascadeWidth: 0
    signal opening()

    implicitHeight: 37
    leftPadding: 11; rightPadding: 12
    onClicked: {
        const buttonX=button.mapToItem(null,0,0).x
        const windowWidth=button.Window.window.width
        const rightSpace=windowWidth-buttonX-picker.width
        // Keep both levels visible when the button is in the middle of a narrow window.
        if(rightCascadeWidth>0) {
            picker.x=Math.max(0,Math.min(buttonX,windowWidth-picker.width-rightCascadeWidth))-buttonX
        } else {
            picker.x=rightSpace<submenuWidth && buttonX<submenuWidth
                ? Math.max(0,windowWidth-picker.width)-buttonX : 0
        }
        picker.open()
    }
    ToolTip.visible: hovered && label.truncated && !picker.visible
    ToolTip.text: text

    contentItem: RowLayout {
        spacing: 6
        Text {
            id: label
            objectName: "cascadeMenuLabel"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: button.text
            textFormat: Text.PlainText
            elide: Text.ElideRight
            font.pixelSize: 13
            color: Theme.text
        }
        Text { text: "⌄"; font.pixelSize: 18; color: Theme.secondary }
    }
    Menu {
        id: picker
        y: button.height+4
        width: 156
        height: Math.min(implicitHeight,320)
        cascade: true
        popupType: Popup.Item
        onAboutToShow: button.opening()

        delegate: MenuItem {
            hoverEnabled: true
            objectName: subMenu ? button.categoryPrefix+subMenu.objectName : ""
        }
    }
}
