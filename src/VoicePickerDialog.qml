import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SiftDialog {
    id: dialog
    title: qsTr("Voice")
    width: Math.min(560, parent ? parent.width - 32 : 560)
    height: Math.min(570, parent ? parent.height - 32 : 570)

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

            SiftField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: qsTr("Search voices by name, language, or gender")
                Accessible.name: qsTr("Search voices")
                font.pixelSize: 13
                selectByMouse: true
                onTextChanged: { dialog.refresh(); voiceList.currentIndex = 0 }
                Keys.onDownPressed: if (voiceList.count) { voiceList.currentIndex = 0; voiceList.forceActiveFocus() }
                onAccepted: if (dialog.filtered.length) { backend.setVoice(dialog.filtered[0].id); dialog.close() }
            }

            FlatButton {
                text: qsTr("Refresh")
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
            currentIndex: 0
            Keys.onDownPressed: {
                currentIndex = Math.min(count - 1, currentIndex + 1)
                positionViewAtIndex(currentIndex, ListView.Contain)
            }
            Keys.onUpPressed: {
                currentIndex = Math.max(0, currentIndex - 1)
                positionViewAtIndex(currentIndex, ListView.Contain)
            }
            Keys.onReturnPressed: if (currentIndex >= 0 && currentIndex < dialog.filtered.length) { backend.setVoice(dialog.filtered[currentIndex].id); dialog.close() }
            Keys.onEnterPressed: if (currentIndex >= 0 && currentIndex < dialog.filtered.length) { backend.setVoice(dialog.filtered[currentIndex].id); dialog.close() }
            ScrollBar.vertical: ScrollBar { }

            delegate: Rectangle {
                required property var modelData
                required property int index

                width: ListView.view.width
                height: 48
                radius: Math.min(5, theme.radius)
                color: backend.voice === modelData.id || (voiceList.activeFocus && voiceList.currentIndex === index) ? theme.panel : "transparent"

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
                        Layout.fillWidth: true
                        text: modelData.id
                        color: backend.voice === modelData.id ? theme.accent : theme.foreground
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }

                    Label {
                        Layout.preferredWidth: 120
                        elide: Text.ElideRight
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

    footer: RowLayout {
        Item { Layout.fillWidth: true }
        FlatButton { text: qsTr("Close"); onClicked: dialog.close() }
        Item { Layout.preferredWidth: 16 }
    }

    Connections {
        target: backend
        function onVoicesChanged() { dialog.refresh() }
    }
}
