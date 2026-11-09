import QtQuick
import QtQuick.Controls

Button {
    id: control
    property string hint: ""
    implicitHeight: 34
    implicitWidth: Math.max(34, label.implicitWidth + 20)
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: hint !== "" ? hint : text
    ToolTip.visible: hovered && hint !== ""
    ToolTip.text: hint
    contentItem: Text {
        id: label
        text: control.text
        font: control.font
        color: control.enabled ? theme.foreground : theme.muted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: Math.min(theme.radius, 4)
        color: control.down || control.hovered ? theme.panel : "transparent"
        border.width: control.activeFocus ? 1 : 0
        border.color: theme.accent
    }
}
