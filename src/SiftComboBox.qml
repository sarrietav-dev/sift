import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    implicitHeight: 40
    leftPadding: 12
    rightPadding: 36
    font.pixelSize: 13
    Accessible.name: hint

    property string hint: ""

    contentItem: Text {
        leftPadding: control.leftPadding
        rightPadding: control.rightPadding
        text: control.displayText
        font: control.font
        color: control.enabled ? theme.foreground : theme.muted
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: SiftIcon {
        x: control.width - width - 12
        y: Math.round((control.height - height) / 2)
        size: 14
        path: "M6 9l6 6 6-6"
        color: theme.muted
    }
    background: Rectangle {
        color: theme.panel
        border.color: control.activeFocus ? theme.accent : theme.border
        radius: Math.min(4, theme.radius)
    }
    delegate: ItemDelegate {
        required property int index
        required property string modelData
        width: control.width - 8
        height: 34
        highlighted: control.highlightedIndex === index
        background: Rectangle {
            color: parent.highlighted || parent.hovered ? theme.panel : "transparent"
            radius: Math.min(3, theme.radius)
        }
        contentItem: Text {
            text: parent.text
            font.pixelSize: 13
            color: theme.foreground
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    popup: Popup {
        y: control.height
        width: control.width
        implicitHeight: Math.min(360, contentItem.implicitHeight + topPadding + bottomPadding)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator { }
        }
        background: Rectangle {
            color: theme.background
            border.color: theme.border
            radius: Math.min(4, theme.radius)
        }
    }
}
