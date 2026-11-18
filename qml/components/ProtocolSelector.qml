import QtQuick
import QtQuick.Controls
import QtQml.Models

CascadeMenuButton {
    id: selector
    required property QtObject protocol
    readonly property QtObject store: protocol.templateStore
    readonly property string appliedId: protocol.binaryConfiguration.templateId
    property string errorText: ""

    readonly property var protocols: Array.from(protocol.protocols).filter(entry=>entry.id!=="none")
    readonly property string selectedId: protocol.selectedProtocolId
    readonly property int templateMenuWidth: Math.min(submenuWidth,
        Math.max(132,(Window.window ? Window.window.width : 380)-menu.width-binaryMenu.width))

    objectName: "protocolSelector"
    text: selectedId==="binary" && protocol.binaryTemplateName ? protocol.binaryTemplateName
        : (protocols.find(entry=>entry.id===selectedId)?.name || "选择协议")
    Accessible.name: "数据协议"
    leftPadding: 15
    menu.objectName: "protocolQuickMenu"
    menu.width: 152
    rightCascadeWidth: binaryMenu.width+templateMenuWidth
    onOpening: store.refresh()

    menu.delegate: ProtocolMenuItem {
        objectName: "protocolCategory_binary"
        hoverEnabled: true
        onClicked: Qt.callLater(selector.useCurrentBinary)
    }

    function useCurrentBinary() {
        selectProtocol("binary")
        menu.dismiss()
    }

    component ProtocolMenuItem: MenuItem {
        id: option
        contentItem: Text {
            leftPadding: 28
            rightPadding: option.subMenu ? 22 : 0
            text: option.text
            font: option.font
            color: option.palette.windowText
            textFormat: Text.PlainText
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    function selectProtocol(id) {
        errorText=""
        protocol.selectProtocol(id)
    }

    function applyTemplate(id) {
        const record=store.loadTemplate(id)
        if(!record.configuration) {
            errorText=store.lastError
            return
        }
        if(protocol.applyBinaryConfiguration(record.configuration)) selectProtocol("binary")
        else errorText=protocol.configurationError
    }

    Connections {
        target: selector.protocol
        function onConfigurationChanged() { selector.errorText="" }
    }

    component TemplateGroup: Menu {
        id: group
        required property var entries
        width: selector.templateMenuWidth
        height: Math.min(implicitHeight, 320)

        MenuItem { text: "暂无模板"; enabled: false; visible: group.entries.length===0; height: visible?implicitHeight:0 }
        Instantiator {
            model: group.entries
            delegate: MenuItem {
                required property var modelData
                objectName: "binaryTemplateOption_"+modelData.id
                text: modelData.name
                checkable: true
                checked: selector.selectedId==="binary"
                    && (selector.appliedId===modelData.id || "builtin:"+selector.appliedId===modelData.id)
                onTriggered: selector.applyTemplate(modelData.id)
            }
            onObjectAdded: (index,item) => group.insertItem(index,item)
            onObjectRemoved: (index,item) => group.removeItem(item)
        }
    }

    Instantiator {
        model: selector.protocols.filter(entry=>entry.id!=="binary")
        delegate: ProtocolMenuItem {
            required property var modelData
            objectName: "protocolOption_"+modelData.id
            text: modelData.name
            checkable: true
            checked: selector.selectedId===modelData.id
            onTriggered: selector.selectProtocol(modelData.id)
        }
        onObjectAdded: (index,item) => selector.menu.insertItem(index,item)
        onObjectRemoved: (index,item) => selector.menu.removeItem(item)
    }

    Menu {
        id: binaryMenu
        objectName: "binary"
        title: "自定义二进制"
        width: 94

        delegate: MenuItem {
            hoverEnabled: true
            objectName: subMenu ? "binaryTemplateCategory_"+subMenu.objectName : ""
        }

        TemplateGroup {
            objectName: "builtIn"
            title: "内置模板"
            entries: selector.store.builtInEntries
        }
        TemplateGroup {
            objectName: "personal"
            title: "我的模板"
            entries: selector.store.entries
        }
    }
}
