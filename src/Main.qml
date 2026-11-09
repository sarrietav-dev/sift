import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine

ApplicationWindow {
    id: win
    width: 1180
    height: 780
    minimumWidth: 560
    minimumHeight: 520
    visible: true
    title: backend.hasArticle ? backend.currentArticle.title + " — sift" : "sift"
    color: theme.background

    font.family: theme.uiFont
    palette.window: theme.background
    palette.base: theme.background
    palette.text: theme.foreground
    palette.windowText: theme.foreground
    palette.button: theme.panel
    palette.buttonText: theme.foreground
    palette.highlight: theme.accent
    palette.highlightedText: theme.accentForeground
    palette.placeholderText: theme.muted

    property bool addOpen: false
    property bool voiceOpen: false
    property bool settingsOpen: false
    property bool helpOpen: false
    property bool focusMode: true
    property string toastText: ""
    property bool readerReady: false
    property bool following: true
    readonly property bool controlFocused: activeFocusItem && (activeFocusItem instanceof AbstractButton || activeFocusItem instanceof Slider || activeFocusItem instanceof TextInput || activeFocusItem instanceof ComboBox)
    property bool modalOpen: addDialog.visible || voiceDialog.visible || settingsDialog.visible || helpOpen
        || sidebar.popupOpen || actionsMenu.visible || playerBar.popupOpen

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
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !win.controlFocused; autoRepeat: false; onActivated: backend.playPause() }
    Shortcut { sequence: "["; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: backend.previousSentence() }
    Shortcut { sequence: "]"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: backend.nextSentence() }
    Shortcut { sequence: "-"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: backend.setSpeed(Math.max(0.5, backend.speed - 0.1)) }
    Shortcut { sequence: "+"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: backend.setSpeed(Math.min(2.0, backend.speed + 0.1)) }
    Shortcut { sequence: "="; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: backend.setSpeed(Math.min(2.0, backend.speed + 0.1)) }
    Shortcut { sequence: "V"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: voiceDialog.open() }
    Shortcut { sequence: "A"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !backend.busy; onActivated: addDialog.open() }
    Shortcut { sequence: "E"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: backend.copyMarkdown() }
    Shortcut { sequence: "F"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive; onActivated: win.focusMode = !win.focusMode }
    Shortcut { sequence: "Ctrl+,"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: settingsDialog.open() }
    Shortcut {
        sequence: "J"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive
        onActivated: win.focusMode ? readerView.runJavaScript("siftManualScroll(140)") : sidebar.moveSelection(1)
    }
    Shortcut {
        sequence: "K"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !sidebar.searchActive
        onActivated: win.focusMode ? readerView.runJavaScript("siftManualScroll(-140)") : sidebar.moveSelection(-1)
    }
    Shortcut { sequence: "Return"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !win.focusMode; onActivated: sidebar.activateSelection() }
    Shortcut { sequence: "PageDown"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: readerView.runJavaScript("siftManualScroll(window.innerHeight * 0.85)") }
    Shortcut { sequence: "PageUp"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: readerView.runJavaScript("siftManualScroll(-window.innerHeight * 0.85)") }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !win.focusMode; onActivated: win.focusMode = true }

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

                FlatButton {
                    text: "☰"
                    hint: qsTr("Toggle queue (F)")
                    font.pixelSize: 16
                    onClicked: win.focusMode = !win.focusMode
                }

                Label {
                    Layout.fillWidth: true
                    text: backend.busy ? qsTr("Adding page…") : backend.hasArticle ? (backend.currentArticle.domain || "sift") : "sift"
                    color: theme.muted
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                FlatButton {
                    text: qsTr("+ Add page")
                    hint: qsTr("Add a page (A)")
                    enabled: !backend.busy
                    font.pixelSize: 13
                    onClicked: addDialog.open()
                }

                FlatButton {
                    text: "?"
                    hint: qsTr("Keyboard shortcuts (?)")
                    font.pixelSize: 13
                    onClicked: win.helpOpen = !win.helpOpen
                }

                FlatButton {
                    text: "⋯"
                    hint: qsTr("Article and settings menu")
                    font.pixelSize: 20
                    onClicked: actionsMenu.open()
                    Menu {
                        id: actionsMenu
                        MenuItem { text: qsTr("Open original"); enabled: backend.hasArticle; onTriggered: backend.openOriginal() }
                        MenuItem { text: qsTr("Add to queue"); enabled: backend.hasArticle; onTriggered: backend.addCurrentToQueue() }
                        MenuItem { text: qsTr("Copy Markdown"); enabled: backend.hasArticle; onTriggered: backend.copyMarkdown() }
                        MenuItem { text: qsTr("Follow playback"); enabled: backend.hasArticle; onTriggered: { win.following = true; readerView.runJavaScript("siftResumeFollowing();") } }
                        MenuSeparator { }
                        MenuItem { text: qsTr("Settings…"); onTriggered: settingsDialog.open() }
                    }
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

                FlatButton {
                    text: "×"
                    font.pixelSize: 17
                    hint: qsTr("Dismiss error")
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

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 42 : 0
            visible: backend.hasArticle && !backend.kokoroHealthy
            color: theme.panel
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16; anchors.rightMargin: 12
                Label { Layout.fillWidth: true; text: backend.kokoroStatus; color: theme.foreground; font.pixelSize: 12; elide: Text.ElideRight }
                FlatButton { text: qsTr("Retry"); onClicked: backend.checkKokoro() }
                FlatButton { text: qsTr("Settings"); onClicked: settingsDialog.open() }
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
                Layout.preferredWidth: visible ? Math.min(310, Math.round(win.width * 0.4)) : 0
                onAddPage: addDialog.open()
            }

            Rectangle {
                visible: !win.focusMode
                Layout.fillHeight: true
                Layout.preferredWidth: 1
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
                        if (target.indexOf("https://sift.invalid/follow/paused") === 0) {
                            request.action = WebEngineNavigationRequest.IgnoreRequest
                            win.following = false
                        } else if (target.indexOf("https://sift.invalid/") === 0) {
                            request.action = WebEngineNavigationRequest.IgnoreRequest
                            backend.handleReaderUrl(target)
                        } else if (target.indexOf("data:") !== 0 && target.indexOf("about:") !== 0) {
                            request.action = WebEngineNavigationRequest.IgnoreRequest
                            backend.openExternal(target)
                        }
                    }
                }

                FlatButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 18
                    visible: backend.hasArticle && backend.playing && !win.following && !backend.busy
                    text: qsTr("Return to playback")
                    hint: qsTr("Follow the current sentence")
                    background: Rectangle { color: theme.panel; border.color: theme.border; radius: Math.min(4, theme.radius) }
                    onClicked: { win.following = true; readerView.runJavaScript("siftResumeFollowing();") }
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
                            text: qsTr("Add a page to your queue. Space starts reading aloud.")
                            color: theme.muted
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        FlatButton {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.topMargin: 10
                            text: qsTr("Add a page")
                            primary: true
                            enabled: !backend.busy
                            onClicked: addDialog.open()
                        }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    visible: backend.busy && !backend.hasArticle
                    color: Qt.rgba(0, 0, 0, 0.45)

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 14

                        BusyIndicator {
                            Layout.alignment: Qt.AlignHCenter
                            running: backend.busy
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
            win.following = true
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
        objectName: "addDialog"
    }

    VoicePickerDialog {
        id: voiceDialog
        objectName: "voiceDialog"
    }

    SettingsDialog {
        id: settingsDialog
        objectName: "settingsDialog"
    }

    ShortcutsOverlay {
        id: shortcutsOverlay
        visible: win.helpOpen
        onClose: win.helpOpen = false
    }
}
