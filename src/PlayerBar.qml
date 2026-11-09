import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: player
    signal openVoice()
    readonly property bool popupOpen: speedMenu.visible
    color: theme.background
    Layout.preferredHeight: 54

    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: theme.border }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 8

        FlatButton {
            iconPath: "M15 5L8 12l7 7"
            hint: qsTr("Previous sentence ([)")
            enabled: backend.sentenceCount > 0
            onClicked: backend.previousSentence()
        }
        FlatButton {
            text: backend.buffering ? "…" : ""
            iconPath: backend.buffering ? "" : backend.playing ? "M8 5h3v14H8z M14 5h3v14h-3z" : "M8 5l11 7-11 7z"
            iconFilled: true
            hint: backend.playing ? qsTr("Pause (Space)") : qsTr("Play (Space)")
            enabled: backend.sentenceCount > 0
            onClicked: backend.playPause()
        }
        FlatButton {
            iconPath: "M9 5l7 7-7 7"
            hint: qsTr("Next sentence (])")
            enabled: backend.sentenceCount > 0
            onClicked: backend.nextSentence()
        }

        Label {
            visible: player.width > 1000
            Layout.preferredWidth: 100
            text: backend.buffering ? qsTr("Preparing…") : backend.playing ? qsTr("Listening") : backend.ttsStatus
            color: theme.muted
            font.pixelSize: 12
            elide: Text.ElideRight
        }

        Slider {
            id: progress
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            from: 0
            to: Math.max(0, backend.sentenceCount - 1)
            stepSize: 1
            enabled: backend.sentenceCount > 0
            value: backend.currentSentence
            Accessible.name: qsTr("Sentence position")
            onMoved: if (!pressed) backend.seekSentence(Math.round(value))
            onPressedChanged: if (!pressed && enabled) backend.seekSentence(Math.round(value))
            background: Rectangle {
                x: progress.leftPadding
                y: progress.topPadding + progress.availableHeight / 2 - height / 2
                width: progress.availableWidth
                height: 2
                color: theme.border
                Rectangle { width: progress.visualPosition * parent.width; height: parent.height; color: theme.accent }
            }
            handle: Rectangle {
                x: progress.leftPadding + progress.visualPosition * (progress.availableWidth - width)
                y: progress.topPadding + progress.availableHeight / 2 - height / 2
                width: 8; height: 8; radius: 4
                color: progress.activeFocus || progress.pressed ? theme.accent : theme.foreground
            }
        }

        Label {
            text: backend.sentenceCount > 0 ? qsTr("%1 / %2").arg(backend.currentSentence + 1).arg(backend.sentenceCount) : ""
            color: theme.muted
            font.family: theme.monoFont
            font.pixelSize: 11
            Accessible.name: qsTr("Sentence %1 of %2").arg(backend.currentSentence + 1).arg(backend.sentenceCount)
        }
        FlatButton {
            visible: player.width > 620
            text: backend.speed.toFixed(1) + "×"
            hint: qsTr("Playback speed; use − and + to change")
            font.pixelSize: 12
            onClicked: speedMenu.open()
            SiftMenu {
                id: speedMenu
                implicitWidth: 150
                Repeater {
                    model: [0.75, 1.0, 1.25, 1.5, 1.75, 2.0]
                    SiftMenuItem {
                        required property real modelData
                        implicitWidth: 150
                        text: modelData.toFixed(2).replace(/0+$/, "").replace(/\.$/, "") + "×"
                        checkable: true
                        checked: Math.abs(backend.speed - modelData) < 0.01
                        onTriggered: backend.setSpeed(modelData)
                    }
                }
            }
        }
        FlatButton {
            visible: player.width > 780
            text: backend.voice
            hint: qsTr("Choose voice (V)")
            font.pixelSize: 11
            onClicked: player.openVoice()
        }
    }
}
