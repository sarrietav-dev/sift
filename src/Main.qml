import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtWebEngine

ApplicationWindow {
    id: win
    width: 1180
    height: 780
    minimumWidth: 780
    minimumHeight: 520
    visible: true
    title: backend.hasArticle ? backend.currentArticle.title + " — sift" : "sift"
    color: theme.background

    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.primary: theme.accent
    Material.background: theme.background
    Material.foreground: theme.foreground
    Material.roundedScale: theme.radius <= 0 ? Material.NotRounded
        : theme.radius <= 3 ? Material.ExtraSmallScale
        : theme.radius <= 7 ? Material.SmallScale
        : theme.radius <= 11 ? Material.MediumScale
        : theme.radius <= 16 ? Material.LargeScale
        : Material.ExtraLargeScale
    font.family: theme.uiFont

    property bool addOpen: false
    property bool voiceOpen: false
    property bool settingsOpen: false
    property bool tagsOpen: false
    property bool playlistOpen: false
    property bool helpOpen: false
    property bool focusMode: false
    property string toastText: ""
    property bool readerReady: false
    property bool modalOpen: addDialog.visible || voiceDialog.visible || settingsDialog.visible
        || tagsDialog.visible || playlistDialog.visible || helpOpen

    function showToast(message) {
        toastText = message
        toastTimer.restart()
    }

    function applyReaderTheme() {
        if (!backend.hasArticle || !win.readerReady)
            return
        readerView.runJavaScript("siftApplyTheme(" + JSON.stringify({
            background: theme.background,
            foreground: theme.foreground,
            muted: theme.muted,
            accent: theme.accent,
            selection: theme.selection,
            panel: theme.panel,
            border: theme.border,
            radius: theme.radius
        }) + ");")
        readerView.runJavaScript("siftSetFontSize(" + backend.fontSize + ");")
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: win.helpOpen = !win.helpOpen }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: backend.playPause() }
    Shortcut { sequence: "["; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: backend.previousSentence() }
    Shortcut { sequence: "]"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: backend.nextSentence() }
    Shortcut { sequence: "-"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: backend.setSpeed(Math.max(0.5, backend.speed - 0.1)) }
    Shortcut { sequence: "+"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: backend.setSpeed(Math.min(2.0, backend.speed + 0.1)) }
    Shortcut { sequence: "="; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: backend.setSpeed(Math.min(2.0, backend.speed + 0.1)) }
    Shortcut { sequence: "V"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: voiceDialog.open() }
    Shortcut { sequence: "A"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: addDialog.open() }
    Shortcut { sequence: "/"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: { win.focusMode = false; sidebar.focusSearch() } }
    Shortcut { sequence: "T"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: tagsDialog.open() }
    Shortcut { sequence: "M"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: backend.toggleCurrentRead() }
    Shortcut { sequence: "U"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: backend.addCurrentToQueue() }
    Shortcut { sequence: "Shift+P"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: playlistDialog.open() }
    Shortcut { sequence: "E"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: backend.copyMarkdown() }
    Shortcut { sequence: "F"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: win.focusMode = !win.focusMode }
    Shortcut { sequence: "Ctrl+,"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: settingsDialog.open() }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: backend.setSection("library") }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: backend.setSection("playlists") }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: backend.setSection("queue") }
    Shortcut {
        sequence: "J"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive
        onActivated: win.focusMode ? readerView.runJavaScript("window.scrollBy(0, 140)") : sidebar.moveSelection(1)
    }
    Shortcut {
        sequence: "K"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive
        onActivated: win.focusMode ? readerView.runJavaScript("window.scrollBy(0, -140)") : sidebar.moveSelection(-1)
    }
    Shortcut { sequence: "Return"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: sidebar.activateSelection() }
    Shortcut { sequence: "PageDown"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: readerView.runJavaScript("window.scrollBy(0, window.innerHeight * 0.85)") }
    Shortcut { sequence: "PageUp"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: readerView.runJavaScript("window.scrollBy(0, -window.innerHeight * 0.85)") }
    Shortcut {
        sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen
        onActivated: sidebar.escapeSearch()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            color: theme.background

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                spacing: 10

                Label {
                    text: "sift"
                    color: theme.accent
                    font.pixelSize: 19
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    width: 1
                    height: 22
                    color: theme.border
                    visible: backend.hasArticle
                }

                Label {
                    Layout.fillWidth: true
                    text: backend.hasArticle ? backend.currentArticle.title : backend.status
                    color: backend.hasArticle ? theme.foreground : theme.muted
                    font.pixelSize: 14
                    elide: Text.ElideRight
                }

                ToolButton {
                    text: qsTr("Add")
                    font.pixelSize: 13
                    onClicked: addDialog.open()
                }

                ToolButton {
                    text: "?"
                    font.pixelSize: 13
                    onClicked: win.helpOpen = !win.helpOpen
                }

                ToolButton {
                    text: "⚙"
                    font.pixelSize: 15
                    onClicked: settingsDialog.open()
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: theme.border
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 42 : 0
            visible: backend.error !== ""
            color: theme.panel

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 8
                spacing: 8

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: theme.red
                }

                Label {
                    Layout.fillWidth: true
                    text: backend.error
                    color: theme.foreground
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }

                ToolButton {
                    text: "×"
                    font.pixelSize: 17
                    onClicked: backend.clearError()
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: theme.border
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            SidebarPanel {
                id: sidebar
                visible: !win.focusMode
                Layout.fillHeight: true
                Layout.preferredWidth: visible ? 326 : 0
            }

            Rectangle {
                visible: !win.focusMode
                Layout.fillHeight: true
                width: 1
                color: theme.border
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                WebEngineView {
                    id: readerView
                    objectName: "readerView"
                    anchors.fill: parent
                    visible: backend.hasArticle
                    focus: false
                    backgroundColor: theme.background
                    settings.javascriptEnabled: true
                    settings.localContentCanAccessRemoteUrls: true
                    settings.localContentCanAccessFileUrls: true

                    onLoadingChanged: function(request) {
                        if (request.status === WebEngineView.LoadSucceededStatus) {
                            win.readerReady = true
                            wrapTimer.restart()
                        } else if (request.status === WebEngineView.LoadFailedStatus) {
                            win.readerReady = false
                            backend.sentencesReady([])
                        }
                    }

                    onNavigationRequested: function(request) {
                        const target = request.url.toString()
                        if (target.indexOf("https://sift.invalid/") === 0) {
                            request.action = WebEngineNavigationRequest.IgnoreRequest
                            backend.handleReaderUrl(target)
                        } else if (target.indexOf("data:") !== 0 && target.indexOf("about:") !== 0) {
                            request.action = WebEngineNavigationRequest.IgnoreRequest
                            backend.openExternal(target)
                        }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    visible: !backend.hasArticle
                    color: theme.background

                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 80, 540)
                        spacing: 12

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: "sift"
                            color: theme.accent
                            font.pixelSize: 54
                            font.weight: Font.DemiBold
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Read the web with Kokoro.")
                            color: theme.foreground
                            font.pixelSize: 18
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Press A to add a page, or choose one from the library. Space starts reading.")
                            color: theme.muted
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Button {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.topMargin: 10
                            text: qsTr("Add a page")
                            highlighted: true
                            onClicked: addDialog.open()
                        }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    visible: backend.busy
                    color: Qt.rgba(0, 0, 0, 0.45)

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 14

                        BusyIndicator {
                            Layout.alignment: Qt.AlignHCenter
                            running: backend.busy
                            Material.accent: theme.accent
                        }

                        Label {
                            text: backend.progress > 0 ? qsTr("Reading page… %1%").arg(backend.progress) : qsTr("Reading page…")
                            color: "white"
                            font.pixelSize: 14
                        }
                    }
                }

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 18
                    width: toastLabel.implicitWidth + 30
                    height: 38
                    radius: Math.min(6, theme.radius)
                    color: theme.panel
                    border.width: 1
                    border.color: theme.border
                    visible: win.toastText !== ""
                    opacity: visible ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 140 } }

                    Label {
                        id: toastLabel
                        anchors.centerIn: parent
                        text: win.toastText
                        color: theme.foreground
                        font.pixelSize: 13
                    }
                }
            }
        }

        PlayerBar {
            id: playerBar
            Layout.fillWidth: true
            visible: backend.hasArticle
            onOpenVoice: voiceDialog.open()
        }
    }

    Timer {
        id: wrapTimer
        interval: 140
        repeat: false
        onTriggered: {
            if (!backend.hasArticle)
                return
            readerView.runJavaScript("siftWrapSentences();", function(result) {
                let sentences = []
                try {
                    sentences = JSON.parse(result)
                } catch (error) {
                    sentences = []
                }
                backend.sentencesReady(sentences)
                win.applyReaderTheme()
            })
        }
    }

    Timer {
        id: toastTimer
        interval: 2800
        repeat: false
        onTriggered: win.toastText = ""
    }

    Connections {
        target: backend
        function onReaderLoad(html, baseUrl) {
            win.readerReady = false
            readerView.loadHtml(html, baseUrl)
        }
        function onHighlightSentence(index) {
            readerView.runJavaScript("siftSetCurrent(" + index + ");")
            readerView.runJavaScript("siftScrollToCurrent(" + index + ", true);")
        }
        function onToast(message) {
            win.showToast(message)
        }
        function onFontSizeChanged() {
            if (backend.hasArticle && win.readerReady)
                readerView.runJavaScript("siftSetFontSize(" + backend.fontSize + ");")
        }
        function onCurrentArticleChanged() {
            win.applyReaderTheme()
        }
    }

    Connections {
        target: theme
        function onChanged() {
            win.applyReaderTheme()
        }
    }

    AddPageDialog {
        id: addDialog
    }

    VoicePickerDialog {
        id: voiceDialog
    }

    SettingsDialog {
        id: settingsDialog
    }

    TagsDialog {
        id: tagsDialog
    }

    PlaylistPickerDialog {
        id: playlistDialog
    }

    ShortcutsOverlay {
        id: shortcutsOverlay
        visible: win.helpOpen
        onClose: win.helpOpen = false
    }
}
