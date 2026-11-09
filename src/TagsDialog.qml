import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    title: qsTr("Tags")
    modal: true
    width: 480
    closePolicy: Popup.CloseOnEscape
    standardButtons: Dialog.Save | Dialog.Cancel

    onOpened: {
        const tags = backend.currentArticle.tags || []
        tagField.text = tags.join(", ")
        tagField.forceActiveFocus()
    }

    onAccepted: backend.setCurrentTags(tagField.text)

    contentItem: ColumnLayout {
        spacing: 10

        Label {
            Layout.fillWidth: true
            text: qsTr("Separate tags with commas. Tags are also written into the Markdown frontmatter.")
            color: theme.muted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        TextField {
            id: tagField
            Layout.fillWidth: true
            placeholderText: qsTr("research, systems, later")
            font.pixelSize: 14
            selectByMouse: true
            onAccepted: dialog.accept()
        }

        Label {
            Layout.fillWidth: true
            text: backend.currentArticle.title || ""
            color: theme.muted
            font.pixelSize: 10
            elide: Text.ElideRight
        }
    }
}
