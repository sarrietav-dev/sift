import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: player
    signal openVoice()

    color: theme.panel
    Layout.preferredHeight: 64

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 8

        ToolButton {
            text: "‹"
            font.pixelSize: 20
            enabled: backend.sentenceCount > 0
            onClicked: backend.previousSentence()
        }

        ToolButton {
            text: backend.buffering ? "…" : backend.playing ? "⏸" : "▶"
            font.pixelSize: 20
            enabled: backend.sentenceCount > 0
            onClicked: backend.playPause()
        }

        ToolButton {
            text: "›"
            font.pixelSize: 20
            enabled: backend.sentenceCount > 0
            onClicked: backend.nextSentence()
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            Label {
                Layout.fillWidth: true
                text: backend.currentArticle.title || qsTr("Nothing playing")
                color: theme.foreground
                font.pixelSize: 13
                elide: Text.ElideRight
            }

            Label {
                Layout.fillWidth: true
                text: backend.buffering ? qsTr("Buffering…") : ((backend.ttsStatus && backend.ttsStatus !== "") ? backend.ttsStatus : backend.status)
                color: backend.error !== "" ? theme.red : theme.muted
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }

        Label {
            text: backend.sentenceCount > 0
                ? (backend.currentSentence + 1) + " / " + backend.sentenceCount
                : ""
            color: theme.muted
            font.pixelSize: 10
        }

        Rectangle {
            width: 130
            height: 4
            radius: 2
            color: theme.border

            Rectangle {
                width: parent.width * (backend.sentenceCount > 0
                    ? (backend.currentSentence + 1) / backend.sentenceCount : 0)
                height: parent.height
                radius: 2
                color: theme.accent
            }
        }

        ToolButton {
            text: "−"
            font.pixelSize: 14
            onClicked: backend.setSpeed(Math.max(0.5, backend.speed - 0.1))
        }

        Label {
            text: backend.speed.toFixed(1) + "×"
            color: theme.foreground
            font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter
            Layout.preferredWidth: 34
        }

        ToolButton {
            text: "+"
            font.pixelSize: 14
            onClicked: backend.setSpeed(Math.min(2.0, backend.speed + 0.1))
        }

        ToolButton {
            text: backend.voice
            font.pixelSize: 10
            onClicked: player.openVoice()
        }
    }
}
