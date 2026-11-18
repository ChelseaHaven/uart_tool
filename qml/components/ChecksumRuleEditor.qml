import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    property Item popupLayer: null
    required property QtObject protocol
    property var rule: ({})
    property int payloadOffset: 0
    property bool structureMode: false
    signal saved(var value)
    property string errorText: ""
    property var ids: ["crc8smbus","crc8maximdow","crc16modbus","sum8","sum16","xor8","crc","crc16ccittfalse","crc32isohdlc"]
    parent: dialog.popupLayer || Overlay.overlay; modal: true; title: "校验规则"
    width: parent?Math.min(730,parent.width-60):700
    height: parent?Math.min(650,parent.height-60):600
    x: parent?(parent.width-width)/2:0; y: parent?(parent.height-height)/2:0
    padding: 20
    function load(value) {
        rule=JSON.parse(JSON.stringify(value)); errorText=""
        nameInput.text=rule.name || "校验"
        enabledCheck.checked=rule.enabled!==false
        const index=ids.indexOf(rule.algorithm || "crc16modbus")
        algorithm.currentIndex=index>=0?index:ids.indexOf("crc")
        location.currentIndex=(rule.offset===undefined?-2:rule.offset)<0?0:1; offset.value=rule.offset>=0?rule.offset:0
        coverage.currentIndex=rule.start===0 && rule.end===rule.offset?0:rule.start===payloadOffset && rule.end===rule.offset?1:2
        if(structureMode) coverage.currentIndex=["before","payload","custom"].indexOf(rule.coverageMode || "before")
        start.value=rule.start || 0; end.value=rule.end===undefined?-2:rule.end
        order.currentIndex=rule.endian==="big"?1:0; early.checked=rule.early || false
        loadParameters(rule.algorithm==="crc"?rule:protocol.checksumPreset(rule.algorithm || "crc16modbus"))
    }
    function loadParameters(value) {
        bits.currentIndex=[8,16,32].indexOf(value.width || 16)
        polynomial.text=value.polynomial===undefined?"0x1021":String(value.polynomial)
        initial.text=value.initial===undefined?"0xFFFF":String(value.initial)
        xorOut.text=value.xorOut===undefined?"0x0":String(value.xorOut)
        reflectIn.checked=value.reflectInput!==false; reflectOut.checked=value.reflectOutput!==false
    }
    function widthBytes() {
        const id=ids[algorithm.currentIndex]
        if(["crc8smbus","crc8maximdow","sum8","xor8"].indexOf(id)>=0) return 1
        return id==="crc"?[1,2,4][bits.currentIndex]:id==="crc32isohdlc"?4:2
    }
    function build() {
        const position=structureMode || location.currentIndex===0?-widthBytes():offset.value
        return {name:nameInput.text,algorithm:ids[algorithm.currentIndex],enabled:enabledCheck.checked,
            offset:position,start:coverage.currentIndex===0?0:coverage.currentIndex===1?payloadOffset:start.value,
            end:coverage.currentIndex<2?position:end.value,endian:order.currentIndex===1?"big":"little",
            early:!structureMode && location.currentIndex===1 && early.checked,coverageMode:["before","payload","custom"][coverage.currentIndex],width:[8,16,32][bits.currentIndex],
            polynomial:polynomial.text,initial:initial.text,xorOut:xorOut.text,reflectInput:reflectIn.checked,reflectOutput:reflectOut.checked}
    }
    onOpened: load(rule)
    background: Rectangle { color: Theme.panel; radius: 10; border.color: Theme.border }
    contentItem: ScrollView {
        id: scroll; clip: true; contentWidth: availableWidth; contentHeight: column.implicitHeight
        ColumnLayout {
            id: column; width: scroll.availableWidth; spacing: 12
            FieldLabel { text: "名称" }
            TextField { id: nameInput; Layout.fillWidth: true }
            CheckBox { id: enabledCheck; text: "启用校验（关闭时仍保留字节）" }
            FieldLabel { text: "算法（选择预设后无需填写 CRC 参数）" }
            ConfigCombo {
                id: algorithm; objectName: "crcRuleAlgorithm"
                model: ["CRC-8/SMBUS","CRC-8/MAXIM-DOW","CRC-16/MODBUS","累加和 SUM8","累加和 SUM16","异或 XOR8","自定义 CRC","CRC-16/IBM-3740（CCITT-FALSE）","CRC-32/ISO-HDLC"]
                onActivated: { if(dialog.ids[currentIndex]!=="crc") dialog.loadParameters(dialog.protocol.checksumPreset(dialog.ids[currentIndex])) }
            }
            GridLayout {
                columns: 2; uniformCellWidths: true; Layout.fillWidth: true
                FieldLabel { visible: !dialog.structureMode; text: "结果位置" }
                FieldLabel { text: "结果字节序" }
                ConfigCombo { id: location; visible: !dialog.structureMode; model: ["帧尾（自动）","指定偏移"] }
                ConfigCombo { id: order; model: ["小端","大端"] }
                SpinBox { id: offset; visible: !dialog.structureMode && location.currentIndex===1; from: 0; to: 65535; editable: true }
                CheckBox { id: early; visible: !dialog.structureMode && location.currentIndex===1; text: "前缀收齐后提前校验" }
            }
            FieldLabel { text: "计算范围" }
            ConfigCombo { id: coverage; model: ["从帧头开始，到校验字节前","仅数据区，到校验字节前","指定字节范围"] }
            RowLayout {
                visible: coverage.currentIndex===2; Layout.fillWidth: true
                FieldLabel { text: "起点（含）" }
                SpinBox { id: start; from: -65536; to: 65535; editable: true }
                FieldLabel { text: "终点（不含）" }
                SpinBox { id: end; from: -65536; to: 65536; editable: true }
            }
            GridLayout {
                visible: algorithm.currentIndex===6; columns: 2; uniformCellWidths: true; Layout.fillWidth: true
                FieldLabel { text: "位宽" }
                FieldLabel { text: "标准多项式（如 0x1021）" }
                ConfigCombo { id: bits; model: [8,16,32] }
                TextField { id: polynomial; Layout.fillWidth: true }
                FieldLabel { text: "初始值" }
                FieldLabel { text: "最终异或" }
                TextField { id: initial; Layout.fillWidth: true }
                TextField { id: xorOut; Layout.fillWidth: true }
                CheckBox { id: reflectIn; text: "输入反转" }
                CheckBox { id: reflectOut; text: "输出反转" }
            }
            Text { text: dialog.structureMode?"结果位置由帧结构自动决定；常见校验只需选择算法，默认计算其前面的全部字节。":"位置从 0 开始，负数从帧尾计数。关闭一条规则只跳过检查，不删除帧内的字节。"; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: 11 }
        }
    }
    footer: ColumnLayout {
        Text { Layout.fillWidth: true; Layout.margins: 10; text: dialog.errorText; visible: text.length>0; wrapMode: Text.WordWrap; color: Theme.error }
        RowLayout {
            Layout.fillWidth: true; Layout.margins: 12
            Item { Layout.fillWidth: true }
            AppButton { text: "取消"; onClicked: dialog.close() }
            AppButton { objectName: "saveChecksumRule"; text: "确定"; primary: true; onClicked: {
                const value=dialog.build(); dialog.errorText=dialog.protocol.validateChecksumRule(value)
                if(!dialog.errorText) { dialog.saved(value); dialog.close() }
            } }
        }
    }
}
