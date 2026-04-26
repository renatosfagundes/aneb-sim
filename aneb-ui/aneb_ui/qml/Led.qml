// Led.qml — round LED with glow halo, brightness 0..1.
import QtQuick 2.15

Item {
    id: root

    property color onColor: "#ffd24a"
    property real  brightness: 0.0      // 0.0..1.0
    property string label: ""

    implicitWidth: 24
    implicitHeight: 24

    // Outer halo, visible when on.
    Rectangle {
        anchors.centerIn: parent
        width:  parent.width  * 2.0
        height: parent.height * 2.0
        radius: width / 2
        color: root.onColor
        opacity: root.brightness * 0.45
        visible: root.brightness > 0.05
    }

    // LED body.
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        border.color: "#0a0a0a"
        border.width: 1
        color: {
            // Interpolate between off-dark and onColor proportionally.
            var b = root.brightness
            var rOn = root.onColor.r * 255
            var gOn = root.onColor.g * 255
            var bOn = root.onColor.b * 255
            return Qt.rgba(
                (0x14/255 + (rOn/255 - 0x14/255) * b),
                (0x14/255 + (gOn/255 - 0x14/255) * b),
                (0x14/255 + (bOn/255 - 0x14/255) * b),
                1
            )
        }
    }

    // Specular highlight when on.
    Rectangle {
        anchors {
            top: parent.top; left: parent.left
            topMargin: parent.height * 0.18
            leftMargin: parent.width * 0.18
        }
        width: parent.width  * 0.32
        height: parent.height * 0.32
        radius: width / 2
        color: "white"
        opacity: root.brightness * 0.6
        visible: root.brightness > 0.05
    }

    Behavior on brightness { NumberAnimation { duration: 80 } }
}
