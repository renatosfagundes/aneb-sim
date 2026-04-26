// Led.qml — domed LED with glow halo, brightness 0..1.
//
// The LED looks like a colored plastic dome whether or not it's lit
// (real LEDs have tinted clear/diffuse plastic so they read as a
// colored body even when off). The on-state adds a wide halo, a
// brightening core, and a sharper specular highlight.
import QtQuick 2.15

Item {
    id: root

    property color onColor: "#ffd24a"
    property real  brightness: 0.0      // 0.0..1.0
    property string label: ""

    implicitWidth: 24
    implicitHeight: 24

    // Outer halo, visible when on. Scales with brightness so the
    // "glow" reads from across the panel without dominating when dim.
    Rectangle {
        anchors.centerIn: parent
        width:  parent.width  * (1.6 + 0.6 * root.brightness)
        height: parent.height * (1.6 + 0.6 * root.brightness)
        radius: width / 2
        color: root.onColor
        opacity: root.brightness * 0.5
        visible: root.brightness > 0.05
    }

    // LED body — a tinted dome. Off state is the dark version of the
    // on color (so it still reads as "this is a red LED" even unlit);
    // on state interpolates toward a bright saturated core.
    Rectangle {
        id: body
        anchors.fill: parent
        radius: width / 2
        border.color: "#050505"
        border.width: 1
        gradient: Gradient {
            // Top is brighter (where light would catch a dome), bottom
            // is darker (in shadow). Both sides bias toward onColor as
            // brightness rises.
            GradientStop {
                position: 0.0
                color: Qt.rgba(
                    Math.min(1, root.onColor.r * (0.55 + 0.45 * root.brightness)),
                    Math.min(1, root.onColor.g * (0.55 + 0.45 * root.brightness)),
                    Math.min(1, root.onColor.b * (0.55 + 0.45 * root.brightness)),
                    1
                )
            }
            GradientStop {
                position: 1.0
                color: Qt.rgba(
                    root.onColor.r * (0.18 + 0.55 * root.brightness),
                    root.onColor.g * (0.18 + 0.55 * root.brightness),
                    root.onColor.b * (0.18 + 0.55 * root.brightness),
                    1
                )
            }
        }
    }

    // Inner ring shadow — gives the dome a recessed-edge feel so the
    // body reads as 3D rather than a flat sticker.
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: width / 2
        color: "transparent"
        border.color: Qt.rgba(0, 0, 0, 0.35)
        border.width: 1
    }

    // Specular highlight — a small bright crescent toward the top
    // left, suggesting an overhead light. Always faintly visible to
    // sell the dome shape; intensifies when the LED lights up.
    Rectangle {
        x: parent.width  * 0.18
        y: parent.height * 0.14
        width:  parent.width  * 0.40
        height: parent.height * 0.32
        radius: Math.min(width, height) / 2
        color: "white"
        opacity: 0.18 + 0.55 * root.brightness
    }

    Behavior on brightness { NumberAnimation { duration: 80 } }
}
