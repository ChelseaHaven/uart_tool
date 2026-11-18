import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: page
    required property QtObject preferences
    radius: 11; color: Theme.surface; border.color: Theme.border
    ScrollView {
        id: scroll
        anchors.fill: parent; anchors.margins: 28; clip: true
        contentWidth: availableWidth
        contentHeight: column.implicitHeight
        ColumnLayout {
            id: column
            width: scroll.availableWidth; spacing: 16
            Text { text: "设置"; font.pixelSize: 24; font.bold: true; color: Theme.heading }
            Text { text: "按你的使用习惯调整工具，修改会自动保存。"; color: Theme.muted; font.pixelSize: 12 }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider; Layout.topMargin: 8; Layout.bottomMargin: 6 }
            Text { text: "外观"; font.pixelSize: 16; font.bold: true; color: Theme.accentText }
            RowLayout {
                Layout.fillWidth: true; spacing: 24
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 8
                    Text { text: "界面模式"; color: Theme.text; font.pixelSize: 14 }
                    Text { text: "立即切换亮色或暗色，重启后保留选择。"; color: Theme.muted; font.pixelSize: 12 }
                }
                ConfigCombo {
                    objectName: "themeModeSelector"; Layout.fillWidth: false; Layout.preferredWidth: 160
                    model: ["亮色模式","暗色模式"]
                    currentIndex: page.preferences.darkMode?1:0
                    onActivated: page.preferences.darkMode=currentIndex===1
                    Accessible.name: "界面模式"
                }
            }
        }
    }
}
