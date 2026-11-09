import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: overlay
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0.62)
    visible: false
    focus: visible

    signal close()

    Keys.onEscapePressed: overlay.close()
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Question
            || (event.key === Qt.Key_Slash && (event.modifiers & Qt.ShiftModifier) !== 0)) {
            overlay.close()
            event.accepted = true
        }
    }
    onVisibleChanged: {
        if (visible)
            overlay.forceActiveFocus()
    }

    MouseArea {
        anchors.fill: parent
        onClicked: overlay.close()
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 620)
        height: content.implicitHeight + 48
        radius: Math.min(8, theme.radius)
        color: theme.panel
        border.width: 1
        border.color: theme.border

        MouseArea {
            anchors.fill: parent
            onClicked: {} // keep clicks inside the panel from closing it
        }

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 24
            spacing: 9

            Label {
                text: qsTr("Keyboard shortcuts")
                color: theme.accent
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }

            Repeater {
                model: [
                    { key: "Space", action: qsTr("Play or pause") },
                    { key: "[ / ]", action: qsTr("Previous or next sentence") },
                    { key: "− / +", action: qsTr("Slower or faster") },
                    { key: "V", action: qsTr("Choose a voice") },
                    { key: "A", action: qsTr("Add a page") },
                    { key: "J / K", action: qsTr("Move in the queue, or scroll the article") },
                    { key: "Shift+J / K", action: qsTr("Move the selected page down or up") },
                    { key: "Del", action: qsTr("Remove the selected page from the queue") },
                    { key: "Enter", action: qsTr("Open the selected page") },
                    { key: "E", action: qsTr("Copy the Markdown export") },
                    { key: "F / Esc", action: qsTr("Show or hide the queue") },
                    { key: "Ctrl+,", action: qsTr("Settings") },
                    { key: "PageUp / PageDown", action: qsTr("Scroll the article") },
                    { key: "Q", action: qsTr("Quit") }
                ]

                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 12

                    Label {
                        Layout.preferredWidth: 150
                        text: modelData.key
                        color: theme.accent
                        font.pixelSize: 12
                        font.family: theme.monoFont
                    }

                    Label {
                        Layout.fillWidth: true
                        text: modelData.action
                        color: theme.foreground
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.topMargin: 8
                text: qsTr("Press ? or Esc to close.")
                color: theme.muted
                font.pixelSize: 10
            }
        }
    }
}
