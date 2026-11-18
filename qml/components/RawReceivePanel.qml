import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Uart.Ui 1.0

Item {
    id: panel
    required property QtObject session
    required property bool sourceConnected
    property bool autoScroll: true
    property bool updatingText: false
    property bool pendingRefresh: true
    readonly property bool followLive: autoScroll && !output.selecting
        && (!output.hasSelection || (!output.activeFocus && !output.copyMenuOpen))
    LogDocumentSettings { document: output.textDocument; rxColor: Theme.accentText; txColor: Theme.logTx }
    onAutoScrollChanged: { if(autoScroll) Qt.callLater(scrollToEnd) }
    onVisibleChanged: { if(visible && pendingRefresh) Qt.callLater(refreshSnapshot) }
    Component.onCompleted: refreshSnapshot()
    ColumnLayout {
        anchors.fill: parent; spacing: 12
        Rectangle {
            visible: panel.session.cacheLimited; Layout.fillWidth: true; implicitHeight: warning.implicitHeight+16
            radius: 5; color: Theme.warningSurface
            Text { id: warning; anchors.fill: parent; anchors.margins: 8; wrapMode: Text.WordWrap; text: "历史内容已达到上限，当前仅保留最近的数据。原始缓存上限为 256 KiB。"; color: Theme.warning; font.pixelSize: 11 }
        }
        Rectangle {
            id: outputPanel; clip: true; Layout.fillWidth: true; Layout.fillHeight: true
            color: Theme.input; radius: 7; border.color: Theme.border
            Flickable {
                id: logScroll
                objectName: "receiveFlickable"
                anchors.fill: parent; anchors.margins: 1; clip: true
                contentWidth: width
                contentHeight: Math.max(height,output.implicitHeight)
                flickableDirection: Flickable.VerticalFlick
                acceptedButtons: Qt.NoButton
                boundsBehavior: Flickable.StopAtBounds
                onContentHeightChanged: panel.followTail()
                onHeightChanged: { if(panel.followLive) Qt.callLater(panel.followTail) }
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }
                ReceiveTextEdit {
                    id: output
                    objectName: "receiveText"
                    width: Math.max(0,logScroll.width-14)
                    height: Math.max(logScroll.height,implicitHeight)
                    font.pixelSize: 13; padding: 14
                    Accessible.name: "串口收发记录"
                }
            }
            Column {
                anchors.centerIn: parent; width: parent.width-48; spacing: 12
                visible: output.length===0
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "≋"; visible: outputPanel.height>190; font.pixelSize: 46; color: Theme.accentSoft }
                Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: !panel.session.showRx && !panel.session.showTx ? "请选择收发方向" : panel.sourceConnected ? "等待串口数据" : "从连接你的设备开始"; color: Theme.secondary; font.pixelSize: outputPanel.height<180?14:18; wrapMode: Text.WordWrap }
                Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; visible: outputPanel.height>110; text: !panel.session.showRx && !panel.session.showTx ? "勾选 RX 或 TX 查看对应记录。" : "支持 UTF-8 文本和 HEX。"; color: Theme.muted; font.pixelSize: 12; lineHeight: 1.6 }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            CheckBox {
                objectName: "receiveRxFilter"
                text: "显示 RX"; checked: panel.session.showRx; font.pixelSize: 12; padding: 0
                palette.windowText: Theme.accentText
                onToggled: panel.session.showRx=checked
            }
            CheckBox {
                objectName: "receiveTxFilter"
                text: "显示 TX"; checked: panel.session.showTx; font.pixelSize: 12; padding: 0
                palette.windowText: Theme.logTx
                onToggled: panel.session.showTx=checked
            }
        }
    }

    function scrollToEnd() {
        const flick=logScroll
        flick.contentY=Math.max(0,flick.contentHeight-flick.height)
    }
    function followTail() {
        if(panel.followLive && !panel.updatingText) panel.scrollToEnd()
    }
    function refreshSnapshot() {
        if(!panel.visible) return
        const oldY=logScroll.contentY
        panel.updatingText=true
        output.text=panel.session.displayText
        panel.pendingRefresh=false
        panel.updatingText=false
        if(panel.followLive) { panel.scrollToEnd(); Qt.callLater(panel.followTail) }
        else logScroll.contentY=Math.max(0,Math.min(oldY,Math.max(0,logScroll.contentHeight-logScroll.height)))
    }
    Connections {
        target: panel.session
        function onDisplayUpdate(text, removeCharacters, reset) {
            if(!panel.visible) { panel.pendingRefresh=true; return }
            if(panel.pendingRefresh) { panel.refreshSnapshot(); return }
            const flick=logScroll
            const oldY=flick.contentY
            const start=output.selectionStart, end=output.selectionEnd
            const caret=output.cursorPosition
            const removedHeight=removeCharacters>0 ? output.positionToRectangle(removeCharacters).y-output.positionToRectangle(0).y : 0
            panel.updatingText=true
            if(reset) output.text=text
            else {
                if(removeCharacters>0) output.remove(0,removeCharacters)
                if(text.length>0) output.insert(output.length,text)
                output.cursorPosition=Math.max(0,Math.min(output.length,caret-removeCharacters))
                if(start!==end) output.select(Math.max(0,start-removeCharacters),Math.max(0,end-removeCharacters))
            }
            panel.updatingText=false
            if(panel.followLive) panel.scrollToEnd()
            else flick.contentY=Math.max(0,Math.min(Math.max(0,flick.contentHeight-flick.height),reset?oldY:oldY-removedHeight))
            // TextEdit finishes measuring on the next UI update; follow the final height too.
            if(panel.followLive) Qt.callLater(panel.followTail)
        }
    }
}
