import QtQuick
import QtQuick.Controls

Dialog {
    id: dialog
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 24
    width: Math.min(520, parent ? parent.width - 32 : 520)
    closePolicy: Popup.CloseOnEscape
    background: Rectangle {
        color: theme.background
        border.color: theme.border
        radius: Math.min(6, theme.radius)
    }
    Overlay.modal: Rectangle { color: "#99000000" }
    header: Label {
        text: dialog.title
        color: theme.foreground
        font.pixelSize: 20
        font.weight: Font.DemiBold
        leftPadding: 24
        rightPadding: 24
        topPadding: 24
        bottomPadding: 8
        elide: Text.ElideRight
    }
}
