import QtQuick
import QtQuick.Shapes

// Stroked 24×24 icons, as Signed and Monologue draw theirs, so controls do not
// depend on whichever font happens to supply a glyph.
Item {
    id: icon
    property string path: ""
    property color color: theme.foreground
    property bool filled: false
    property real size: 16
    implicitWidth: size
    implicitHeight: size

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            scale: Qt.size(icon.width / 24, icon.height / 24)
            strokeColor: icon.filled ? "transparent" : icon.color
            strokeWidth: icon.filled ? 0 : 1.8
            fillColor: icon.filled ? icon.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.path }
        }
    }
}
