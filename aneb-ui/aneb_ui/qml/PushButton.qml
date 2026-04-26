// PushButton.qml — tactile-style cap. Momentary by default; latching mode
// is supported for the MCU mode-selector buttons.
import QtQuick 2.15

Item {
    id: root

    property string chip:  ""      // e.g. "ecu1"
    property string pin:   ""      // "A4", "D8", etc.
    property string label: ""
    property bool   latching: false
    property bool   _down:    false
    property bool   _latched: false

    implicitWidth:  60
    implicitHeight: 78

    Rectangle {
        id: cap
        width: parent.width
        height: parent.width
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        radius: 8

        // Outer collar.
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#3a4248" }
            GradientStop { position: 1.0; color: "#15191c" }
        }
        border.color: "#0a0c0e"; border.width: 1

        Rectangle {
            // Inner cap — bevel flips when pressed.
            anchors.fill: parent
            anchors.margins: 5
            radius: 5
            border.color: "#000"; border.width: 1
            gradient: Gradient {
                GradientStop {
                    position: 0.0
                    color: (root._down || (root.latching && root._latched))
                        ? "#15191c" : "#454d54"
                }
                GradientStop {
                    position: 1.0
                    color: (root._down || (root.latching && root._latched))
                        ? "#0a0d10" : "#1a1f24"
                }
            }
        }
    }

    Text {
        anchors.top: cap.bottom
        anchors.topMargin: 3
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.label || root.pin
        color: "#102018"
        font.family: "Consolas"
        font.pixelSize: 9
        font.bold: true
    }

    MouseArea {
        anchors.fill: cap
        cursorShape: Qt.PointingHandCursor

        onPressed: {
            root._down = true
            if (root.latching) {
                root._latched = !root._latched
                bridge.setDin(root.chip, root.pin, root._latched ? 1 : 0)
            } else {
                bridge.setDin(root.chip, root.pin, 1)
            }
        }
        onReleased: {
            root._down = false
            if (!root.latching) {
                bridge.setDin(root.chip, root.pin, 0)
            }
        }
    }
}
