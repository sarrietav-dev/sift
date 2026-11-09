import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SiftDialog {
    id: dialog
    title: qsTr("Add a page")
    standardButtons: Dialog.NoButton

    function submit() {
        const url = urlField.text.trim()
        if (url === "")
            return
        backend.addUrl(url)
        close()
    }

    onOpened: {
        urlField.text = ""
        urlField.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: 10

        Label {
            Layout.fillWidth: true
            text: qsTr("Paste an article address to add a clean copy to your queue.")
            color: theme.muted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        SiftField {
            id: urlField
            Layout.fillWidth: true
            placeholderText: "https://"
            Accessible.name: qsTr("Page address")
            font.pixelSize: 14
            selectByMouse: true
            onAccepted: dialog.submit()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 8

            Item { Layout.fillWidth: true }

            FlatButton {
                text: qsTr("Cancel")
                onClicked: dialog.close()
            }

            FlatButton {
                text: qsTr("Add")
                primary: true
                enabled: urlField.text.trim() !== ""
                onClicked: dialog.submit()
            }
        }
    }
}
