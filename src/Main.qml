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

    property bool helpOpen: false
    property bool focusMode: true
    property string toastText: ""
    // The reader page is loaded once; articles are rendered into it.
    readonly property string readerShellUrl: "https://sift.invalid/reader"
    property bool readerReady: false
    property bool readerPainted: false
    property var pendingRender: null
    property bool following: true
    readonly property bool controlFocused: activeFocusItem && (activeFocusItem instanceof AbstractButton || activeFocusItem instanceof Slider || activeFocusItem instanceof TextInput || activeFocusItem instanceof ComboBox)
    readonly property bool narrow: width < 900
    readonly property bool rtl: Qt.application.layoutDirection === Qt.RightToLeft
    property bool modalOpen: addDialog.visible || voiceDialog.visible || settingsDialog.visible || helpOpen
        || sidebar.popupOpen || actionsMenu.visible || playerBar.popupOpen

    function showToast(message) {
        toastText = message
        toastTimer.restart()
    }

    function renderArticle(article) {
        if (!win.readerReady) {
            win.pendingRender = article
            return
        }
        win.pendingRender = null
        win.following = true
        readerView.runJavaScript("siftRender(" + JSON.stringify(article) + ");", function(result) {
            let sentences = []
            try {
                sentences = JSON.parse(result)
            } catch (error) {
                sentences = []
            }
            backend.sentencesReady(article.token, sentences)
        })
    }

    function applyReaderTheme() {
        if (!win.readerReady)
            return
        readerView.runJavaScript("siftApplyTheme(" + JSON.stringify({
            background: theme.background,
            foreground: theme.foreground,
            muted: theme.muted,
            accent: theme.accent,
            selection: theme.selection,
            panel: theme.panel,
            border: theme.border,
            radius: theme.radius,
            dark: theme.dark,
            font: backend.readerFont.replace(/["\\]/g, "")
        }) + ");")
        readerView.runJavaScript("siftSetFontSize(" + backend.fontSize + ");")
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: win.helpOpen = !win.helpOpen }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !win.controlFocused; autoRepeat: false; onActivated: backend.playPause() }
    Shortcut { sequence: "["; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: backend.previousSentence() }
    Shortcut { sequence: "]"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: backend.nextSentence() }
    Shortcut { sequence: "-"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: backend.setSpeed(Math.max(0.5, backend.speed - 0.1)) }
    Shortcut { sequence: "+"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: backend.setSpeed(Math.min(2.0, backend.speed + 0.1)) }
    Shortcut { sequence: "="; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: backend.setSpeed(Math.min(2.0, backend.speed + 0.1)) }
    Shortcut { sequence: "V"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: voiceDialog.open() }
    Shortcut { sequence: "A"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !backend.busy; onActivated: addDialog.open() }
    Shortcut { sequence: "E"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: backend.copyMarkdown() }
    Shortcut { sequence: "F"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: win.focusMode = !win.focusMode }
    Shortcut { sequence: "Ctrl+,"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: settingsDialog.open() }
    Shortcut {
        sequence: "J"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen
        onActivated: win.focusMode ? readerView.runJavaScript("siftManualScroll(140)") : sidebar.moveSelection(1)
    }
    Shortcut {
        sequence: "K"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen
        onActivated: win.focusMode ? readerView.runJavaScript("siftManualScroll(-140)") : sidebar.moveSelection(-1)
    }
    Shortcut { sequence: "Shift+J"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !win.focusMode; onActivated: sidebar.moveSelected(1) }
    Shortcut { sequence: "Shift+K"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !win.focusMode; onActivated: sidebar.moveSelected(-1) }
    Shortcut { sequence: "Delete"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !win.focusMode && !win.controlFocused; onActivated: sidebar.removeSelected() }
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
                    iconPath: "M4 7h16 M4 12h16 M4 17h16"
                    hint: qsTr("Toggle queue (F)")
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
                    iconPath: "M12 5v14 M5 12h14"
                    text: qsTr("Add page")
                    hint: qsTr("Add a page (A)")
                    enabled: !backend.busy
                    font.pixelSize: 13
                    onClicked: addDialog.open()
                }

                FlatButton {
                    iconPath: "M12 17h.01 M9.4 9a2.6 2.6 0 1 1 4.2 2.1c-.9.7-1.6 1.2-1.6 2.4 M12 3a9 9 0 1 0 0 18a9 9 0 0 0 0-18z"
                    hint: qsTr("Keyboard shortcuts (?)")
                    onClicked: win.helpOpen = !win.helpOpen
                }

                FlatButton {
                    iconPath: "M6 12h.01 M12 12h.01 M18 12h.01"
                    hint: qsTr("Article and settings menu")
                    onClicked: actionsMenu.open()
                    SiftMenu {
                        id: actionsMenu
                        objectName: "actionsMenu"
                        SiftMenuItem { text: qsTr("Open original"); enabled: backend.hasArticle; onTriggered: backend.openOriginal() }
                        SiftMenuItem { text: qsTr("Add to queue"); enabled: backend.hasArticle; onTriggered: backend.addCurrentToQueue() }
                        SiftMenuItem { text: qsTr("Copy Markdown"); shortcut: "E"; enabled: backend.hasArticle; onTriggered: backend.copyMarkdown() }
                        SiftMenuItem { text: qsTr("Follow playback"); enabled: backend.hasArticle; onTriggered: { win.following = true; readerView.runJavaScript("siftResumeFollowing();") } }
                        SiftMenuSeparator { }
                        SiftMenuItem { text: qsTr("Keyboard shortcuts"); shortcut: "?"; onTriggered: win.helpOpen = true }
                        SiftMenuItem { text: qsTr("Settings…"); shortcut: "Ctrl+,"; onTriggered: settingsDialog.open() }
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

        Item {
            id: workspace
            Layout.fillWidth: true
            Layout.fillHeight: true
            readonly property bool queueDocked: !win.focusMode && !win.narrow
            readonly property int queueWidth: win.narrow ? Math.min(330, Math.round(win.width * 0.88)) : 300

            Rectangle {
                anchors.fill: parent
                visible: !win.focusMode && win.narrow
                color: "#66000000"
                z: 1
                MouseArea { anchors.fill: parent; onClicked: win.focusMode = true }
            }

            Item {
                id: readerArea
                anchors.fill: parent
                anchors.leftMargin: workspace.queueDocked && !win.rtl ? workspace.queueWidth + 1 : 0
                anchors.rightMargin: workspace.queueDocked && win.rtl ? workspace.queueWidth + 1 : 0

                WebEngineView {
                    id: readerView
                    objectName: "readerView"
                    anchors.fill: parent
                    focus: false
                    backgroundColor: theme.background
                    settings.javascriptEnabled: true
                    settings.localContentCanAccessRemoteUrls: true
                    settings.localContentCanAccessFileUrls: true

                    Component.onCompleted: loadHtml(backend.readerShell(), win.readerShellUrl)

                    onLoadingChanged: function(request) {
                        if (request.status === WebEngineView.LoadSucceededStatus) {
                            win.readerReady = true
                            win.applyReaderTheme()
                            paintTimer.restart()
                            if (win.pendingRender)
                                win.renderArticle(win.pendingRender)
                        } else if (request.status === WebEngineView.LoadFailedStatus) {
                            win.readerReady = false
                            if (win.pendingRender)
                                backend.sentencesReady(win.pendingRender.token, [])
                            win.pendingRender = null
                        }
                    }

                    onNavigationRequested: function(request) {
                        const target = request.url.toString()
                        if (target === win.readerShellUrl) {
                            return
                        } else if (target.indexOf("https://sift.invalid/follow/paused") === 0) {
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

                // WebEngine paints its surface black until the first frame of
                // the loaded page arrives. Hold the window colour over it so
                // opening an article does not flash black.
                Rectangle {
                    objectName: "readerCover"
                    anchors.fill: parent
                    visible: backend.hasArticle && !win.readerPainted
                    color: theme.background
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
                            text: backend.queue.length > 0
                                ? qsTr("Choose a page from your queue, or add another.")
                                : qsTr("Add a page to your queue. Space starts reading aloud.")
                            color: theme.muted
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.topMargin: 10
                            spacing: 8
                            FlatButton {
                                visible: backend.queue.length > 0
                                text: qsTr("Open queue")
                                hint: qsTr("Show the queue (F)")
                                onClicked: win.focusMode = false
                            }
                            FlatButton {
                                text: qsTr("Add a page")
                                primary: true
                                enabled: !backend.busy
                                onClicked: addDialog.open()
                            }
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

            Rectangle {
                id: queueDivider
                visible: workspace.queueDocked
                width: 1
                color: theme.border
                z: 2
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: win.rtl ? undefined : parent.left
                anchors.right: win.rtl ? parent.right : undefined
                anchors.leftMargin: win.rtl ? 0 : workspace.queueWidth
                anchors.rightMargin: win.rtl ? workspace.queueWidth : 0
            }

            SidebarPanel {
                id: sidebar
                visible: !win.focusMode
                width: workspace.queueWidth
                z: 2
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: win.rtl ? undefined : parent.left
                anchors.right: win.rtl ? parent.right : undefined
                onAddPage: addDialog.open()
            }
        }

        PlayerBar {
            id: playerBar
            Layout.fillWidth: true
            visible: backend.hasArticle
            onOpenVoice: voiceDialog.open()
        }
    }

    // LoadSucceeded precedes the first painted frame by a moment. Keep the
    // window-coloured cover up a little longer so the black surface never
    // shows between the two. Only the shell loads, so this happens once.
    Timer {
        id: paintTimer
        interval: 300
        repeat: false
        onTriggered: win.readerPainted = true
    }

    // A window returning from another workspace may still be waiting for its
    // first frame, so ask again shortly after as well.
    Timer {
        id: repaintTimer
        property int remaining: 0
        interval: 120
        repeat: true
        onTriggered: {
            if (win.readerReady)
                readerView.runJavaScript("siftRepaint();")
            if (--remaining <= 0)
                stop()
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
        function onReaderNeedsRepaint() {
            if (win.readerReady)
                readerView.runJavaScript("siftRepaint();")
            repaintTimer.remaining = 3
            repaintTimer.restart()
        }
        function onReaderRender(article) {
            win.renderArticle(article)
        }
        function onHighlightSentence(index) {
            readerView.runJavaScript("siftSetCurrent(" + index + ");")
            readerView.runJavaScript("siftScrollToCurrent(" + index + ", true);")
        }
        function onToast(message) {
            win.showToast(message)
        }
        function onFontSizeChanged() {
            if (win.readerReady)
                readerView.runJavaScript("siftSetFontSize(" + backend.fontSize + ");")
        }
        function onReaderFontChanged() {
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
