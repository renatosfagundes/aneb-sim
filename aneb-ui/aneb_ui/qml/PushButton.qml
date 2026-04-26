// PushButton.qml — colored tactile button drawn entirely in QML
// primitives, in the style of a TV-remote keypad.
//
// Outer collar is a dark metallic frame; the inner cap is tinted to
// `color` and flips its bevel gradient when pressed (or while latched).
import QtQuick 2.15

Item {
    id: root

    property string chip:  ""
    property string pin:   ""
    property string label: ""
    property color  color: "#cccccc"   // remote-flasher style cap color
    property bool   latching: false
    property bool   _down:    false
    property bool   _latched: false

    readonly property bool _visiblyDown: _down || (latching && _latched)

    implicitWidth:  44
    implicitHeight: 60

    // ---- Collar (static metallic frame) ---------------------------
    Rectangle {
        id: collar
        width: parent.width
        height: parent.width
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        radius: width / 2

        gradient: Gradient {
            GradientStop { position: 0.0; color: "#3a4248" }
            GradientStop { position: 1.0; color: "#15191c" }
        }
        border.color: "#0a0c0e"; border.width: 1

        // Inner cap — tinted to `color`. The gradient reverses when
        // the button is held, giving the cap a "pressed" inset look.
        Rectangle {
            id: cap
            anchors.fill: parent
            anchors.margins: 4
            radius: width / 2
            border.color: Qt.rgba(0, 0, 0, 0.6)
            border.width: 1
            gradient: Gradient {
                GradientStop {
                    position: 0.0
                    color: root._visiblyDown
                        ? Qt.rgba(root.color.r * 0.45, root.color.g * 0.45, root.color.b * 0.45, 1)
                        : Qt.rgba(Math.min(1, root.color.r * 1.25),
                                  Math.min(1, root.color.g * 1.25),
                                  Math.min(1, root.color.b * 1.25), 1)
                }
                GradientStop {
                    position: 1.0
                    color: root._visiblyDown
                        ? Qt.rgba(root.color.r * 0.30, root.color.g * 0.30, root.color.b * 0.30, 1)
                        : Qt.rgba(root.color.r * 0.65, root.color.g * 0.65, root.color.b * 0.65, 1)
                }
            }
            Behavior on color { ColorAnimation { duration: 60 } }

            // Soft inner highlight — sells the cap as a domed
            // pressable surface rather than a flat coloured disc.
            Rectangle {
                x: parent.width  * 0.18
                y: parent.height * 0.15
                width:  parent.width  * 0.40
                height: parent.height * 0.28
                radius: Math.min(width, height) / 2
                color: "white"
                opacity: root._visiblyDown ? 0.10 : 0.30
            }
        }
    }

    // ---- Label ---------------------------------------------------
    Text {
        anchors.top: collar.bottom
        anchors.topMargin: 2
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.label || root.pin
        color: "#cdfac0"
        font.family: "Consolas"
        font.pixelSize: 8
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
    }

    // ---- Input ---------------------------------------------------
    MouseArea {
        anchors.fill: collar
        cursorShape: Qt.PointingHandCursor

        onPressed: {
            root._down = true
            if (root.latching) {
                root._latched = !root._latched
                if (typeof bridge !== "undefined" && bridge) {
                    bridge.setDin(root.chip, root.pin, root._latched ? 1 : 0)
                }
            } else if (typeof bridge !== "undefined" && bridge) {
                bridge.setDin(root.chip, root.pin, 1)
            }
        }
        onReleased: {
            root._down = false
            if (!root.latching && typeof bridge !== "undefined" && bridge) {
                bridge.setDin(root.chip, root.pin, 0)
            }
        }
    }
}
