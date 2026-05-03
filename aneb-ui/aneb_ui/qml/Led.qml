// Led.qml — round 5mm-style LED viewed head-on, brightness 0..1.
//
// Looks like a colored plastic dome: tinted body that's still visibly
// "this color" when off, prominent bright reflection near the top-left
// (the canonical "lit from above" highlight), a soft secondary
// reflection on the lower-right, and a halo glow that swells with
// brightness.
import QtQuick 2.15

Item {
    id: root

    property color onColor: "#ffd24a"
    property real  brightness: 0.0      // 0.0..1.0
    property string label: ""

    implicitWidth: 24
    implicitHeight: 24

    // Outer halo, visible when on.  Kept tight (1.2x → 1.5x with
    // brightness) so adjacent LEDs in a row don't bleed into each
    // other — the previous 1.6x → 2.2x had ~30 % overlap on dense
    // ECU rows like DOUT0/DOUT1/L/LDR.  Opacity tapered to match.
    Rectangle {
        anchors.centerIn: parent
        width:  parent.width  * (1.2 + 0.3 * root.brightness)
        height: parent.height * (1.2 + 0.3 * root.brightness)
        radius: width / 2
        color: root.onColor
        opacity: root.brightness * 0.45
        visible: root.brightness > 0.05
    }

    // LED dome body — three-stop vertical gradient suggests a
    // top-lit sphere. Both ends bias toward the on-color as
    // brightness rises so the unlit dome still reads as "this is
    // a [red/green/blue] LED" rather than a neutral grey blob.
    Rectangle {
        id: body
        anchors.fill: parent
        radius: width / 2
        border.color: "#050505"
        border.width: 1
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: Qt.rgba(
                    Math.min(1, root.onColor.r * (0.65 + 0.35 * root.brightness)),
                    Math.min(1, root.onColor.g * (0.65 + 0.35 * root.brightness)),
                    Math.min(1, root.onColor.b * (0.65 + 0.35 * root.brightness)),
                    1
                )
            }
            GradientStop {
                position: 0.55
                color: Qt.rgba(
                    root.onColor.r * (0.40 + 0.55 * root.brightness),
                    root.onColor.g * (0.40 + 0.55 * root.brightness),
                    root.onColor.b * (0.40 + 0.55 * root.brightness),
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

    // Inner ring shadow — gives the dome a recessed-edge feel.
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: width / 2
        color: "transparent"
        border.color: Qt.rgba(0, 0, 0, 0.30)
        border.width: 1
    }

    // Primary specular reflection — bright crescent toward the
    // top-left, suggesting an overhead light. Always faintly
    // visible to sell the dome shape; intensifies when lit.
    Rectangle {
        x: parent.width  * 0.16
        y: parent.height * 0.12
        width:  parent.width  * 0.42
        height: parent.height * 0.30
        radius: Math.min(width, height) / 2
        color: "white"
        opacity: 0.40 + 0.50 * root.brightness
    }

    // Secondary rim reflection — small bright spot lower-right
    // gives the dome a curved feel by hinting at light wrapping
    // around the back edge.
    Rectangle {
        x: parent.width  * 0.62
        y: parent.height * 0.62
        width:  parent.width  * 0.18
        height: parent.height * 0.14
        radius: Math.min(width, height) / 2
        color: "white"
        opacity: 0.18 + 0.25 * root.brightness
    }

}
