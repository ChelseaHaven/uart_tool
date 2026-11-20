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
    property bool xReferenceEnabled: true
    property bool yReferenceEnabled: true
    property real xReferenceValue: 0
    property real yReferenceValue: 0
    property string xKey: ""
    property string yKey: ""
    readonly property var channels: series?series.channels:[]
    objectName: "xyPlotCard_"+instanceId
    title: "XY 轨迹图"

    function channelName(key) {
        return channels.find(channel => channel.key === key)?.name || "—"
    }

    component AxisSelector: ConfigCombo {
        required property string selectedKey
        model: card.channels
        textRole: "name"
        valueRole: "key"
        currentIndex: indexOfValue(selectedKey)
        displayText: currentIndex < 0 ? "选择字段" : currentText
        enabled: count > 0
        implicitHeight: 32
    }

    headerActions: [
        AppButton {
            id: parameterButton
            objectName: "xyParameters_" + card.instanceId
            text: "选择参数"
            implicitWidth: 108
            implicitHeight: 29
            onClicked: parameters.open()
        },
        AppButton {
            text: "自动"; implicitWidth: 46; implicitHeight: 29
            onClicked: plot.resetView()
            ToolTip.visible: hovered; ToolTip.text: "恢复自动量程"
        }
    ]
    ColumnLayout {
        anchors.fill: parent; spacing: 6
        Text {
            Layout.fillWidth: true
            visible: card.channels.length > 0
            text: {
                if (!card.xKey && !card.yKey) return "分别选择 X 和 Y 轴字段"
                const axes = "X  " + card.channelName(card.xKey) + "    Y  " + card.channelName(card.yKey)
                return card.xKey && card.yKey && plot.pointCount === 0 ? axes + " · 等待数据" : axes
            }
            textFormat: Text.PlainText
            elide: Text.ElideRight
            font.pixelSize: 11
            color: Theme.secondary
        }
        XYPlot {
            id: plot; objectName: "xyPlot_"+card.instanceId
            Layout.fillWidth: true; Layout.fillHeight: true
            stream: card.series
            xChannel: card.xKey; yChannel: card.yKey; paused: card.paused
            textColor: Theme.secondary; gridColor: Theme.divider; traceColor: Theme.accentText
            focusEnabled: card.focusEnabled
            onPlotPressed: card.activateForKeyboard()
            xReferenceEnabled: card.xReferenceEnabled; xReferenceValue: card.xReferenceValue
            yReferenceEnabled: card.yReferenceEnabled; yReferenceValue: card.yReferenceValue
        }
    }
    PlotParametersPopup {
        id: parameters
        objectName: "xyParametersPopup_" + card.instanceId
        parent: card
        trigger: parameterButton
        focusEnabled: card.focusEnabled
        focusToggleName: "xyFocusToggle_" + card.instanceId
        onFocusEdited: enabled => card.focusEnabled = enabled

        PlotReferenceEditor {
            nameSuffix: "x_" + card.instanceId
            axis: "x"; label: "垂直参考线"
            lineEnabled: card.xReferenceEnabled; value: card.xReferenceValue
            onEnabledEdited: enabled => card.xReferenceEnabled = enabled
            onValueEdited: value => card.xReferenceValue = value
        }
        PlotReferenceEditor {
            nameSuffix: "y_" + card.instanceId
            axis: "y"; label: "水平参考线"
            lineEnabled: card.yReferenceEnabled; value: card.yReferenceValue
            onEnabledEdited: enabled => card.yReferenceEnabled = enabled
            onValueEdited: value => card.yReferenceValue = value
        }
        RowLayout {
            Layout.fillWidth: true
            Text { text: "X 轴"; color: Theme.secondary }
            AxisSelector {
                objectName: "xyAxis_" + card.instanceId + "_x"
                Accessible.name: "X 轴字段"
                selectedKey: card.xKey
                onActivated: card.xKey = currentValue
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Y 轴"; color: Theme.secondary }
            AxisSelector {
                objectName: "xyAxis_" + card.instanceId + "_y"
                Accessible.name: "Y 轴字段"
                selectedKey: card.yKey
                onActivated: card.yKey = currentValue
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
