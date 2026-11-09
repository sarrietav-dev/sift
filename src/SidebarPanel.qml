import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: sidebar
    color: theme.background
    readonly property bool popupOpen: clearDialog.visible || queueMenu.visible || itemMenuOpen
    property bool itemMenuOpen: false
    readonly property int rowHeight: 64
    // While a row is dragged: where it came from and where it would land.
    property int dragFrom: -1
    property int dragTo: -1
    signal addPage()

    function moveSelection(delta) {
        if (queueList.count === 0) return
        queueList.currentIndex = Math.max(0, Math.min(queueList.count - 1, queueList.currentIndex + delta))
        queueList.positionViewAtIndex(queueList.currentIndex, ListView.Contain)
    }
    function activateSelection() {
        if (queueList.currentIndex >= 0 && queueList.currentIndex < backend.queue.length) {
            const id = backend.queue[queueList.currentIndex].id
            if (!backend.hasArticle || backend.currentArticle.id !== id)
                backend.openArticle(id, false)
        }
    }
    function moveItem(from, to) {
        to = Math.max(0, Math.min(queueList.count - 1, to))
        if (from < 0 || from >= queueList.count || from === to)
            return
        backend.moveQueueItem(from, to)
        queueList.currentIndex = to
        queueList.positionViewAtIndex(to, ListView.Contain)
    }
    // Moves the selected page by one place, from the keyboard.
    function moveSelected(delta) {
        moveItem(queueList.currentIndex, queueList.currentIndex + delta)
    }
    function removeSelected() {
        const index = queueList.currentIndex
        if (index < 0 || index >= backend.queue.length)
            return
        backend.removeFromQueue(backend.queue[index].id)
        queueList.currentIndex = Math.min(index, queueList.count - 1)
    }
    function currentQueueIndex() {
        if (!backend.hasArticle)
            return -1
        for (let i = 0; i < backend.queue.length; i++) {
            if (backend.queue[i].id === backend.currentArticle.id)
                return i
        }
        return -1
    }
    // Places a page right after the one being read.
    function playNext(from) {
        const current = currentQueueIndex()
        if (current < 0 || from === current)
            return
        moveItem(from, from > current ? current + 1 : current)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 10
            Layout.preferredHeight: 54
            Label { text: qsTr("Queue"); color: theme.foreground; font.pixelSize: 15; font.weight: Font.DemiBold }
            Label { text: backend.queue.length; color: theme.muted; font.family: theme.monoFont; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            FlatButton { iconPath: "M12 5v14 M5 12h14"; hint: qsTr("Add a page (A)"); enabled: !backend.busy; onClicked: sidebar.addPage() }
            FlatButton { iconPath: "M6 12h.01 M12 12h.01 M18 12h.01"; hint: qsTr("Queue actions"); enabled: backend.queue.length > 0; onClicked: queueMenu.open()
                SiftMenu {
                    id: queueMenu
                    SiftMenuItem { text: qsTr("Clear queue…"); destructive: true; onTriggered: clearDialog.open() }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: theme.border }
        ListView {
            id: queueList
            objectName: "queueList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 6
            clip: true
            model: backend.queue
            currentIndex: count ? 0 : -1
            interactive: sidebar.dragFrom < 0
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }

            delegate: Item {
                id: row
                required property int index
                required property var modelData
                width: ListView.view.width
                height: sidebar.rowHeight
                z: dragging ? 10 : 0
                readonly property bool current: backend.hasArticle && backend.currentArticle.id === modelData.id
                readonly property bool dragging: sidebar.dragFrom === index
                // Rows between the dragged one and its target slide aside to
                // show where it will land.
                readonly property real shift: sidebar.dragFrom < 0 || dragging ? 0
                    : index > sidebar.dragFrom && index <= sidebar.dragTo ? -sidebar.rowHeight
                    : index < sidebar.dragFrom && index >= sidebar.dragTo ? sidebar.rowHeight : 0
                transform: Translate {
                    y: row.shift
                    Behavior on y { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                }

                Item {
                    id: card
                    width: row.width
                    height: row.height

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 8; anchors.rightMargin: 8
                        anchors.topMargin: 2; anchors.bottomMargin: 2
                        radius: Math.min(5, theme.radius)
                        color: row.dragging ? theme.panel
                            : row.current || queueList.currentIndex === row.index ? theme.panel
                            : rowHover.hovered ? theme.withAlpha(theme.panel, 0.6) : "transparent"
                        border.width: row.dragging ? 1 : 0
                        border.color: theme.accent
                    }
                    Rectangle {
                        anchors.left: parent.left; anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 2; height: 30
                        color: theme.accent
                        visible: row.current
                    }
                    HoverHandler { id: rowHover }
                    TapHandler {
                        onTapped: { queueList.currentIndex = row.index; sidebar.activateSelection() }
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14; anchors.rightMargin: 12
                        spacing: 6

                        // The position number turns into a grip on hover;
                        // drag it to reorder.
                        Item {
                            Layout.preferredWidth: 24
                            Layout.fillHeight: true
                            Label {
                                anchors.centerIn: parent
                                visible: !rowHover.hovered && !row.dragging
                                text: String(row.index + 1 + Math.sign(row.shift))
                                color: theme.muted
                                font.family: theme.monoFont
                                font.pixelSize: 11
                            }
                            SiftIcon {
                                anchors.centerIn: parent
                                visible: rowHover.hovered || row.dragging
                                size: 16
                                path: "M9 6h.01 M15 6h.01 M9 12h.01 M15 12h.01 M9 18h.01 M15 18h.01"
                                color: grip.containsMouse || row.dragging ? theme.foreground : theme.muted
                            }
                            MouseArea {
                                id: grip
                                anchors.fill: parent
                                hoverEnabled: true
                                preventStealing: true
                                cursorShape: row.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                drag.target: card
                                drag.axis: Drag.YAxis
                                drag.threshold: 2
                                Accessible.name: qsTr("Drag to reorder")
                                onPressed: {
                                    queueList.currentIndex = row.index
                                    sidebar.dragFrom = row.index
                                    sidebar.dragTo = row.index
                                }
                                onPositionChanged: {
                                    if (!drag.active)
                                        return
                                    const center = row.y + card.y + card.height / 2
                                    sidebar.dragTo = Math.max(0, Math.min(queueList.count - 1,
                                                                          Math.floor(center / sidebar.rowHeight)))
                                    const inView = row.y + card.y - queueList.contentY
                                    autoScroll.direction = inView < 0 ? -1
                                        : inView + card.height > queueList.height ? 1 : 0
                                }
                                onReleased: finish()
                                onCanceled: finish()
                                function finish() {
                                    autoScroll.direction = 0
                                    const from = sidebar.dragFrom
                                    const to = sidebar.dragTo
                                    sidebar.dragFrom = -1
                                    sidebar.dragTo = -1
                                    card.y = 0
                                    sidebar.moveItem(from, to)
                                }
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 3
                            Label { Layout.fillWidth: true; text: row.modelData.title; color: theme.foreground; font.pixelSize: 13; elide: Text.ElideRight }
                            Label { Layout.fillWidth: true; text: row.modelData.domain || ""; color: theme.muted; font.pixelSize: 11; elide: Text.ElideRight }
                        }
                        FlatButton {
                            iconPath: "M6 12h.01 M12 12h.01 M18 12h.01"; hint: qsTr("Queue item actions")
                            opacity: rowHover.hovered || itemMenu.visible || queueList.currentIndex === row.index ? 1 : 0
                            onClicked: itemMenu.open()
                            SiftMenu {
                                id: itemMenu
                                onVisibleChanged: sidebar.itemMenuOpen = visible
                                SiftMenuItem { text: qsTr("Play"); onTriggered: backend.playQueueAt(row.index) }
                                SiftMenuItem {
                                    text: qsTr("Play next")
                                    enabled: sidebar.currentQueueIndex() >= 0 && !row.current
                                        && row.index !== sidebar.currentQueueIndex() + 1
                                    onTriggered: sidebar.playNext(row.index)
                                }
                                SiftMenuSeparator { }
                                SiftMenuItem { text: qsTr("Move to top"); enabled: row.index > 0; onTriggered: sidebar.moveItem(row.index, 0) }
                                SiftMenuItem { text: qsTr("Move up"); shortcut: "Shift+K"; enabled: row.index > 0; onTriggered: sidebar.moveItem(row.index, row.index - 1) }
                                SiftMenuItem { text: qsTr("Move down"); shortcut: "Shift+J"; enabled: row.index < queueList.count - 1; onTriggered: sidebar.moveItem(row.index, row.index + 1) }
                                SiftMenuItem { text: qsTr("Move to bottom"); enabled: row.index < queueList.count - 1; onTriggered: sidebar.moveItem(row.index, queueList.count - 1) }
                                SiftMenuSeparator { }
                                SiftMenuItem { text: qsTr("Remove from queue"); shortcut: "Del"; destructive: true; onTriggered: backend.removeFromQueue(row.modelData.id) }
                            }
                        }
                    }
                }
            }

            Timer {
                id: autoScroll
                property int direction: 0
                interval: 16
                repeat: true
                running: direction !== 0
                onTriggered: {
                    const limit = Math.max(0, queueList.contentHeight - queueList.height)
                    queueList.contentY = Math.max(0, Math.min(limit, queueList.contentY + direction * 6))
                }
            }

            Column {
                anchors.centerIn: parent
                width: parent.width - 48
                spacing: 10
                visible: queueList.count === 0
                Label { width: parent.width; text: qsTr("Your queue is empty"); color: theme.foreground; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter }
                Label { width: parent.width; text: qsTr("Add a page to read or listen."); color: theme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter }
                FlatButton { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("+ Add page"); enabled: !backend.busy; onClicked: sidebar.addPage() }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: theme.background
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: theme.border }
            Label {
                anchors.centerIn: parent
                width: parent.width - 16
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("J/K select  ·  Shift+J/K move  ·  Drag to reorder")
                color: theme.muted
                font.family: theme.monoFont
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }
    }

    Connections {
        target: backend
        function onCurrentArticleChanged() {
            const index = sidebar.currentQueueIndex()
            if (index >= 0)
                queueList.currentIndex = index
        }
    }
    SiftDialog {
        id: clearDialog
        title: qsTr("Clear the queue?")
        Label { text: qsTr("Saved article files will not be deleted."); color: theme.foreground }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            FlatButton { text: qsTr("Cancel"); onClicked: clearDialog.close() }
            FlatButton { text: qsTr("Clear queue"); primary: true; onClicked: { backend.clearQueue(); clearDialog.close() } }
            Item { Layout.preferredWidth: 16 }
        }
    }
}
