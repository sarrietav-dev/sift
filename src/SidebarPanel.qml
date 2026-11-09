import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: sidebar
    color: theme.background
    readonly property bool popupOpen: clearDialog.visible || queueMenu.visible || itemMenuOpen
    property bool itemMenuOpen: false
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
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            clip: true
            model: backend.queue
            currentIndex: count ? 0 : -1
            ScrollBar.vertical: ScrollBar { }
            delegate: Item {
                id: row
                required property int index
                required property var modelData
                width: ListView.view.width
                height: 70
                readonly property bool current: backend.hasArticle && backend.currentArticle.id === modelData.id
                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 8; anchors.rightMargin: 8
                    color: row.current || queueList.currentIndex === row.index ? theme.panel : "transparent"
                    radius: Math.min(3, theme.radius)
                }
                Rectangle { anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; width: 2; height: 32; color: theme.accent; visible: row.current }
                MouseArea {
                    anchors.fill: parent
                    onClicked: { queueList.currentIndex = row.index; sidebar.activateSelection() }
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20; anchors.rightMargin: 12
                    spacing: 8
                    Label { text: String(row.index + 1); color: theme.muted; font.family: theme.monoFont; font.pixelSize: 11; Layout.preferredWidth: 20 }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 4
                        Label { Layout.fillWidth: true; text: row.modelData.title; color: theme.foreground; font.pixelSize: 13; elide: Text.ElideRight }
                        Label { Layout.fillWidth: true; text: row.modelData.domain || ""; color: theme.muted; font.pixelSize: 11; elide: Text.ElideRight }
                    }
                    FlatButton {
                        iconPath: "M6 12h.01 M12 12h.01 M18 12h.01"; hint: qsTr("Queue item actions")
                        onClicked: itemMenu.open()
                        SiftMenu {
                            id: itemMenu
                            onVisibleChanged: sidebar.itemMenuOpen = visible
                            SiftMenuItem { text: qsTr("Play"); onTriggered: backend.playQueueAt(row.index) }
                            SiftMenuItem { text: qsTr("Move up"); enabled: row.index > 0; onTriggered: { const from = row.index; queueList.currentIndex = from - 1; backend.moveQueueItem(from, from - 1) } }
                            SiftMenuItem { text: qsTr("Move down"); enabled: row.index < queueList.count - 1; onTriggered: { const from = row.index; queueList.currentIndex = from + 1; backend.moveQueueItem(from, from + 1) } }
                            SiftMenuSeparator { }
                            SiftMenuItem { text: qsTr("Remove from queue"); destructive: true; onTriggered: backend.removeFromQueue(row.modelData.id) }
                        }
                    }
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
                text: qsTr("J/K move  ·  Enter open  ·  F close")
                color: theme.muted
                font.family: theme.monoFont
                font.pixelSize: 10
            }
        }
    }

    Connections {
        target: backend
        function onCurrentArticleChanged() {
            if (!backend.hasArticle)
                return
            for (let i = 0; i < backend.queue.length; i++) {
                if (backend.queue[i].id === backend.currentArticle.id) {
                    queueList.currentIndex = i
                    return
                }
            }
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
