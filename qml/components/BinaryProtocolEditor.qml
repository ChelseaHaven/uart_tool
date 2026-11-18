import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as Files

Dialog {
    id: editor
    property Item popupLayer: null
    required property QtObject protocol
    readonly property QtObject store: protocol.templateStore
    parent: editor.popupLayer || Overlay.overlay; modal: true; title: "编辑协议"
    width: parent?Math.min(1040,parent.width-40):960
    height: parent?Math.min(780,parent.height-40):720
    x: parent?(parent.width-width)/2:0; y: parent?(parent.height-height)/2:0
    closePolicy: Popup.CloseOnEscape; padding: 20
    property var draft: ({})
    property string selectedTemplateId: ""
    property string localError: ""
    property string notice: ""
    property string savedSignature: ""
    property bool newDraft: false
    readonly property bool hasEditingTarget: newDraft || presetPicker.currentIndex>=0
    component Input: TextField {
        implicitHeight: 36; color: Theme.text; selectByMouse: true
        background: Rectangle { radius: 5; color: Theme.surface; border.color: Theme.border }
    }
    function loadConfiguration(c) { draft=c; localError=""; structureForm.loadConfiguration(c) }
    function build() {
        let c=structureForm.build(); c.templateId=selectedTemplateId || "custom"; return c
    }
    function stable(value) {
        if(Array.isArray(value)) return value.map(stable)
        if(value!==null && typeof value==="object") {
            let result={}; for(const key of Object.keys(value).sort()) result[key]=stable(value[key]); return result
        }
        return value
    }
    function signature() { return JSON.stringify(stable({name:templateName.text.trim(),structure:structureForm.draftState()})) }
    function markSaved() { savedSignature=signature() }
    function applyNow() {
        localError=""
        try { if(protocol.applyBinaryConfiguration(build())) { close(); return true } }
        catch(e) { localError=String(e) }
        return false
    }
    function requestApply() {
        if(!hasEditingTarget) return
        localError=""
        try {
            build() // Validate before offering to save or apply.
            if(signature()!==savedSignature) { unsavedName.text=templateName.text; unsavedDialog.open() }
            else applyNow()
        } catch(e) { localError=String(e) }
    }
    function templateItems() { return templateCategory.currentIndex===0?store.builtInEntries:store.entries }
    function templateIndex(id) { const items=templateItems(); for(let i=0;i<items.length;i++) if(items[i].id===id) return i; return -1 }
    function newTemplate() {
        newDraft=true
        loadConfiguration(protocol.newBinaryConfiguration())
        selectedTemplateId=""; templateName.text=""; templateCategory.currentIndex=1; presetPicker.currentIndex=-1
        notice="请输入模板名称，配置后点击保存模板。"
        scroll.contentItem.contentY=0
        markSaved(); templateName.forceActiveFocus()
    }
    function useTemplate(id) {
        const record=store.loadTemplate(id)
        if(!record.configuration) { localError=store.lastError; return }
        loadConfiguration(record.configuration); selectedTemplateId=id; templateName.text=record.name
        templateCategory.currentIndex=id.startsWith("builtin:")?0:1
        presetPicker.currentIndex=templateIndex(id); newDraft=false; notice="模板已载入草稿，应用后生效。"; markSaved()
    }
    function saveTemplate(asNew,name) {
        localError=""
        try {
            const id=store.saveTemplate(name,build(),asNew || selectedTemplateId.startsWith("builtin:")?"":selectedTemplateId)
            if(!id) { localError=store.lastError; return false }
            selectedTemplateId=id; templateCategory.currentIndex=1; templateName.text=name.trim(); notice="模板已保存为 JSON 文件。"
            markSaved()
            Qt.callLater(function(){presetPicker.currentIndex=editor.templateIndex(id); editor.newDraft=false})
            return true
        } catch(e) { localError=String(e); return false }
    }
    onOpened: {
        newDraft=false
        store.refresh(); loadConfiguration(protocol.binaryConfiguration)
        selectedTemplateId=""; templateName.text=""; notice=""
        templateCategory.currentIndex=1
        for(const entry of store.entries) if(entry.id===draft.templateId) { selectedTemplateId=entry.id; templateName.text=entry.name }
        if(!selectedTemplateId) for(const entry of store.builtInEntries) if(entry.id===draft.templateId || entry.id==="builtin:"+draft.templateId) {
            selectedTemplateId=entry.id; templateName.text=entry.name; templateCategory.currentIndex=0
        }
        presetPicker.currentIndex=templateIndex(selectedTemplateId)
        if(selectedTemplateId) {
            const record=store.loadTemplate(selectedTemplateId)
            if(record.configuration) {
                structureForm.loadConfiguration(record.configuration); markSaved()
                structureForm.loadConfiguration(draft)
            } else markSaved()
        } else markSaved()
    }
    background: Rectangle { color: Theme.panel; radius: 12; border.color: Theme.border }
    contentItem: ScrollView {
        id: scroll; clip: true; contentWidth: availableWidth; contentHeight: form.implicitHeight
        ColumnLayout {
            id: form; width: scroll.availableWidth; spacing: 12
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                ConfigCombo {
                    id: templateCategory; objectName: "binaryTemplateCategory"; model: ["内置模板","我的模板"]
                    Layout.fillWidth: false; Layout.preferredWidth: 120
                    onActivated: { editor.newDraft=false; presetPicker.currentIndex=-1 }
                }
                ConfigCombo {
                    id: presetPicker; objectName: "binaryTemplatePicker"; model: editor.templateItems(); textRole: "name"
                    Layout.fillWidth: true
                    displayText: currentIndex<0?"选择模板":currentText
                    onActivated: { if(currentIndex>=0) editor.useTemplate(model[currentIndex].id) }
                }
                AppButton { objectName: "newBinaryTemplate"; text: "新建"; onClicked: editor.newTemplate() }
                AppButton { objectName: "importBinaryTemplate"; text: "导入"; onClicked: importDialog.open() }
                AppButton { objectName: "exportBinaryTemplate"; text: "导出"; enabled: editor.hasEditingTarget && templateName.text.trim().length>0; onClicked: exportDialog.open() }
                AppButton { objectName: "deleteBinaryTemplate"; text: "删除"; enabled: editor.hasEditingTarget && editor.selectedTemplateId.length>0 && !editor.selectedTemplateId.startsWith("builtin:"); onClicked: { deleteDialog.targetId=editor.selectedTemplateId; deleteDialog.targetName=templateName.text; deleteDialog.open() } }
            }
            ColumnLayout {
                objectName: "binaryTemplateDetails"
                Layout.fillWidth: true; spacing: 12; visible: editor.hasEditingTarget
                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    Input { id: templateName; objectName: "binaryTemplateName"; Layout.fillWidth: true; placeholderText: "模板名称"; maximumLength: 80 }
                    AppButton { objectName: "saveBinaryTemplate"; text: editor.selectedTemplateId.startsWith("builtin:")?"保存到我的模板":"保存模板"; enabled: templateName.text.trim().length>0; onClicked: editor.saveTemplate(false,templateName.text) }
                    AppButton { objectName: "saveBinaryTemplateAs"; text: "另存为"; onClicked: { editor.localError=""; copyName.text=templateName.text?templateName.text.slice(0,76)+" 副本":"新协议"; copyDialog.open() } }
                    AppButton { text: "刷新"; onClicked: { editor.store.refresh(); presetPicker.currentIndex=editor.templateIndex(editor.selectedTemplateId); if(presetPicker.currentIndex<0) editor.selectedTemplateId="" } }
                }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: editor.notice; visible: text.length>0; color: Theme.accentText; font.pixelSize: 12 }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
                FrameStructureEditor { id: structureForm; protocol: editor.protocol; popupLayer: editor.popupLayer; Layout.fillWidth: true }
            }
        }
    }
    Files.FileDialog {
        id: importDialog; parentWindow: editor.popupLayer?editor.popupLayer.Window.window:null; objectName: "importBinaryTemplateDialog"; title: "导入协议模板"; nameFilters: ["协议模板 (*.json)"]; fileMode: Files.FileDialog.OpenFile
        onAccepted: { const id=editor.store.importTemplate(selectedFile); if(id) editor.useTemplate(id); else editor.localError=editor.store.lastError }
    }
    Files.FileDialog {
        id: exportDialog; parentWindow: editor.popupLayer?editor.popupLayer.Window.window:null; objectName: "exportBinaryTemplateDialog"; title: "导出当前模板"; nameFilters: ["协议模板 (*.json)"]; fileMode: Files.FileDialog.SaveFile; defaultSuffix: "json"
        onAccepted: { try { if(editor.store.exportConfiguration(templateName.text,editor.build(),selectedFile)) editor.notice="已导出当前草稿。"; else editor.localError=editor.store.lastError } catch(e) { editor.localError=String(e) } }
    }
    Dialog {
        id: copyDialog; objectName: "copyBinaryTemplateDialog"; parent: editor.popupLayer || Overlay.overlay; modal: true; title: "另存为新模板"; anchors.centerIn: parent; width: 400
        contentItem: ColumnLayout {
            Input { id: copyName; objectName: "copyBinaryTemplateName"; maximumLength: 80; Layout.fillWidth: true }
            Text { text: editor.localError; visible: text.length>0; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.error }
        }
        footer: RowLayout { Item { Layout.fillWidth: true } AppButton { text: "取消"; onClicked: copyDialog.close() } AppButton { objectName: "confirmCopyBinaryTemplate"; text: "保存"; onClicked: { if(editor.saveTemplate(true,copyName.text)) copyDialog.close() } } }
    }
    Dialog {
        id: deleteDialog; objectName: "deleteBinaryTemplateDialog"; parent: editor.popupLayer || Overlay.overlay; modal: true; title: "删除模板"; anchors.centerIn: parent; width: 420
        property string targetId: ""
        property string targetName: ""
        contentItem: Text { text: "删除“"+deleteDialog.targetName+"”的模板文件？当前接收与草稿保留。"; wrapMode: Text.WordWrap; color: Theme.text }
        footer: RowLayout { Item { Layout.fillWidth: true } AppButton { text: "取消"; onClicked: deleteDialog.close() } AppButton { objectName: "confirmDeleteBinaryTemplate"; text: "删除"; onClicked: {
            if(editor.store.deleteTemplate(deleteDialog.targetId)) { editor.selectedTemplateId=""; templateName.text=""; editor.newDraft=false; presetPicker.currentIndex=-1; editor.notice="模板文件已删除，草稿仍保留。"; deleteDialog.close() }
            else editor.localError=editor.store.lastError
        } } }
    }
    Dialog {
        id: unsavedDialog; objectName: "unsavedBinaryTemplateDialog"
        parent: editor.popupLayer || Overlay.overlay; modal: true; title: "模板有未保存的修改"; width: 540
        anchors.centerIn: parent; closePolicy: Popup.CloseOnEscape
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: "模板有未保存的修改，请保存后应用，或返回继续编辑。"; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text }
            Input { id: unsavedName; objectName: "unsavedBinaryTemplateName"; Layout.fillWidth: true; placeholderText: "模板名称"; maximumLength: 80 }
            Text { text: editor.localError || editor.store.lastError || editor.protocol.configurationError; visible: text.length>0; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.error }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            AppButton { objectName: "returnToBinaryEditor"; text: "返回编辑"; onClicked: unsavedDialog.close() }
            AppButton { objectName: "saveTemplateAndApply"; text: "保存并应用"; implicitWidth: 110; primary: true; onClicked: {
                templateName.text=unsavedName.text
                if(editor.saveTemplate(false,unsavedName.text) && editor.applyNow()) unsavedDialog.close()
            } }
        }
    }
    footer: ColumnLayout {
        Text { Layout.fillWidth: true; Layout.margins: 10; wrapMode: Text.WordWrap; color: Theme.error; text: editor.localError || structureForm.errorText || editor.store.lastError || editor.protocol.configurationError; visible: text.length>0 }
        RowLayout { Layout.fillWidth: true; Layout.margins: 12
            Text { visible: editor.hasEditingTarget; text: "应用只影响后续解析；模板用上方按钮保存。"; color: Theme.secondary; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            AppButton { text: "取消"; onClicked: editor.close() }
            AppButton { objectName: "saveBinaryProtocol"; text: "应用并关闭"; enabled: editor.hasEditingTarget; primary: true; onClicked: editor.requestApply() }
        }
    }
}
