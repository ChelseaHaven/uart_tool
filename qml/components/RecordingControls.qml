import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

RowLayout {
    id: controls
    required property QtObject recorder
    visible: recorder!==null
    spacing: 8

    function duration(ms) {
        const total=Math.floor(ms/1000)
        const hours=Math.floor(total/3600)
        const minutes=String(Math.floor(total/60)%60).padStart(2,"0")
        const seconds=String(total%60).padStart(2,"0")
        return hours>0 ? hours+":"+minutes+":"+seconds : minutes+":"+seconds
    }

    ColumnLayout {
        Layout.preferredWidth: 250
        visible: statusLine.text.length>0
        spacing: 2
        Text {
            id: statusLine
            objectName: "recordingStatus"
            Layout.fillWidth: true
            elide: Text.ElideRight
            font.pixelSize: 12
            color: controls.recorder && controls.recorder.failed ? Theme.error : Theme.secondary
            text: {
                const recorder=controls.recorder
                if(!recorder || !recorder.status) return ""
                if(recorder.recording || recorder.stopping)
                    return recorder.status+" · "+controls.duration(recorder.elapsedMs)+" · "+recorder.writtenFrames+" 帧"
                return recorder.status+(recorder.directory ? " · "+recorder.writtenFrames+" 帧" : "")
            }
            HoverHandler { id: statusHover }
            ToolTip.visible: statusHover.hovered && truncated
            ToolTip.text: text
        }
        Text {
            objectName: "recordingDirectory"
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            font.pixelSize: 11
            color: Theme.muted
            visible: text.length>0
            text: controls.recorder ? controls.recorder.directory : ""
            HoverHandler { id: directoryHover }
            ToolTip.visible: directoryHover.hovered
            ToolTip.text: text
        }
    }

    TitleActionButton {
        id: recordButton
        icon.source: recording ? "../icons/stop-recording.svg" : "../icons/record.svg"
        alertActive: recording
        objectName: "recordingButton"
        readonly property bool recording: controls.recorder!==null && controls.recorder.recording
        text: recording ? "停止录制" : "开始录制"
        enabled: controls.recorder!==null && (recording || controls.recorder.canStart)
        onClicked: {
            if(recording) controls.recorder.stop()
            else directoryPicker.open()
        }
    }

    FolderDialog {
        id: directoryPicker
        objectName: "recordingDirectoryPicker"
        title: "选择录制保存目录"
        onAccepted: {
            if(controls.recorder) controls.recorder.start(selectedFolder)
        }
    }
}
