import QtQuick
import QtQuick.Controls

Button {
    id: control
    property string hint: ""
    property bool primary: false
    property string iconPath: ""
    property bool iconFilled: false
    property real iconSize: 16
    implicitHeight: 34
    implicitWidth: Math.max(34, contentRow.implicitWidth + 20)
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: hint !== "" ? hint : text
    ToolTip.visible: hovered && hint !== ""
    ToolTip.text: hint
    // The control positions its contentItem inside its padding. Centre the
    // icon and label inside that area rather than positioning them at its
    // left edge, which is what happens when the content item is a Row itself.
    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight
        Row {
            id: contentRow
            anchors.centerIn: parent
            spacing: control.text !== "" && control.iconPath !== "" ? 7 : 0
            SiftIcon {
                visible: control.iconPath !== ""
                path: control.iconPath
                filled: control.iconFilled
                size: control.iconSize
                color: control.enabled ? (control.primary ? theme.accentForeground : theme.foreground) : theme.muted
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                id: label
                visible: control.text !== ""
                text: control.text
                font: control.font
                color: control.enabled ? (control.primary ? theme.accentForeground : theme.foreground) : theme.muted
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
    background: Rectangle {
        radius: Math.min(theme.radius, 4)
        color: !control.enabled ? "transparent" : control.primary ? theme.accent : control.down || control.hovered ? theme.panel : "transparent"
        border.width: control.activeFocus ? 1 : 0
        border.color: theme.accent
    }
}
