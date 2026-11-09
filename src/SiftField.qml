import QtQuick
import QtQuick.Controls

TextField {
    id: field
    implicitHeight: 40
    leftPadding: 12
    rightPadding: 12
    color: theme.foreground
    placeholderTextColor: theme.muted
    selectionColor: theme.selection
    selectedTextColor: theme.foreground
    selectByMouse: true
    background: Rectangle {
        color: theme.panel
        border.color: field.activeFocus ? theme.accent : theme.border
        radius: Math.min(4, theme.radius)
    }
}
