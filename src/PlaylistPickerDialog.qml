import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    title: qsTr("Add to playlist")
    modal: true
    width: 520
    height: 460
    closePolicy: Popup.CloseOnEscape
    standardButtons: Dialog.Close

    function createAndAdd() {
        const id = backend.createPlaylist(newName.text)
        if (id !== "") {
            backend.addCurrentToPlaylist(id)
            dialog.close()
        }
    }

    onOpened: {
        newName.text = ""
        newName.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: 10

        Label {
            Layout.fillWidth: true
            text: qsTr("Choose a playlist for “%1”.").arg(backend.currentArticle.title || qsTr("this page"))
            color: theme.muted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        ListView {
            id: playlistList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: backend.playlists
            ScrollBar.vertical: ScrollBar { }

            delegate: Rectangle {
                required property var modelData

                width: ListView.view.width
                height: 48
                radius: Math.min(5, theme.radius)
                color: "transparent"

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        backend.addCurrentToPlaylist(modelData.id)
                        dialog.close()
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        text: modelData.name
                        color: theme.foreground
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }

                    Label {
                        text: modelData.count
                        color: theme.muted
                        font.pixelSize: 11
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: playlistList.count === 0
                text: qsTr("No playlists yet. Create one below.")
                color: theme.muted
                font.pixelSize: 12
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            TextField {
                id: newName
                Layout.fillWidth: true
                placeholderText: qsTr("New playlist")
                font.pixelSize: 12
                selectByMouse: true
                onAccepted: dialog.createAndAdd()
            }

            Button {
                text: qsTr("Create and add")
                highlighted: true
                enabled: newName.text.trim() !== ""
                onClicked: dialog.createAndAdd()
            }
        }
    }
}
