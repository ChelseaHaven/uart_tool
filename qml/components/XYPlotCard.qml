import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Uart.Ui 1.0

ComponentFrame {
    id: card
    required property var series
    required property int instanceId
    property bool paused: false
    property string xKey: ""
    property string yKey: ""
    readonly property var channels: series?series.channels:[]
    objectName: "xyPlotCard_"+instanceId
    title: "XY 轨迹图"

    component AxisSelector: ConfigCombo {
        required property string axis
        required property string selectedKey
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        implicitHeight: 32
        model: card.channels
        textRole: "name"; valueRole: "key"
        currentIndex: card.channels.findIndex(channel=>channel.key===selectedKey)
        displayText: currentIndex<0?"选择字段":currentText
        enabled: count>0
        Accessible.name: axis+" 轴字段"
        ToolTip.visible: hovered && currentIndex>=0
        ToolTip.text: currentText
    }

    headerActions: [
        AppButton {
            text: "自动"; implicitWidth: 46; implicitHeight: 29
            onClicked: plot.resetView()
            ToolTip.visible: hovered; ToolTip.text: "恢复自动量程"
        }
    ]
    ColumnLayout {
        anchors.fill: parent; spacing: 6
        RowLayout {
            Layout.fillWidth: true; spacing: 6
            Text { text: "X"; color: Theme.secondary; font.bold: true }
            AxisSelector {
                objectName: "xyXChannel_"+card.instanceId
                axis: "X"; selectedKey: card.xKey
                onActivated: card.xKey=currentValue
            }
            Text { text: "Y"; color: Theme.secondary; font.bold: true }
            AxisSelector {
                objectName: "xyYChannel_"+card.instanceId
                axis: "Y"; selectedKey: card.yKey
                onActivated: card.yKey=currentValue
            }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            XYPlot {
                id: plot; objectName: "xyPlot_"+card.instanceId
                anchors.fill: parent; stream: card.series
                xChannel: card.xKey; yChannel: card.yKey; paused: card.paused
                textColor: Theme.secondary; gridColor: Theme.divider; traceColor: Theme.accentText
            }
            Text {
                anchors.centerIn: parent; width: parent.width-20
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                visible: plot.pointCount===0
                text: {
                    if(card.channels.length===0) return "当前协议尚未解析出数值参数"
                    if(!card.xKey || !card.yKey) return "分别选择 X 和 Y 轴字段"
                    return "等待同一帧中的 X、Y 数据"
                }
                color: Theme.muted; font.pixelSize: 12
            }
        }
    }
    Connections {
        target: card.series
        function onChannelsChanged() {
            if(!card.series.hasChannel(card.xKey)) card.xKey=""
            if(!card.series.hasChannel(card.yKey)) card.yKey=""
        }
    }
}
