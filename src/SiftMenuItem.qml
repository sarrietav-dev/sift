import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

MenuItem {
    id: item
    // Shown at the trailing edge, so menus also teach the shortcuts.
    property string shortcut: ""
    property bool destructive: false

    implicitHeight: 32
    implicitWidth: Math.max(200, row.implicitWidth + leftPadding + rightPadding)
    leftPadding: 8
    rightPadding: 10
    font.pixelSize: 13

    indicator: Item { }
    arrow: SiftIcon {
        x: item.mirrored ? item.leftPadding : item.width - width - item.rightPadding
        y: (item.height - height) / 2
        visible: item.subMenu
        size: 12
        path: item.mirrored ? "M15 6l-6 6 6 6" : "M9 6l6 6-6 6"
        color: theme.muted
    }

    contentItem: RowLayout {
        id: row
        spacing: 8
        SiftIcon {
            Layout.preferredWidth: 14
            visible: item.checkable
            opacity: item.checked ? 1 : 0
            size: 14
            path: "M5 12.5l4.5 4.5L19 7.5"
            color: theme.accent
        }
        Text {
            Layout.fillWidth: true
            text: item.text
            font: item.font
            color: !item.enabled ? theme.muted : item.destructive ? theme.red : theme.foreground
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            visible: item.shortcut !== ""
            text: item.shortcut
            font.family: theme.monoFont
            font.pixelSize: 11
            color: theme.muted
            verticalAlignment: Text.AlignVCenter
        }
        Item { visible: item.subMenu; Layout.preferredWidth: 12 }
    }

    background: Rectangle {
        radius: Math.min(5, theme.radius)
        color: item.highlighted && item.enabled
            ? (item.destructive ? theme.withAlpha(theme.red, 0.16) : theme.withAlpha(theme.accent, 0.16))
            : "transparent"
    }
}
