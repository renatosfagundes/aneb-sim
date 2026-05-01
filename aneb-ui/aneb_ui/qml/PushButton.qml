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
    // Multiplied into the label font size so the parent panel can scale
    // text alongside the button's geometric size.
    property real   fontScale: 1.0

    readonly property bool _visiblyDown: _down || (latching && _latched)

    implicitWidth:  44
    implicitHeight: 60

    // ---- Collar (static metallic frame) ---------------------------
    // Collar is a circle whose diameter is bounded by both the
    // container's width AND ~65% of its height — so when the parent
    // layout shrinks the button vertically (e.g. four buttons stacked
    // in a column that's tighter than 4 × preferredHeight), the collar
    // shrinks too and the bottom 35% remains available for the
    // 2-line label.  Without this clamp the collar stayed at
    // `parent.width` and the label was crushed against the next button.
    Rectangle {
        id: collar
        // Reserve ~45% of the container's height for the 2-line label.
        readonly property real _diameter: Math.min(parent.width, parent.height * 0.55)
        width:  _diameter
        height: _diameter
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
    // Width is clamped to the button's width and WordWrap lets a
    // multi-token label like "DIN1  A4" wrap to two lines instead of
    // bleeding past the button into the adjacent column's label.
    Text {
        anchors.top: collar.bottom
        anchors.topMargin: 2
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width
        text: root.label || root.pin
        color: "#cdfac0"
        font.family: "Consolas"
        font.pixelSize: 9 * root.fontScale
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
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
