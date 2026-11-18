import QtQuick
import QtQuick.Controls
import QtQml.Models

CascadeMenuButton {
    id: selector
    required property QtObject protocol
    readonly property var groups: protocol.binaryCommandGroups
    readonly property var channels: groups.length > 0 ? []
        : Array.from(protocol.binaryChannels).filter(channel=>channel.command.length>0)
    readonly property bool hasCommands: groups.length>0 || channels.length>0

    objectName: "receivedCommandSelector"
    Accessible.name: "接收命令"
    categoryPrefix: "receivedCommandCategory_"
    menu.objectName: "receivedCommandMenu"
    text: groups.length>0 ? "接收命令"
        : (channels.find(channel=>channel.id===protocol.selectedBinaryChannelId)?.name || "接收命令")

    function chooseGroupCommand(blockIndex, command) {
        protocol.selectBinaryCommand(blockIndex,command)
    }

    Instantiator {
        model: selector.groups
        delegate: Menu {
            id: groupMenu
            required property var modelData
            objectName: String(modelData.blockIndex)
            title: modelData.name
            width: selector.submenuWidth
            height: Math.min(implicitHeight,320)

            Instantiator {
                model: groupMenu.modelData.choices
                delegate: MenuItem {
                    required property var modelData
                    objectName: "receivedCommandOption_"+groupMenu.modelData.blockIndex+"_"+modelData.command
                    text: modelData.name
                    checkable: true
                    checked: modelData.command===groupMenu.modelData.value
                    onTriggered: Qt.callLater(selector.chooseGroupCommand,groupMenu.modelData.blockIndex,modelData.command)
                }
                onObjectAdded: (index,item) => groupMenu.insertItem(index,item)
                onObjectRemoved: (index,item) => groupMenu.removeItem(item)
            }
        }
        onObjectAdded: (index,item) => selector.menu.insertMenu(index,item)
        onObjectRemoved: (index,item) => selector.menu.removeMenu(item)
    }

    Instantiator {
        model: selector.channels
        delegate: MenuItem {
            required property var modelData
            objectName: "receivedChannelOption_"+modelData.id
            text: modelData.name
            checkable: true
            checked: modelData.id===selector.protocol.selectedBinaryChannelId
            onTriggered: selector.protocol.selectBinaryChannel(modelData.id)
        }
        onObjectAdded: (index,item) => selector.menu.insertItem(index,item)
        onObjectRemoved: (index,item) => selector.menu.removeItem(item)
    }
}
