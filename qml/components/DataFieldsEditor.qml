import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: editor
    required property var fieldModel
    property bool variableAllowed: false
    property string errorText: ""
    property int revision: 0
    property var types: ["float32","float64","uint8","int8","uint16","int16","uint32","int32","uint64","int64","bytes","padding"]
    property var typeLabels: ["float32","float64","uint8","int8","uint16","int16","uint32","int32","uint64","int64","原始字节 bytes","保留字节"]
    implicitHeight: column.implicitHeight
    function sizeOf(type) { return type==="float64" || type.endsWith("64")?8:type==="float32" || type.endsWith("32")?4:type.endsWith("16")?2:1 }
    function setField(index,key,value) { fieldModel.setProperty(index,key,value); revision++; errorText="" }
    component Input: TextField {
        implicitHeight: 36; color: Theme.text; selectByMouse: true
        background: Rectangle { radius: 5; color: Theme.surface; border.color: Theme.border }
    }
    ColumnLayout {
        id: column; width: parent.width; spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Text { text: "按顺序排列，偏移与帧长自动计算"; color: Theme.secondary; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            FieldLabel { text: "字节序" }
            ConfigCombo {
                id: groupOrder; objectName: "binaryGroupEndian"; Layout.fillWidth: false; Layout.preferredWidth: 90
                model: ["小端","大端","混合"]
                currentIndex: {
                    let ignored=editor.revision,little=false,big=false
                    for(let i=0;i<editor.fieldModel.count;i++) if(editor.fieldModel.get(i).fieldEndian==="big") big=true; else little=true
                    return little && big?2:big?1:0
                }
                onActivated: {
                    if(currentIndex>1) return
                    const order=currentIndex===1?"big":"little"
                    for(let i=0;i<editor.fieldModel.count;i++) editor.fieldModel.setProperty(i,"fieldEndian",order)
                    editor.revision++
                }
            }
            AppButton {
                objectName: "addDataField"; text: "+ 添加字段"; enabled: editor.fieldModel.count<64
                onClicked: {
                    const fields=editor.fieldModel
                    if(fields.count && fields.get(fields.count-1).variable) { editor.errorText="先关闭末尾字段的变长选项，再添加字段。"; return }
                    let n=fields.count+1,names=[]
                    for(let i=0;i<fields.count;i++) names.push(fields.get(i).fieldName)
                    while(names.indexOf("field"+n)>=0) n++
                    fields.append({fieldName:"field"+n,fieldType:"float32",fieldEndian:groupOrder.currentIndex===1?"big":"little",fixedCount:"1",variable:false})
                    editor.revision++; editor.errorText=""
                }
            }
        }
        Repeater {
            model: editor.fieldModel
            delegate: Rectangle {
                id: row
                required property int index
                required property string fieldName
                required property string fieldType
                required property string fieldEndian
                required property string fixedCount
                required property bool variable
                Layout.fillWidth: true; implicitHeight: details.visible?94:56
                radius: 6; color: Theme.panel; border.color: Theme.border
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 8; spacing: 5
                    RowLayout {
                        Layout.fillWidth: true; spacing: 7
                        Input { objectName: "binaryFieldName"+row.index; Layout.fillWidth: true; text: row.fieldName; placeholderText: "字段名称"; onTextEdited: editor.setField(row.index,"fieldName",text) }
                        ConfigCombo {
                            objectName: "binaryFieldType"+row.index; Layout.fillWidth: false; Layout.preferredWidth: 135
                            model: editor.typeLabels; currentIndex: editor.types.indexOf(row.fieldType)
                            onActivated: { editor.setField(row.index,"fieldType",editor.types[currentIndex]); if(currentIndex===11) editor.setField(row.index,"variable",false) }
                        }
                        FieldLabel { text: "数量" }
                        Input { Layout.preferredWidth: 54; text: row.fixedCount; enabled: !row.variable; validator: IntValidator { bottom: 1; top: 4096 } onTextEdited: editor.setField(row.index,"fixedCount",text) }
                        ConfigCombo { Layout.fillWidth: false; Layout.preferredWidth: 82; model: ["小端","大端"]; enabled: editor.sizeOf(row.fieldType)>1; currentIndex: row.fieldEndian==="big"?1:0; onActivated: editor.setField(row.index,"fieldEndian",currentIndex===1?"big":"little") }
                        AppButton { text: "↑"; implicitWidth: 30; enabled: row.index>0 && !row.variable; onClicked: { editor.fieldModel.move(row.index,row.index-1,1); editor.revision++ } }
                        AppButton { text: "↓"; implicitWidth: 30; enabled: row.index<editor.fieldModel.count-1 && !row.variable && !editor.fieldModel.get(row.index+1).variable; onClicked: { editor.fieldModel.move(row.index,row.index+1,1); editor.revision++ } }
                        AppButton { objectName: "removeDataField"+row.index; text: "删除"; implicitWidth: 54; onClicked: { editor.fieldModel.remove(row.index); editor.revision++ } }
                    }
                    RowLayout {
                        id: details; visible: (editor.variableAllowed || row.variable) && row.index===editor.fieldModel.count-1 && row.fieldType!=="padding"
                        CheckBox { text: row.fieldType==="bytes"?"使用剩余字节（变长）":"数量随帧长度变化"; checked: row.variable; onToggled: editor.setField(row.index,"variable",checked) }
                    }
                }
            }
        }
        Text { text: editor.errorText; visible: text.length>0; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.error }
    }
}
