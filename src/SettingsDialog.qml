import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    title: qsTr("Settings")
    modal: true
    width: 600
    closePolicy: Popup.CloseOnEscape
    standardButtons: Dialog.Save | Dialog.Cancel

    property var current: ({})

    function voiceIndex(id) {
        for (let i = 0; i < backend.voices.length; i++) {
            if (backend.voices[i].id === id)
                return i
        }
        return 0
    }

    function load() {
        current = backend.settings()
        endpointField.text = current.kokoroUrl || ""
        saveDirField.text = current.saveDir || ""
        voiceBox.currentIndex = voiceIndex(current.voice || "af_heart")
        speedSlider.value = current.speed || 1.0
        fontSlider.value = current.fontSize || 19
        cacheSpin.value = current.cacheLimitMb || 1024
        autoAdvance.checked = current.autoAdvance !== false
    }

    function save() {
        const voice = voiceBox.currentIndex >= 0 && voiceBox.currentIndex < backend.voices.length
            ? backend.voices[voiceBox.currentIndex].id : (current.voice || "af_heart")
        backend.applySettings({
            kokoroUrl: endpointField.text,
            saveDir: saveDirField.text,
            voice: voice,
            speed: speedSlider.value,
            fontSize: fontSlider.value,
            cacheLimitMb: cacheSpin.value,
            autoAdvance: autoAdvance.checked
        })
    }

    onOpened: load()
    onAccepted: save()

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            Layout.fillWidth: true
            text: qsTr("Kokoro endpoint")
            color: theme.muted
            font.pixelSize: 11
        }

        TextField {
            id: endpointField
            Layout.fillWidth: true
            placeholderText: "http://127.0.0.1:8880"
            font.pixelSize: 13
            selectByMouse: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: backend.kokoroStatus
                color: backend.kokoroHealthy ? theme.muted : theme.red
                font.pixelSize: 10
                elide: Text.ElideRight
            }

            Button {
                text: qsTr("Check")
                flat: true
                font.pixelSize: 10
                onClicked: backend.checkKokoro()
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 4
            text: qsTr("Markdown folder")
            color: theme.muted
            font.pixelSize: 11
        }

        TextField {
            id: saveDirField
            Layout.fillWidth: true
            placeholderText: "~/Documents/sift"
            font.pixelSize: 13
            selectByMouse: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Label {
                    text: qsTr("Voice")
                    color: theme.muted
                    font.pixelSize: 11
                }

                ComboBox {
                    id: voiceBox
                    Layout.fillWidth: true
                    model: backend.voices
                    textRole: "id"
                    font.pixelSize: 12
                }
            }

            ColumnLayout {
                Layout.preferredWidth: 180
                spacing: 2

                Label {
                    text: qsTr("Speed")
                    color: theme.muted
                    font.pixelSize: 11
                }

                RowLayout {
                    Layout.fillWidth: true

                    Slider {
                        id: speedSlider
                        Layout.fillWidth: true
                        from: 0.5
                        to: 2.0
                        stepSize: 0.1
                    }

                    Label {
                        text: speedSlider.value.toFixed(1) + "×"
                        color: theme.foreground
                        font.pixelSize: 11
                        Layout.preferredWidth: 34
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Label {
                    text: qsTr("Reading font size")
                    color: theme.muted
                    font.pixelSize: 11
                }

                RowLayout {
                    Layout.fillWidth: true

                    Slider {
                        id: fontSlider
                        Layout.fillWidth: true
                        from: 14
                        to: 32
                        stepSize: 1
                    }

                    Label {
                        text: Math.round(fontSlider.value)
                        color: theme.foreground
                        font.pixelSize: 11
                        Layout.preferredWidth: 26
                    }
                }
            }

            ColumnLayout {
                Layout.preferredWidth: 180
                spacing: 2

                Label {
                    text: qsTr("Audio cache (MB)")
                    color: theme.muted
                    font.pixelSize: 11
                }

                SpinBox {
                    id: cacheSpin
                    Layout.fillWidth: true
                    from: 64
                    to: 8192
                    stepSize: 256
                }
            }
        }

        CheckBox {
            id: autoAdvance
            text: qsTr("Continue to the next page in the queue")
            font.pixelSize: 12
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 2
            text: qsTr("Changes take effect after saving. Voice and speed changes regenerate audio from the current sentence.")
            color: theme.muted
            font.pixelSize: 10
            wrapMode: Text.WordWrap
        }
    }
}
