import QtQuick
import QtQuick.Controls

// A themed drop-down menu. It opens under the control that owns it, aligned
// to that control's trailing edge, and stays inside the window.
Menu {
    id: menu
    property bool alignLeading: false
    // A submenu is placed beside its item by Menu itself.
    property bool submenu: false
    readonly property bool rtl: Qt.application.layoutDirection === Qt.RightToLeft

    onAboutToShow: {
        if (submenu || !parent)
            return
        x = (alignLeading !== rtl) ? 0 : parent.width - width
        y = parent.height + 4
    }
    margins: 8
    padding: 4
    implicitWidth: Math.max(200, contentWidth + leftPadding + rightPadding)
    delegate: SiftMenuItem { }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 120; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 80 }
    }

    background: Rectangle {
        implicitWidth: 200
        color: theme.panel
        border.color: theme.border
        radius: Math.min(8, theme.radius)

        // A soft drop shadow that does not depend on a graphical effects module.
        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.topMargin: 3
            anchors.bottomMargin: -6
            anchors.leftMargin: -2
            anchors.rightMargin: -2
            radius: parent.radius + 2
            color: "#000000"
            opacity: theme.dark ? 0.35 : 0.12
        }
    }
}
