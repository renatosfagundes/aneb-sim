// PushButton.qml — tactile cap drawn entirely in QML primitives.
//
// Outer collar uses a static gradient; the inner cap flips its bevel
// gradient on press (or while latched). No asset images required.
import QtQuick 2.15

Item {
    id: root

    property string chip:  ""
    property string pin:   ""
    property string label: ""
    property bool   latching: false
    property bool   _down:    false
    property bool   _latched: false

    readonly property bool _visiblyDown: _down || (latching && _latched)

    implicitWidth:  38
    implicitHeight: 54

    // ---- Collar (static metallic frame) ---------------------------
    Rectangle {
        id: collar
        width: parent.width
        height: parent.width
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        radius: 6

        gradient: Gradient {
            GradientStop { position: 0.0; color: "#3a4248" }
            GradientStop { position: 1.0; color: "#15191c" }
        }
        border.color: "#0a0c0e"; border.width: 1

        // Inner cap — bevel flips when pressed.
        Rectangle {
            id: cap
            anchors.fill: parent
            anchors.margins: 4
            radius: 4
            border.color: "#000"; border.width: 1
            gradient: Gradient {
                GradientStop {
                    position: 0.0
                    color: root._visiblyDown ? "#15191c" : "#454d54"
                }
                GradientStop {
                    position: 1.0
                    color: root._visiblyDown ? "#0a0d10" : "#1a1f24"
                }
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
