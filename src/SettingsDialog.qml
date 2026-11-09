import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SiftDialog {
    id: dialog
    title: qsTr("Settings")
    width: Math.min(520, parent ? parent.width - 32 : 520)
    height: Math.min(700, parent ? parent.height - 32 : 700)
    property var current: ({})

    function fontIndex(family) {
        for (let i = 0; i < backend.fonts.length; i++) {
            if (backend.fonts[i] === family)
                return i
        }
        return 0
    }
    function load() {
        current = backend.settings()
        endpointField.text = current.kokoroUrl || ""
        saveDirField.text = current.saveDir || ""
        speedSlider.value = current.speed || 1.0
        fontSlider.value = current.fontSize || 19
        fontBox.currentIndex = fontIndex(current.readerFont || backend.readerFont)
        cacheSpin.value = current.cacheLimitMb || 1024
        autoAdvance.checked = current.autoAdvance !== false
    }
    function save() {
        backend.applySettings({
            kokoroUrl: endpointField.text,
            saveDir: saveDirField.text,
            voice: current.voice || backend.voice,
            speed: speedSlider.value,
            fontSize: Math.round(fontSlider.value),
            readerFont: fontBox.currentText,
            cacheLimitMb: cacheSpin.value,
            autoAdvance: autoAdvance.checked
        })
        close()
    }
    onOpened: { load(); endpointField.forceActiveFocus() }

    contentItem: ScrollView {
        id: scroll
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 9
            Label { text: qsTr("Appearance"); color: theme.foreground; font.pixelSize: 13 }
            // Applies at once, like the desktop's own theme picker.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 40
                color: theme.panel
                border.color: theme.border
                radius: Math.min(5, theme.radius)
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 3
                    spacing: 3
                    Repeater {
                        model: [
                            { value: "omarchy", label: qsTr("Omarchy") },
                            { value: "system", label: qsTr("System") },
                            { value: "light", label: qsTr("Light") },
                            { value: "dark", label: qsTr("Dark") }
                        ]
                        FlatButton {
                            required property var modelData
                            readonly property bool chosen: theme.appearance === modelData.value
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            text: modelData.label
                            hint: qsTr("Appearance: %1").arg(modelData.label)
                            font.pixelSize: 13
                            primary: chosen
                            onClicked: backend.setAppearance(modelData.value)
                        }
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                text: theme.appearance === "omarchy" ? qsTr("Follows the Omarchy theme as it changes.")
                    : theme.appearance === "system" ? qsTr("Light or dark, as the system prefers.")
                    : qsTr("Stays the same whatever the desktop theme.")
                color: theme.muted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Item { Layout.preferredHeight: 4 }
            Label { text: qsTr("Kokoro endpoint"); color: theme.foreground; font.pixelSize: 13 }
            SiftField { id: endpointField; Layout.fillWidth: true; placeholderText: "http://127.0.0.1:8880"; Accessible.name: qsTr("Kokoro endpoint") }
            Label { Layout.fillWidth: true; text: qsTr("The server must already be running. Save changes before testing the connection."); color: theme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
            Item { Layout.preferredHeight: 4 }
            Label { text: qsTr("Markdown folder"); color: theme.foreground; font.pixelSize: 13 }
            SiftField { id: saveDirField; Layout.fillWidth: true; placeholderText: "~/Documents/sift"; Accessible.name: qsTr("Markdown folder") }
            Item { Layout.preferredHeight: 4 }
            Label { text: qsTr("Reading font"); color: theme.foreground; font.pixelSize: 13 }
            SiftComboBox {
                id: fontBox
                objectName: "fontBox"
                Layout.fillWidth: true
                model: backend.fonts
                previewFonts: true
                hint: qsTr("Reading font")
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Aa — The quiet art of reading")
                color: theme.muted
                font.family: fontBox.currentText
                font.pixelSize: 17
                elide: Text.ElideRight
            }
            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; text: qsTr("Reading text size"); color: theme.foreground; font.pixelSize: 13 }
                Label { text: Math.round(fontSlider.value) + " px"; color: theme.muted; font.pixelSize: 12 }
            }
            Slider { id: fontSlider; Layout.fillWidth: true; from: 14; to: 32; stepSize: 1; Accessible.name: qsTr("Reading text size") }
            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; text: qsTr("Playback speed"); color: theme.foreground; font.pixelSize: 13 }
                Label { text: speedSlider.value.toFixed(1) + "×"; color: theme.muted; font.pixelSize: 12 }
            }
            Slider { id: speedSlider; Layout.fillWidth: true; from: 0.5; to: 2.0; stepSize: 0.1; Accessible.name: qsTr("Playback speed") }
            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; text: qsTr("Audio cache (MB)"); color: theme.foreground; font.pixelSize: 13 }
                SpinBox { id: cacheSpin; from: 64; to: 8192; stepSize: 256; Accessible.name: qsTr("Audio cache (MB)") }
            }
            CheckBox { id: autoAdvance; text: qsTr("Continue to the next page in the queue"); font.pixelSize: 13 }
        }
    }
    footer: RowLayout {
        spacing: 8
        Item { Layout.fillWidth: true }
        FlatButton { text: qsTr("Cancel"); onClicked: dialog.close() }
        FlatButton { text: qsTr("Save"); primary: true; onClicked: dialog.save() }
        Item { Layout.preferredWidth: 16 }
    }
}
