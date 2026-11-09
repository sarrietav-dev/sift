import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    title: qsTr("Voice")
    modal: true
    width: 760
    height: 600
    closePolicy: Popup.CloseOnEscape
    standardButtons: Dialog.Close

    property var filtered: []

    function refresh() {
        const query = searchField.text.trim().toLowerCase()
        const result = []
        for (let i = 0; i < backend.voices.length; i++) {
            const voice = backend.voices[i]
            if (query === "" || String(voice.search).toLowerCase().indexOf(query) >= 0)
                result.push(voice)
        }
        filtered = result
    }

    onOpened: {
        searchField.text = ""
        refresh()
        searchField.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: qsTr("Search voices by name, language, or gender")
                font.pixelSize: 13
                selectByMouse: true
                onTextChanged: dialog.refresh()
            }

            Button {
                text: qsTr("Refresh")
                flat: true
                font.pixelSize: 11
                onClicked: backend.refreshVoices()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Rectangle {
                width: 7
                height: 7
                radius: 4
                color: backend.kokoroHealthy ? theme.green : theme.red
            }

            Label {
                Layout.fillWidth: true
                text: backend.kokoroStatus
                color: theme.muted
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }

        ListView {
            id: voiceList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: dialog.filtered
            ScrollBar.vertical: ScrollBar { }

            delegate: Rectangle {
                required property var modelData

                width: ListView.view.width
                height: 48
                radius: Math.min(5, theme.radius)
                color: backend.voice === modelData.id ? theme.panel : "transparent"

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        backend.setVoice(modelData.id)
                        dialog.close()
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 10

                    Label {
                        Layout.preferredWidth: 230
                        text: modelData.id
                        color: backend.voice === modelData.id ? theme.accent : theme.foreground
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.fillWidth: true
                        text: modelData.language
                        color: theme.muted
                        font.pixelSize: 11
                    }

                    Label {
                        text: modelData.gender
                        color: theme.muted
                        font.pixelSize: 11
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: voiceList.count === 0
                text: qsTr("No voices match.")
                color: theme.muted
                font.pixelSize: 12
            }
        }
    }

    Connections {
        target: backend
        function onVoicesChanged() { dialog.refresh() }
    }
}
