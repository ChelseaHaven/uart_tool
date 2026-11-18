import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: dialog
    property Item popupLayer: null
    required property QtObject protocol
    property var configuration: ({})
    property var blocks: []
    property string errorText: ""
    property int editingCheck: -1
    property var messages: []
    property int currentCommand: 0
    property var kinds: ["header","length","command","field","payload","check","tail","padding"]
    property int bodyOffset: 2
    ListModel { id: dataFields }
    objectName: "frameStructureEditor"
    implicitHeight: column.implicitHeight
    function clone(value) { return JSON.parse(JSON.stringify(value)) }
    function bytes(hex) { return (hex || "").replace(/\s/g,"").length/2 }
    function label(kind) { return ({header:"帧头",length:"长度",command:"命令字",field:"公共字段",payload:"数据区",check:"校验",tail:"帧尾",padding:"保留字节"})[kind] || kind }
    function checkBlock(rule,position) {
        let r=clone(rule)
        const coverage=r.start===0 && r.end===position?"before":r.start===configuration.payloadOffset && r.end===position?"payload":"custom"
        return {kind:"check",rule:r,coverage:coverage}
    }
    function fieldRows() {
        let result=[],offset=bodyOffset
        for(let i=0;i<dataFields.count;i++) {
            const f=dataFields.get(i)
            result.push({name:f.fieldName,type:f.fieldType,endian:f.fieldEndian,offset:offset,count:Number(f.fixedCount),variable:f.variable})
            offset+=typeBytes(f.fieldType)*Number(f.fixedCount)
        }
        return result
    }
    function loadFields(values,end) {
        const rows=alignedFields(values,end===undefined?bodyOffset:end)
        dataFields.clear()
        for(const f of rows) dataFields.append({fieldName:f.name,fieldType:f.type,fieldEndian:f.endian || "little",fixedCount:String(f.count || 1),variable:!!f.variable})
    }
    function alignedFields(values,end) {
        let result=[],offset=bodyOffset,n=1,names=values.map(f=>f.name)
        function reserve(size) {
            while(size>0) {
                while(names.indexOf("reserved"+n)>=0) n++
                const count=Math.min(size,4096),name="reserved"+n++
                result.push({name:name,type:"padding",offset:offset,count:count,variable:false,endian:"little"})
                names.push(name); offset+=count; size-=count
            }
        }
        for(const f of clone(values).sort((a,b)=>a.offset-b.offset)) {
            reserve(Math.max(0,f.offset-offset)); result.push(f); offset=f.offset+typeBytes(f.type)*f.count
        }
        if(!result.some(f=>f.variable)) reserve(Math.max(0,end-offset))
        return result
    }
    function loadLegacy() {
        errorText=""
        const c=configuration
        messages=clone(c.messages || []); currentCommand=0
        bodyOffset=c.mode==="variable" || c.commandOffset>=0 || (c.structure && c.structure.length)?c.payloadOffset:Math.min.apply(null,c.fields.map(f=>f.offset))
        const legacyWidth=c.checksum==="none"?0:c.checksum==="sum16" || c.checksum==="crc16modbus"?2:1
        if(!messages.length) loadFields(c.fields.filter(f=>f.offset>=bodyOffset),c.mode==="fixed"?c.frameSize-(c.trailerBytes>=0?c.trailerBytes:legacyWidth):bodyOffset)
        if(c.commandOffset>=0) ensureCommands()
        const selected=messages.findIndex(m=>Number(m.command)===Number(c.commandValue)); if(selected>=0) currentCommand=selected
        if(messages.length) loadFields(messages[currentCommand].fields,messages[currentCommand].payloadSize>=0?bodyOffset+messages[currentCommand].payloadSize:bodyOffset)
        maximum.value=c.maxFrameSize || 4096
        if(c.structure && c.structure.length) { blocks=clone(c.structure).map(b=>{ if(b.kind==="command" && b.value===undefined) b.value=c.commandValue || "0x0"; return b }); return }
        let result=[{kind:"header",hex:c.header}], spans=[], end=bytes(c.header)
        const payload=bodyOffset
        if(c.mode==="variable") spans.push({at:c.lengthOffset,size:c.lengthBytes,block:{kind:"length",bytes:c.lengthBytes,endian:c.lengthEndian,meaning:c.lengthMeaning,includesTrailer:c.lengthIncludesChecksum}})
        if(c.commandOffset>=0) spans.push({at:c.commandOffset,size:c.commandBytes,block:{kind:"command",name:"command",bytes:c.commandBytes,endian:c.commandEndian,value:c.commandValue || "0x0"}})
        for(const f of c.fields) if(f.offset<payload && f.offset!==c.commandOffset) spans.push({at:f.offset,size:typeBytes(f.type),block:{kind:"field",name:f.name,type:f.type,endian:f.endian}})
        for(const r of (c.checks || [])) if(r.offset>=0) spans.push({at:r.offset,size:r.width/8,block:checkBlock(r,r.offset)})
        spans.sort((a,b)=>a.at-b.at)
        function gaps(count) { while(count>0) { const size=Math.min(16,count); result.push({kind:"padding",bytes:size}); count-=size } }
        for(const s of spans) { gaps(s.at-end); result.push(s.block); end=s.at+s.size }
        gaps(payload-end); result.push({kind:"payload"})
        const legacy=c.checksum==="none"?0:c.checksum==="sum16" || c.checksum==="crc16modbus"?2:1
        const trailer=c.trailerBytes>=0?c.trailerBytes:legacy
        spans=[]; end=0
        for(const r of (c.checks || [])) if(r.offset<0) spans.push({at:trailer+r.offset,size:r.width/8,block:checkBlock(r,r.offset)})
        if(legacy) spans.push({at:trailer-legacy,size:legacy,block:checkBlock({name:"整帧校验",algorithm:c.checksum,endian:c.checksumEndian,offset:-legacy,start:c.checksumStart,end:c.checksumEnd<0?-legacy:c.checksumEnd,enabled:true},-legacy)})
        if(c.tail) spans.push({at:trailer-bytes(c.tail),size:bytes(c.tail),block:{kind:"tail",hex:c.tail}})
        spans.sort((a,b)=>a.at-b.at)
        for(const s of spans) { gaps(s.at-end); result.push(s.block); end=s.at+s.size }
        gaps(trailer-end); blocks=result
    }
    function typeBytes(type) { return type==="float64" || type.endsWith("64")?8:type==="float32" || type.endsWith("32")?4:type.endsWith("16")?2:1 }
    function ensureCommands() {
        if(messages.length) return
        const c=configuration,offset=c.mode==="variable" || c.commandOffset>=0 || (c.structure && c.structure.length)?c.payloadOffset:Math.min.apply(null,c.fields.map(f=>f.offset))
        const command=c.commandOffset>=0?(c.commandValue || "0x0"):"0x1"
        messages=[{command:command,name:"命令 "+command,payloadSize:-1,fields:fieldRows()}]
        currentCommand=0
    }
    property bool loadingConfiguration: false
    property var fieldModels: ({})
    property int nextKey: 0
    property int addingCommandAt: -1
    Component { id: fieldsFactory; ListModel {} }
    function rows(model) {
        let result=[],offset=0
        for(let i=0;i<model.count;i++) {
            const f=model.get(i)
            result.push({name:f.fieldName,type:f.fieldType,endian:f.fieldEndian,offset:offset,count:Number(f.fixedCount),variable:f.variable})
            offset+=typeBytes(f.fieldType)*Number(f.fixedCount)
        }
        return result
    }
    function fill(model,values) {
        model.clear()
        for(const f of values) model.append({fieldName:f.name,fieldType:f.type,fieldEndian:f.endian || "little",fixedCount:String(f.count || 1),variable:!!f.variable})
    }
    function modelFor(block) { return fieldModels[block._key] || null }
    function owner(items,index) { for(let i=index-1;i>=0;i--) if(items[i].kind==="command") return i; return -1 }
    function commandIndex(block) { return (block.messages || []).findIndex(m=>Number(m.command)===Number(block.value)) }
    function commandName(block) { const m=(block.messages || [])[commandIndex(block)]; return m?m.name:"" }
    function snapshot() {
        let result=clone(blocks)
        for(let i=0;i<result.length;i++) if(result[i].kind==="payload") {
            result[i].fields=rows(modelFor(result[i]))
            const at=owner(result,i)
            if(at>=0) {
                if(!result[i].variants) result[i].variants={}
                result[i].variants[result[at].value]=clone(result[i].fields)
                for(const m of result[at].messages) if(!result[i].variants[m.command]) result[i].variants[m.command]=clone(result[i].fields)
            }
        }
        return result
    }
    function draftState() {
        let parts=snapshot()
        function fields(values) { return values.map(f=>({name:f.name,type:f.type,count:Number(f.count),endian:f.endian || "little",variable:!!f.variable})) }
        for(let i=0;i<parts.length;i++) {
            const b=parts[i]; delete b._key
            if(b.hex!==undefined) b.hex=b.hex.replace(/\s/g,"").toUpperCase()
            if(b.kind==="command") delete b.value // Selecting a command only changes the editing view.
            if(b.kind==="payload") {
                b.fields=fields(b.fields)
                for(const key in b.variants) b.variants[key]=fields(b.variants[key])
                if(owner(parts,i)>=0) delete b.fields
                else delete b.variants
            }
        }
        return {blocks:parts,maxFrameSize:maximum.value}
    }
    function loadConfiguration(c) {
        loadingConfiguration=true
        blocks=[]
        for(const key in fieldModels) fieldModels[key].destroy()
        fieldModels={}; configuration=clone(c); loadLegacy()
        let loaded=clone(blocks),single=loaded.filter(b=>b.kind==="command").length<=1 && loaded.filter(b=>b.kind==="payload").length===1
        for(let i=0;i<loaded.length;i++) {
            let b=loaded[i]; b._key="part"+(nextKey++)
            if(b.kind==="command") {
                if(single && c.messages && c.messages.length) b.messages=c.messages.map(m=>({command:m.command,name:m.name}))
                if(!b.messages || !b.messages.length) b.messages=[{command:b.value || "0x1",name:"命令 "+(b.value || "0x1")}]
                if(commandIndex(b)<0) b.value=b.messages[0].command
            }
            if(b.kind==="payload") {
                b.fields=!single && b.fields?b.fields:fieldRows()
                b.variants=b.variants || {}
                if(single && c.messages && c.messages.length) {
                    for(const m of c.messages) b.variants[m.command]=alignedFields(m.fields,m.payloadSize>=0?bodyOffset+m.payloadSize:bodyOffset)
                    const at=owner(loaded,i); if(at>=0) b.fields=b.variants[loaded[at].value] || b.fields
                }
                const model=fieldsFactory.createObject(dialog); fill(model,b.fields); fieldModels[b._key]=model
            }
        }
        blocks=loaded; loadingConfiguration=false; errorText=""
    }
    function build() {
        let items=snapshot(),base=clone(configuration); base.maxFrameSize=maximum.value
        const commands=items.filter(b=>b.kind==="command"),payloads=items.filter(b=>b.kind==="payload")
        base.messages=[]; base.payloadOffset=0; base.commandOffset=-1
        if(payloads.length===1 && commands.length<=1) {
            base.fields=payloads[0].fields
            if(commands.length) {
                const cmd=commands[0],data=payloads[0]
                base.commandValue=cmd.value
                base.messages=cmd.messages.map(m=>({command:m.command,name:m.name,payloadSize:-1,fields:data.variants[m.command] || data.fields}))
            }
        }
        for(const b of items) delete b._key
        const result=protocol.compileFrameStructure(items,base); errorText=result.error
        if(errorText) throw new Error(errorText)
        return result.configuration
    }
    function change(index,key,value) { let copy=snapshot(); copy[index][key]=value; blocks=copy; errorText="" }
    function move(index,delta) {
        const target=index+delta
        if(target<1 || target>=blocks.length) return
        let copy=snapshot(),item=copy.splice(index,1)[0]; copy.splice(target,0,item); blocks=copy
    }
    function selectCommand(at,index) {
        if(at<0 || index<0 || index>=blocks[at].messages.length) return
        let copy=snapshot(),cmd=copy[at]; cmd.value=cmd.messages[index].command
        for(let i=at+1;i<copy.length && copy[i].kind!=="command";i++) if(copy[i].kind==="payload") {
            const data=copy[i],values=data.variants[cmd.value] || data.fields
            fill(modelFor(data),values); data.fields=clone(values); data.variants[cmd.value]=clone(values)
        }
        blocks=copy; errorText=""
    }
    function renameCommand(at,name) {
        if(!name.trim()) { errorText="命令名称不能为空。"; return }
        let copy=snapshot(); copy[at].messages[commandIndex(copy[at])].name=name.trim(); blocks=copy; errorText=""
    }
    function addCommand(text,name) {
        const at=addingCommandAt
        if(at<0 || at>=blocks.length) return false
        const number=Number(text),width=blocks[at].bytes
        if(!text.trim() || !Number.isInteger(number) || number<0 || number>=Math.pow(2,width*8)) { errorText="命令值无效或超过所选字节数，可输入 0x 开头的十六进制。"; return false }
        let copy=snapshot(),cmd=copy[at],found=cmd.messages.findIndex(m=>Number(m.command)===number)
        if(found>=0) { selectCommand(at,found); return true }
        if(cmd.messages.length>=16) { errorText="每处最多设置 16 个命令。"; return false }
        const value="0x"+number.toString(16).toUpperCase()
        cmd.messages.push({command:value,name:name.trim() || "命令 "+value})
        blocks=copy; selectCommand(at,cmd.messages.length-1); return true
    }
    function removeCommand(at) {
        let copy=snapshot(),cmd=copy[at]
        if(cmd.messages.length<2) return
        const index=commandIndex(cmd),removed=cmd.messages[index].command
        cmd.messages.splice(index,1)
        blocks=copy; selectCommand(at,Math.max(0,index-1))
        copy=snapshot()
        for(let i=at+1;i<copy.length && copy[i].kind!=="command";i++) if(copy[i].kind==="payload") delete copy[i].variants[removed]
        blocks=copy
    }
    function canAdd(kind) { return ["header","length","tail"].indexOf(kind)<0 || !blocks.some(b=>b.kind===kind) }
    function remove(index) {
        let copy=snapshot(),removed=copy.splice(index,1)[0]; blocks=copy
        if(removed.kind==="payload") { fieldModels[removed._key].destroy(); delete fieldModels[removed._key] }
    }
    function add(kind) {
        if(!canAdd(kind)) { errorText="该模块已经存在。"; return }
        let copy=snapshot(),block={kind:kind,_key:"part"+(nextKey++)}
        // New body parts go before the trailing checksum/tail, preserving wire order.
        let at=copy.length
        while(at>0 && (copy[at-1].kind==="tail" || copy[at-1].kind==="check")) at--
        if(kind==="header") { block.hex="AA 55"; at=0 }
        if(kind==="length") { Object.assign(block,{bytes:2,endian:"little",meaning:"payload",includesTrailer:false}); at=1 }
        if(kind==="command") {
            const n=copy.filter(b=>b.kind==="command").length+1
            Object.assign(block,{name:"command"+n,bytes:2,endian:"little",value:"0x1",messages:[{command:"0x1",name:"命令 0x1"}]})
        }
        if(kind==="payload") {
            block.fields=[{name:"value",type:"float32",count:1,offset:0,variable:false,endian:"little"}]; block.variants={}
            const model=fieldsFactory.createObject(dialog); fill(model,block.fields); fieldModels[block._key]=model
            const cmd=owner(copy,at); if(cmd>=0) for(const m of copy[cmd].messages) block.variants[m.command]=clone(block.fields)
        }
        if(kind==="field") { Object.assign(block,{name:"sequence",type:"uint8",endian:"little"}); at=copy.findIndex(b=>b.kind==="payload"); if(at<0) at=copy.length }
        if(kind==="padding") block.bytes=1
        if(kind==="tail") { block.hex="0D 0A"; at=copy.length }
        if(kind==="check") Object.assign(block,{coverage:"before",rule:{name:"整帧校验",algorithm:"crc16modbus",enabled:true,offset:-2,start:0,end:-2,width:16}})
        copy.splice(at,0,block); blocks=copy; errorText=""
    }
    ColumnLayout {
        id: column; width: parent.width; spacing: 10
            Text { text: "只添加设备实际发送的部分，位置与总长度自动计算。数据字段直接在数据区模块中添加。"; color: Theme.secondary; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                ConfigCombo { id: addKind; objectName: "structureAddKind"; Layout.preferredWidth: 200; Layout.fillWidth: false; currentIndex: -1; displayText: currentIndex<0?"选择模块…":currentText; model: ["帧头","长度字段","命令字","公共字段（如序号）","数据区","校验","固定帧尾","保留字节"] }
                AppButton { objectName: "structureAddBlock"; text: "+ 添加模块"; implicitWidth: 110; enabled: addKind.currentIndex>=0 && dialog.blocks.length<24 && dialog.canAdd(dialog.kinds[addKind.currentIndex]); onClicked: dialog.add(dialog.kinds[addKind.currentIndex]) }
            }
            Repeater {
                model: dialog.blocks
                delegate: Rectangle {
                    id: blockCard
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true; implicitHeight: content.implicitHeight+20
                    radius: 7; color: Theme.surface; border.color: Theme.border
                    ColumnLayout {
                        id: content; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 10; y: 10; spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: (index+1)+". "+dialog.label(modelData.kind); font.bold: true; color: Theme.accentText; Layout.preferredWidth: 110 }
                            TextField { objectName: "structureHex"+index; visible: modelData.kind==="header" || modelData.kind==="tail"; Layout.fillWidth: true; text: modelData.hex || ""; placeholderText: "HEX，例如 AA 55"; onEditingFinished: dialog.change(blockCard.index,"hex",text) }
                            TextField { visible: modelData.kind==="field"; Layout.preferredWidth: 130; text: modelData.name || ""; placeholderText: "名称"; onEditingFinished: dialog.change(blockCard.index,"name",text) }
                            ConfigCombo {
                                objectName: modelData.kind==="command"?"structureCommandSelector":""; visible: modelData.kind==="command"; Layout.fillWidth: false; Layout.preferredWidth: 150
                                model: modelData.messages || []; textRole: "command"; currentIndex: dialog.commandIndex(modelData)
                                onActivated: function(choice) { dialog.selectCommand(blockCard.index,choice) }
                                
                            }
                            AppButton { objectName: modelData.kind==="command"?"addCommandButton":""; visible: modelData.kind==="command"; text: "添加命令"; implicitWidth: 80; enabled: (modelData.messages || []).length<16; onClicked: { dialog.addingCommandAt=blockCard.index; dialog.errorText=""; newCommandValue.text=""; newCommandName.text=""; newCommandDialog.open() } }
                            AppButton { visible: modelData.kind==="command"; text: "删除命令"; implicitWidth: 80; enabled: (modelData.messages || []).length>1; onClicked: dialog.removeCommand(blockCard.index) }
                            ConfigCombo { visible: modelData.kind==="field"; Layout.preferredWidth: 120; Layout.fillWidth: false; model: ["uint8","int8","uint16","int16","uint32","int32","float32","uint64","int64","float64"]; currentIndex: Math.max(0,model.indexOf(modelData.type)); onActivated: dialog.change(blockCard.index,"type",currentText) }
                            ConfigCombo { visible: modelData.kind==="length" || modelData.kind==="command"; Layout.preferredWidth: 100; Layout.fillWidth: false; model: ["1 字节","2 字节","4 字节"]; currentIndex: Math.max(0,[1,2,4].indexOf(modelData.bytes)); onActivated: dialog.change(blockCard.index,"bytes",[1,2,4][currentIndex]) }
                            SpinBox { visible: modelData.kind==="padding"; from: 1; to: 16; value: modelData.bytes || 1; onValueModified: dialog.change(blockCard.index,"bytes",value) }
                            Text { visible: modelData.kind==="payload"; text: "字段自动排列，变长数组由长度字段决定"; color: Theme.secondary; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            AppButton { objectName: "structureCheck"+index; visible: modelData.kind==="check"; implicitWidth: 230; text: modelData.rule?modelData.rule.name+" · 选择算法":"选择算法"; onClicked: { dialog.editingCheck=index; let r=dialog.clone(modelData.rule); r.coverageMode=modelData.coverage; checksumDialog.rule=r; checksumDialog.open() } }
                            Item { Layout.fillWidth: true }
                            AppButton { text: "↑"; implicitWidth: 36; enabled: index>1; onClicked: dialog.move(blockCard.index,-1) }
                            AppButton { text: "↓"; implicitWidth: 36; enabled: index>0 && index<dialog.blocks.length-1; onClicked: dialog.move(blockCard.index,1) }
                            AppButton { objectName: modelData.kind==="payload"?"removePayloadModule":"removeFrameBlock"+index; visible: modelData.kind!=="header"; text: "删除"; onClicked: dialog.remove(blockCard.index) }
                        }
                        ColumnLayout {
                            visible: modelData.kind==="payload"; Layout.fillWidth: true; spacing: 8
                            Text { visible: dialog.owner(dialog.blocks,index)>=0; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.accentText; font.pixelSize: 12; text: { const at=dialog.owner(dialog.blocks,index); return at>=0?"所属命令字 "+(at+1)+"："+dialog.commandName(dialog.blocks[at])+" · "+dialog.blocks[at].value:"公共数据区" } }
                            Loader {
                                Layout.fillWidth: true; active: modelData.kind==="payload" && !dialog.loadingConfiguration && dialog.modelFor(modelData)!==null
                                sourceComponent: Component {
                                    DataFieldsEditor {
                                        objectName: "inlineDataFields"; fieldModel: dialog.modelFor(modelData)
                                        variableAllowed: dialog.blocks.some(b=>b.kind==="length") && !dialog.blocks.slice(index+1).some(b=>b.kind==="payload" || b.kind==="command")
                                    }
                                }
                            }
                        }
                        RowLayout {
                            visible: modelData.kind==="command"; Layout.fillWidth: true
                            FieldLabel { text: "名称" }
                            TextField {
                                objectName: modelData.kind==="command"?"commandDisplayName":""; Layout.fillWidth: true
                                placeholderText: "例如：轮速反馈、IMU 数据"; maximumLength: 80; selectByMouse: true
                                text: dialog.commandName(modelData)
                                onEditingFinished: { if(modelData.kind==="command") dialog.renameCommand(blockCard.index,text) }
                            }
                        }
                        RowLayout {
                            visible: ["length","command","field"].indexOf(modelData.kind)>=0
                            FieldLabel { text: "字节序" }
                            ConfigCombo { Layout.preferredWidth: 100; Layout.fillWidth: false; model: ["小端","大端"]; currentIndex: modelData.endian==="big"?1:0; onActivated: dialog.change(blockCard.index,"endian",currentIndex===1?"big":"little") }
                            FieldLabel { visible: modelData.kind==="length"; text: "长度表示" }
                            ConfigCombo { visible: modelData.kind==="length"; Layout.preferredWidth: 150; Layout.fillWidth: false; model: ["数据区字节数","整帧字节数"]; currentIndex: modelData.meaning==="frame"?1:0; onActivated: dialog.change(blockCard.index,"meaning",currentIndex===1?"frame":"payload") }
                            CheckBox { visible: modelData.kind==="length"; text: "包含数据区后的全部字节"; checked: modelData.includesTrailer || false; onToggled: dialog.change(blockCard.index,"includesTrailer",checked) }
                            Text { visible: modelData.kind==="command"; text: "点击添加命令，下拉选择已有命令"; color: Theme.secondary; font.pixelSize: 11 }
                        }
                    }
                }
            }
            Text { text: "命令字和数据区可重复添加。数据区归属于前面最近的命令字；各处命令独立选择。变长数组只放在最后一个数据区末尾。"; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.secondary; font.pixelSize: 12 }
            RowLayout {
                FieldLabel { text: "最大帧长（字节）" }
                SpinBox { id: maximum; from: 1; to: 65536; editable: true }
            }
    }
    ChecksumRuleEditor {
        id: checksumDialog; objectName: "structureChecksumEditor"; protocol: dialog.protocol; popupLayer: dialog.popupLayer; structureMode: true; payloadOffset: dialog.configuration.payloadOffset || 0
        onSaved: function(value) { let copy=dialog.snapshot(); copy[dialog.editingCheck].rule=value; copy[dialog.editingCheck].coverage=value.coverageMode; dialog.blocks=copy }
    }
    Dialog {
        id: newCommandDialog; objectName: "addCommandDialog"
        parent: dialog.popupLayer || Overlay.overlay; modal: true; title: "添加命令"; width: 380
        anchors.centerIn: parent
        contentItem: ColumnLayout {
            TextField { id: newCommandValue; objectName: "newCommandValue"; Layout.fillWidth: true; placeholderText: "命令值，例如 0x0103"; selectByMouse: true; onAccepted: newCommandName.forceActiveFocus() }
            TextField { id: newCommandName; objectName: "newCommandName"; Layout.fillWidth: true; placeholderText: "名称（可选），例如：轮速反馈"; maximumLength: 80; selectByMouse: true; onAccepted: { if(dialog.addCommand(newCommandValue.text,text)) newCommandDialog.close() } }
            Text { text: dialog.errorText; visible: text.length>0; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.error }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            AppButton { text: "取消"; onClicked: newCommandDialog.close() }
            AppButton { objectName: "confirmAddCommand"; text: "添加"; primary: true; onClicked: { if(dialog.addCommand(newCommandValue.text,newCommandName.text)) newCommandDialog.close() } }
        }
    }
}
