import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Uart.Ui 1.0

ComponentFrame {
    id: card
    required property var series
    required property int instanceId
    property bool paused: false
    property var selectedKeys: []
    objectName: "timePlotCard_"+instanceId
    title: "时序图"
    function toggleChannel(key,checked) {
        let keys=selectedKeys.filter(k=>k!==key)
        if(checked && keys.length<8) keys.push(key)
        selectedKeys=keys
    }
    function nameFor(key) {
        if(!series) return key
        const channel=series.channels.find(c=>c.key===key)
        return channel?channel.name:key
    }
    headerActions: [
        AppButton {
            id: parameterButton; objectName: "plotParameters_"+card.instanceId
            text: card.selectedKeys.length?"参数（"+card.selectedKeys.length+"）":"选择参数"
            implicitWidth: 108; implicitHeight: 29
            enabled: card.series!==null && card.series.channels.length>0
            onClicked: parameters.open()
        },
        AppButton {
            text: "自动"; implicitWidth: 46; implicitHeight: 29
            onClicked: plot.resetView()
            ToolTip.visible: hovered; ToolTip.text: "恢复实时跟随和自动量程"
        }
    ]
    ColumnLayout {
        anchors.fill: parent; spacing: 5
        Flow {
            Layout.fillWidth: true; Layout.preferredHeight: Math.max(0,childrenRect.height); spacing: 12
            Repeater {
                model: card.selectedKeys
                Text {
                    required property string modelData; required property int index
                    text: "● "+card.nameFor(modelData); font.pixelSize: 11
                    color: { const changed=plot.textColor; return plot.colorAt(index) }
                }
            }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 80
            TimeSeriesPlot {
                id: plot; objectName: "timePlot_"+card.instanceId
                anchors.fill: parent; stream: card.series; channels: card.selectedKeys; paused: card.paused
                textColor: Theme.secondary; gridColor: Theme.divider
            }
            Text {
                anchors.centerIn: parent; width: parent.width-28; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                visible: card.selectedKeys.length===0
                text: !card.series || card.series.channels.length===0?"当前协议尚未解析出数值参数":"点击“选择参数”添加曲线"
                color: Theme.muted; font.pixelSize: 12
            }
        }
    }
    Popup {
        id: parameters; objectName: "plotParametersPopup_"+card.instanceId; parent: card
        width: 280; height: Math.min(350,content.implicitHeight+20); padding: 10; font.pixelSize: 13
        onAboutToShow: {
            const position=parameterButton.mapToItem(parent,0,parameterButton.height+4)
            x=Math.max(0,Math.min(position.x,card.width-width)); y=position.y
        }
        background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: 8 }
        contentItem: ColumnLayout {
            id: content; spacing: 5
            Text { text: "当前解析参数 · 最多 8 条曲线"; color: Theme.muted; font.pixelSize: 11 }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredHeight: Math.min(280,parameterList.implicitHeight)
                contentWidth: availableWidth
                Column {
                    id: parameterList; width: parent.width
                    Repeater {
                        model: card.series?card.series.channels:[]
                        CheckDelegate {
                            required property var modelData
                            required property int index
                            objectName: "plotChannel_"+card.instanceId+"_"+index
                            width: parameterList.width; height: 34; text: modelData.name
                            checked: card.selectedKeys.indexOf(modelData.key)>=0
                            enabled: checked || card.selectedKeys.length<8
                            onToggled: card.toggleChannel(modelData.key,checked)
                        }
                    }
                }
            }
        }
    }
    Connections {
        target: card.series
        function onChannelsChanged() { card.selectedKeys=card.selectedKeys.filter(key=>card.series.hasChannel(key)) }
    }
}
