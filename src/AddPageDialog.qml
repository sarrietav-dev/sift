import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    title: qsTr("Add a page")
    modal: true
    width: 560
    closePolicy: Popup.CloseOnEscape
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

        TextField {
            id: urlField
            Layout.fillWidth: true
            placeholderText: "https://"
            font.pixelSize: 14
            selectByMouse: true
            onAccepted: dialog.submit()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 8

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Cancel")
                flat: true
                onClicked: dialog.close()
            }

            Button {
                text: qsTr("Add")
                highlighted: true
                enabled: urlField.text.trim() !== ""
                onClicked: dialog.submit()
            }
        }
    }
}
