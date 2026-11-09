import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A themed drop-down list. Long lists get a search field that filters as you
// type; arrows move, Enter picks, Escape closes. The current choice carries a
// check mark and is scrolled into view when the list opens. With previewFonts,
// each entry is drawn in the font it names.
FocusScope {
    id: control
    property var model: []
    property int currentIndex: -1
    property string hint: ""
    property bool previewFonts: false
    property bool searchable: count > 10
    readonly property int count: model ? model.length : 0
    readonly property string currentText: currentIndex >= 0 && currentIndex < count ? String(model[currentIndex]) : ""
    readonly property bool popupOpen: popup.visible
    readonly property var filtered: {
        const needle = search.text.trim().toLocaleLowerCase()
        const result = []
        for (let i = 0; i < count; i++) {
            if (needle === "" || String(model[i]).toLocaleLowerCase().indexOf(needle) >= 0)
                result.push(i)
        }
        return result
    }
    signal activated(int index)

    implicitWidth: 240
    implicitHeight: 40
    activeFocusOnTab: true
    Accessible.role: Accessible.ComboBox
    Accessible.name: hint
    Accessible.description: currentText

    function openPopup() {
        if (count === 0)
            return
        search.text = ""
        popup.open()
    }
    function choose(index) {
        if (index < 0 || index >= count)
            return
        currentIndex = index
        activated(index)
        popup.close()
        control.forceActiveFocus()
    }
    function step(delta) {
        if (count === 0)
            return
        const next = Math.max(0, Math.min(count - 1, currentIndex + delta))
        if (next !== currentIndex) {
            currentIndex = next
            activated(next)
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                || (event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier))) {
            openPopup()
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            step(1)
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            step(-1)
            event.accepted = true
        }
    }

    Rectangle {
        id: field
        anchors.fill: parent
        color: hover.hovered && control.enabled ? Qt.lighter(theme.panel, theme.dark ? 1.12 : 0.97) : theme.panel
        border.color: control.activeFocus || popup.visible ? theme.accent : theme.border
        radius: Math.min(4, theme.radius)
        Behavior on color { ColorAnimation { duration: 100 } }

        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler {
            enabled: control.enabled
            onTapped: {
                control.forceActiveFocus()
                popup.visible ? popup.close() : control.openPopup()
            }
        }

        Text {
            id: label
            anchors.left: parent.left
            anchors.right: chevron.left
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: control.currentText !== "" ? control.currentText : control.hint
            font.pixelSize: 13
            font.family: control.previewFonts && control.currentText !== "" ? control.currentText : theme.uiFont
            color: !control.enabled || control.currentText === "" ? theme.muted : theme.foreground
            elide: Text.ElideRight
        }

        SiftIcon {
            id: chevron
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            size: 14
            path: "M6 9l6 6 6-6"
            color: theme.muted
            rotation: popup.visible ? 180 : 0
            Behavior on rotation { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        }
    }

    Popup {
        id: popup
        y: control.height + 4
        width: control.width
        margins: 8
        padding: 4
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

        onOpened: {
            const position = control.filtered.indexOf(control.currentIndex)
            list.currentIndex = Math.max(0, position)
            list.positionViewAtIndex(list.currentIndex, ListView.Center)
            if (control.searchable)
                search.forceActiveFocus()
            else
                list.forceActiveFocus()
        }
        onClosed: control.forceActiveFocus()

        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120; easing.type: Easing.OutCubic }
                NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 120; easing.type: Easing.OutCubic }
            }
        }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 80 } }

        background: Rectangle {
            color: theme.panel
            border.color: theme.border
            radius: Math.min(8, theme.radius)
            Rectangle {
                z: -1
                anchors.fill: parent
                anchors.topMargin: 3
                anchors.bottomMargin: -6
                anchors.leftMargin: -2
                anchors.rightMargin: -2
                radius: parent.radius + 2
                color: "#000000"
                opacity: theme.dark ? 0.35 : 0.12
            }
        }

        contentItem: ColumnLayout {
            spacing: 4

            SiftField {
                id: search
                visible: control.searchable
                Layout.fillWidth: true
                implicitHeight: 34
                placeholderText: qsTr("Search…")
                Accessible.name: qsTr("Search %1").arg(control.hint)
                background: Rectangle {
                    color: theme.background
                    border.color: search.activeFocus ? theme.accent : theme.border
                    radius: Math.min(5, theme.radius)
                }
                onTextChanged: {
                    list.currentIndex = 0
                    list.positionViewAtBeginning()
                }
                Keys.onDownPressed: list.incrementCurrentIndex()
                Keys.onUpPressed: list.decrementCurrentIndex()
                Keys.onReturnPressed: control.choose(control.filtered[list.currentIndex])
                Keys.onEnterPressed: control.choose(control.filtered[list.currentIndex])
            }

            ListView {
                id: list
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 8 * 34)
                clip: true
                model: control.filtered
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: 0
                keyNavigationWraps: false
                ScrollBar.vertical: ScrollBar { policy: list.contentHeight > list.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
                Keys.onReturnPressed: control.choose(control.filtered[currentIndex])
                Keys.onEnterPressed: control.choose(control.filtered[currentIndex])
                Keys.onSpacePressed: control.choose(control.filtered[currentIndex])

                delegate: Rectangle {
                    id: entry
                    required property int index
                    required property int modelData
                    readonly property bool chosen: modelData === control.currentIndex
                    width: ListView.view.width
                    height: 34
                    radius: Math.min(5, theme.radius)
                    color: ListView.isCurrentItem ? theme.withAlpha(theme.accent, 0.16) : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 10
                        spacing: 8
                        SiftIcon {
                            Layout.preferredWidth: 14
                            size: 14
                            opacity: entry.chosen ? 1 : 0
                            path: "M5 12.5l4.5 4.5L19 7.5"
                            color: theme.accent
                        }
                        Text {
                            Layout.fillWidth: true
                            text: String(control.model[entry.modelData])
                            font.pixelSize: control.previewFonts ? 15 : 13
                            font.family: control.previewFonts ? text : theme.uiFont
                            font.weight: entry.chosen ? Font.DemiBold : Font.Normal
                            color: theme.foreground
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    HoverHandler {
                        onHoveredChanged: if (hovered) list.currentIndex = entry.index
                    }
                    TapHandler { onTapped: control.choose(entry.modelData) }
                }
            }

            Text {
                visible: control.filtered.length === 0
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                text: qsTr("No matches")
                color: theme.muted
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
