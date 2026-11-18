import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: editor
    signal submitRequested()
    readonly property bool canAddParameter: parameters.count<32

    function addParameter() {
        if(!canAddParameter) return
        parameters.append({keyText:"",valueText:"",stepText:"0.1"})
        Qt.callLater(function() { rows.contentY=Math.max(0,rows.contentHeight-rows.height) })
    }

    ListModel {
        id: parameters
        ListElement { keyText: ""; valueText: ""; stepText: "0.1" }
    }

    function isNumber(text) {
        return /^[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?$/.test(text) && Number.isFinite(Number(text))
    }

    function encode() {
        const names=new Set()
        const pairs=[]
        for(let index=0;index<parameters.count;++index) {
            const parameter=parameters.get(index)
            const name=parameter.keyText.trim()
            const value=parameter.valueText.trim()
            const row=index+1
            if(!name || /[:,{}'"\x00-\x1f\x7f-\x9f]/.test(name))
                return {error:"第 "+row+" 行请填写有效参数名。"}
            if(names.has(name)) return {error:"参数名重复："+name}
            if(!isNumber(value)) return {error:"第 "+row+" 行请输入有效数值。"}
            names.add(name)
            pairs.push(name+":"+value)
        }
        return {text:pairs.join(",")}
    }

    component ParameterValue: TextField {
        id: value
        required property string stepText
        property bool wheelArmed: false
        signal valueAdjusted(string nextValue)
        signal stepChangedByUser(string nextStep)
        placeholderText: "数值"
        Accessible.name: "参数数值"
        selectByMouse: true
        maximumLength: 48
        color: Theme.text
        ToolTip.visible: hovered && !stepPopup.opened
        ToolTip.text: "点击后滚轮调整 · 右键设步长（当前 "+stepText+"）"
        onActiveFocusChanged: { if(!activeFocus) wheelArmed=false }

        function adjust(clicks) {
            const current=text.trim()
            if(current && !editor.isNumber(current)) return
            const next=Number(current)+clicks*Number(stepText)
            if(Number.isFinite(next)) valueAdjusted(String(Number(next.toPrecision(15))))
        }

        WheelHandler {
            target: null
            enabled: value.activeFocus && value.wheelArmed
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            property real pendingSteps: 0
            onEnabledChanged: { if(!enabled) pendingSteps=0 }
            onWheel: function(event) {
                const steps=event.angleDelta.y?event.angleDelta.y/120:event.pixelDelta.y/40
                pendingSteps+=steps
                const clicks=Math.trunc(pendingSteps)
                if(clicks!==0) {
                    pendingSteps-=clicks
                    value.adjust(clicks)
                }
            }
        }
        MouseArea {
            anchors.fill: parent
            z: 1
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            preventStealing: true
            onPressed: function(event) {
                if(event.button===Qt.LeftButton) {
                    value.wheelArmed=true
                    event.accepted=false
                }
            }
            onClicked: stepPopup.open()
        }
        Keys.onReturnPressed: function(event) {
            if(event.modifiers & Qt.ControlModifier) editor.submitRequested()
            else event.accepted=false
        }
        Keys.onEnterPressed: function(event) {
            if(event.modifiers & Qt.ControlModifier) editor.submitRequested()
            else event.accepted=false
        }

        Popup {
            id: stepPopup
            objectName: "parameterStepPopup"
            parent: value
            width: 222
            x: value.width-width; y: value.height+4
            focus: true
            padding: 10
            onAboutToShow: stepInput.text=value.stepText
            onOpened: { stepInput.forceActiveFocus(); stepInput.selectAll() }
            function applyStep() {
                if(!stepInput.validStep) return
                value.stepChangedByUser(stepInput.text.trim())
                close()
            }
            background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: 7 }
            contentItem: RowLayout {
                spacing: 6
                Text { text: "步长"; color: Theme.text }
                TextField {
                    id: stepInput
                    objectName: "parameterStepInput"
                    focus: true
                    Layout.fillWidth: true
                    maximumLength: 48
                    readonly property bool validStep: editor.isNumber(text.trim()) && Number(text)>0
                    Accessible.name: "滚轮步长"
                    selectByMouse: true
                    color: Theme.text
                    onAccepted: stepPopup.applyStep()
                }
                AppButton {
                    text: "确定"; implicitWidth: 50; implicitHeight: 30
                    enabled: stepInput.validStep
                    onClicked: stepPopup.applyStep()
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent; spacing: 4
        Flickable {
            id: rows
            objectName: "sendParameterRows"
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: width; contentHeight: parameterRows.height
            clip: true
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Column {
                id: parameterRows
                width: rows.width-(rows.contentHeight>rows.height?12:0)
                spacing: 5
                Repeater {
                    model: parameters
                    RowLayout {
                        id: row
                        required property int index
                        required property string keyText
                        required property string valueText
                        required property string stepText
                        width: parameterRows.width; height: 32; spacing: 5
                        Text {
                            Layout.preferredWidth: 20
                            Layout.alignment: Qt.AlignVCenter
                            horizontalAlignment: Text.AlignRight
                            text: row.index+1
                            font.pixelSize: 12
                            color: Theme.muted
                        }
                        TextField {
                            objectName: "parameterName_"+row.index
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.fillHeight: true
                            text: row.keyText
                            placeholderText: "参数名，如 kp"
                            Accessible.name: "参数名"
                            selectByMouse: true
                            maximumLength: 64
                            color: Theme.text
                            onTextEdited: parameters.setProperty(row.index,"keyText",text)
                        }
                        ParameterValue {
                            objectName: "parameterValue_"+row.index
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.fillHeight: true
                            text: row.valueText
                            stepText: row.stepText
                            onTextEdited: parameters.setProperty(row.index,"valueText",text)
                            onValueAdjusted: function(next) { parameters.setProperty(row.index,"valueText",next) }
                            onStepChangedByUser: function(next) { parameters.setProperty(row.index,"stepText",next) }
                        }
                        ToolButton {
                            objectName: "moveSendParameterUp_"+row.index
                            implicitWidth: 25; implicitHeight: 30
                            visible: parameters.count>1
                            enabled: row.index>0
                            text: "↑"; Accessible.name: "上移参数"
                            onClicked: parameters.move(row.index,row.index-1,1)
                        }
                        ToolButton {
                            implicitWidth: 25; implicitHeight: 30
                            visible: parameters.count>1
                            text: "×"; Accessible.name: "删除参数"
                            onClicked: parameters.remove(row.index)
                        }
                    }
                }
            }
        }
    }
}
