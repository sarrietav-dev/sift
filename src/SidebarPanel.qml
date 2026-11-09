import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: sidebar
    color: theme.background

    property bool searchActive: searchField.activeFocus

    function focusSearch() {
        searchField.forceActiveFocus()
        searchField.selectAll()
    }

    function escapeSearch() {
        if (searchActive) {
            searchField.text = ""
            backend.setSearch("")
            searchField.focus = false
        }
    }

    function moveSelection(delta) {
        if (backend.section === "library") {
            if (libraryList.count === 0) return
            libraryList.currentIndex = Math.max(0, Math.min(libraryList.count - 1, libraryList.currentIndex + delta))
            libraryList.positionViewAtIndex(libraryList.currentIndex, ListView.Contain)
        } else if (backend.section === "playlists") {
            if (playlistList.count === 0) return
            playlistList.currentIndex = Math.max(0, Math.min(playlistList.count - 1, playlistList.currentIndex + delta))
            playlistList.positionViewAtIndex(playlistList.currentIndex, ListView.Contain)
        } else {
            if (queueList.count === 0) return
            queueList.currentIndex = Math.max(0, Math.min(queueList.count - 1, queueList.currentIndex + delta))
            queueList.positionViewAtIndex(queueList.currentIndex, ListView.Contain)
        }
    }

    function activateSelection() {
        if (backend.section === "library") {
            if (libraryList.currentIndex < 0 || libraryList.currentIndex >= backend.articles.length) return
            backend.openArticle(backend.articles[libraryList.currentIndex].id, false)
        } else if (backend.section === "playlists") {
            if (playlistList.currentIndex < 0 || playlistList.currentIndex >= backend.playlists.length) return
            backend.playPlaylist(backend.playlists[playlistList.currentIndex].id)
        } else {
            if (queueList.currentIndex < 0 || queueList.currentIndex >= backend.queue.length) return
            backend.playQueueAt(queueList.currentIndex)
        }
    }

    function syncLibraryIndex() {
        if (!backend.hasArticle) return
        const id = backend.currentArticle.id
        for (let i = 0; i < backend.articles.length; i++) {
            if (backend.articles[i].id === id) {
                libraryList.currentIndex = i
                return
            }
        }
    }

    function syncPlaylistIndex() {
        for (let i = 0; i < backend.playlists.length; i++) {
            if (backend.playlists[i].id === backend.selectedPlaylistId) {
                playlistList.currentIndex = i
                return
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 2

            Repeater {
                model: [
                    { key: "library", label: qsTr("Library") },
                    { key: "playlists", label: qsTr("Playlists") },
                    { key: "queue", label: qsTr("Queue") }
                ]

                Button {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.label
                    flat: true
                    checkable: true
                    checked: backend.section === modelData.key
                    font.pixelSize: 12
                    onClicked: backend.setSection(modelData.key)
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: theme.border
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: backend.section === "library" ? 0 : backend.section === "playlists" ? 1 : 2

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    anchors.margins: 10

                    TextField {
                        id: searchField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Search library")
                        font.pixelSize: 13
                        selectByMouse: true
                        onTextChanged: searchTimer.restart()
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Repeater {
                            model: [
                                { key: "all", label: qsTr("All") },
                                { key: "unread", label: qsTr("Unread") },
                                { key: "reading", label: qsTr("Reading") },
                                { key: "read", label: qsTr("Read") }
                            ]

                            Button {
                                required property var modelData
                                Layout.fillWidth: true
                                text: modelData.label
                                flat: true
                                checkable: true
                                checked: backend.filter === modelData.key
                                font.pixelSize: 10
                                padding: 4
                                onClicked: backend.setFilter(modelData.key)
                            }
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: backend.tags.length > 0

                        Repeater {
                            model: backend.tags

                            Button {
                                required property var modelData
                                text: "#" + modelData.name
                                flat: true
                                checkable: true
                                checked: backend.selectedTag === modelData.name
                                font.pixelSize: 10
                                padding: 4
                                onClicked: backend.setTag(checked ? "" : modelData.name)
                            }
                        }
                    }

                    ListView {
                        id: libraryList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        model: backend.articles
                        currentIndex: 0
                        ScrollBar.vertical: ScrollBar { }

                        delegate: Rectangle {
                            required property int index
                            required property var modelData

                            width: ListView.view.width
                            height: 72
                            radius: Math.min(5, theme.radius)
                            color: libraryList.currentIndex === index ? theme.panel : "transparent"

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    libraryList.currentIndex = index
                                    backend.openArticle(modelData.id, false)
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 8

                                Rectangle {
                                    width: 6
                                    height: 6
                                    radius: 3
                                    color: modelData.readState === 0 ? theme.accent : "transparent"
                                    border.width: modelData.readState === 0 ? 0 : 1
                                    border.color: theme.border
                                    Layout.alignment: Qt.AlignTop
                                    Layout.topMargin: 5
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.title
                                        color: theme.foreground
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: (modelData.domain || "")
                                            + (modelData.readState === 1 ? " · " + qsTr("Reading") : "")
                                            + (modelData.readState === 2 ? " · " + qsTr("Read") : "")
                                        color: theme.muted
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: libraryList.count === 0
                            text: backend.search !== "" || backend.selectedTag !== ""
                                ? qsTr("No matching pages.") : qsTr("Nothing saved yet.")
                            color: theme.muted
                            font.pixelSize: 12
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    anchors.margins: 10

                    ListView {
                        id: playlistList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(230, contentHeight)
                        clip: true
                        spacing: 2
                        model: backend.playlists
                        currentIndex: 0
                        ScrollBar.vertical: ScrollBar { }

                        delegate: Rectangle {
                            required property int index
                            required property var modelData

                            width: ListView.view.width
                            height: 46
                            radius: Math.min(5, theme.radius)
                            color: playlistList.currentIndex === index || backend.selectedPlaylistId === modelData.id
                                ? theme.panel : "transparent"

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    playlistList.currentIndex = index
                                    backend.selectPlaylist(modelData.id)
                                }
                                onDoubleClicked: backend.playPlaylist(modelData.id)
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 9
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
                            text: qsTr("No playlists yet.")
                            color: theme.muted
                            font.pixelSize: 12
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        TextField {
                            id: newPlaylistName
                            Layout.fillWidth: true
                            placeholderText: qsTr("New playlist")
                            font.pixelSize: 11
                            selectByMouse: true
                            onAccepted: {
                                backend.createPlaylist(text)
                                text = ""
                            }
                        }

                        ToolButton {
                            text: "+"
                            font.pixelSize: 15
                            onClicked: {
                                backend.createPlaylist(newPlaylistName.text)
                                newPlaylistName.text = ""
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: backend.selectedPlaylistId !== ""
                        spacing: 6

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Playlist")
                            color: theme.muted
                            font.pixelSize: 10
                        }

                        Button {
                            text: qsTr("Play")
                            font.pixelSize: 10
                            padding: 4
                            onClicked: backend.playPlaylist(backend.selectedPlaylistId)
                        }
                    }

                    ListView {
                        id: playlistItems
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        model: backend.selectedPlaylistItems
                        ScrollBar.vertical: ScrollBar { }

                        delegate: Rectangle {
                            required property var modelData

                            width: ListView.view.width
                            height: 54
                            radius: Math.min(5, theme.radius)
                            color: backend.currentArticle.id === modelData.id ? theme.panel : "transparent"

                            MouseArea {
                                anchors.fill: parent
                                onClicked: backend.openArticle(modelData.id, false)
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 9
                                anchors.rightMargin: 4
                                spacing: 6

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.title
                                        color: theme.foreground
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.domain
                                        color: theme.muted
                                        font.pixelSize: 9
                                        elide: Text.ElideRight
                                    }
                                }

                                ToolButton {
                                    text: "×"
                                    font.pixelSize: 12
                                    onClicked: backend.removeFromPlaylist(backend.selectedPlaylistId, modelData.id)
                                }
                            }
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    anchors.margins: 10

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Up next")
                            color: theme.muted
                            font.pixelSize: 10
                        }

                        ToolButton {
                            text: qsTr("Clear")
                            font.pixelSize: 10
                            onClicked: backend.clearQueue()
                        }
                    }

                    ListView {
                        id: queueList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        model: backend.queue
                        currentIndex: 0
                        ScrollBar.vertical: ScrollBar { }

                        delegate: Rectangle {
                            required property int index
                            required property var modelData

                            width: ListView.view.width
                            height: 56
                            radius: Math.min(5, theme.radius)
                            color: backend.currentArticle.id === modelData.id ? theme.panel
                                : queueList.currentIndex === index ? theme.panel : "transparent"

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    queueList.currentIndex = index
                                    backend.playQueueAt(index)
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 9
                                anchors.rightMargin: 4
                                spacing: 6

                                Label {
                                    text: index + 1
                                    color: theme.muted
                                    font.pixelSize: 10
                                    Layout.alignment: Qt.AlignTop
                                    Layout.topMargin: 4
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.title
                                        color: theme.foreground
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.domain
                                        color: theme.muted
                                        font.pixelSize: 9
                                        elide: Text.ElideRight
                                    }
                                }

                                ToolButton {
                                    text: "×"
                                    font.pixelSize: 12
                                    onClicked: backend.removeFromQueue(modelData.id)
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: queueList.count === 0
                            text: qsTr("The queue is empty.")
                            color: theme.muted
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            color: theme.panel

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 4
                spacing: 6

                Rectangle {
                    width: 7
                    height: 7
                    radius: 4
                    color: backend.kokoroHealthy ? theme.green : theme.red
                }

                Label {
                    Layout.fillWidth: true
                    text: backend.kokoroStatus
                    color: theme.muted
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }

                ToolButton {
                    text: "↻"
                    font.pixelSize: 12
                    onClicked: backend.checkKokoro()
                }
            }
        }
    }

    Timer {
        id: searchTimer
        interval: 220
        repeat: false
        onTriggered: backend.setSearch(searchField.text)
    }

    Connections {
        target: backend
        function onArticlesChanged() { sidebar.syncLibraryIndex() }
        function onCurrentArticleChanged() { sidebar.syncLibraryIndex() }
        function onPlaylistsChanged() { sidebar.syncPlaylistIndex() }
        function onSelectedPlaylistChanged() { sidebar.syncPlaylistIndex() }
        function onSearchChanged() {
            if (backend.search !== searchField.text)
                searchField.text = backend.search
        }
    }
}
