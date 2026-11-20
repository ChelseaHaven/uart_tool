import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Uart.Ui 1.0

ComponentFrame {
    id: card
    required property var series
    required property int instanceId
    property bool paused: false
    property bool focusEnabled: true
    property bool referenceEnabled: true
    property real referenceValue: 0
    readonly property var channels: series ? series.channels : []
    property var selectedKeys: []
    objectName: "timePlotCard_"+instanceId
    title: "时序图"
    titleLabelWidth: plot.yLabelWidth
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
                focusEnabled: card.focusEnabled
                onPlotPressed: card.activateForKeyboard()
                referenceEnabled: card.referenceEnabled
                referenceValue: card.referenceValue
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 24
                width: parent.width-28; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                visible: card.channels.length > 0 && card.selectedKeys.length === 0
                text: "点击“选择参数”添加曲线"
                color: Theme.muted; font.pixelSize: 12
            }
        }
    }
    PlotParametersPopup {
        id: parameters
        objectName: "plotParametersPopup_" + card.instanceId
        parent: card
        trigger: parameterButton
        focusEnabled: card.focusEnabled
        focusToggleName: "plotFocusToggle_" + card.instanceId
        onFocusEdited: enabled => card.focusEnabled = enabled

        PlotReferenceEditor {
            nameSuffix: String(card.instanceId)
            axis: "y"; label: "水平参考线"
            lineEnabled: card.referenceEnabled; value: card.referenceValue
            onEnabledEdited: enabled => card.referenceEnabled = enabled
            onValueEdited: value => card.referenceValue = value
        }
        Text {
            visible: card.channels.length > 0
            text: "当前解析参数 · 最多 8 条曲线"
            color: Theme.muted
            font.pixelSize: 11
        }
        ScrollView {
            visible: card.channels.length > 0
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredHeight: Math.min(220,parameterList.implicitHeight)
            contentWidth: availableWidth
            Column {
                id: parameterList; width: parent.width
                Repeater {
                    model: card.channels
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
    Connections {
        target: card.series
        function onChannelsChanged() { card.selectedKeys=card.selectedKeys.filter(key=>card.series.hasChannel(key)) }
    }
}
