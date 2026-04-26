// PushButton.qml — two-layer composition.
//
// Layer 1 (static): button_frame.png — the metal frame / PCB pads
// surrounding the cap. Center is transparent.
// Layer 2 (state-dependent): button_cap_up.png OR button_cap_down.png
// — the cap, swapped on press.
//
// Falls back to drawn QML primitives if the asset images are missing.
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

    implicitWidth:  46
    implicitHeight: 66

    // ---- Frame (static) --------------------------------------------
    Image {
        id: frameImage
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width:  parent.width
        height: parent.width
        source: "../qml_assets/button_frame.png"
        smooth: true
        antialiasing: true
        fillMode: Image.PreserveAspectFit
    }
    // Fallback frame.
    Rectangle {
        anchors.fill: frameImage
        visible: frameImage.status !== Image.Ready
        radius: 6
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#3a4248" }
            GradientStop { position: 1.0; color: "#15191c" }
        }
        border.color: "#0a0c0e"; border.width: 1
    }

    // ---- Cap (state-dependent) -------------------------------------
    Image {
        id: capImage
        anchors.centerIn: frameImage
        width:  frameImage.width  * 0.62
        height: frameImage.height * 0.62
        source: root._visiblyDown
            ? "../qml_assets/button_cap_down.png"
            : "../qml_assets/button_cap_up.png"
        smooth: true
        antialiasing: true
        fillMode: Image.PreserveAspectFit
    }
    // Fallback cap.
    Rectangle {
        anchors.fill: capImage
        visible: capImage.status !== Image.Ready
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

    // ---- Label ------------------------------------------------------
    Text {
        anchors.top: frameImage.bottom
        anchors.topMargin: 3
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.label || root.pin
        color: "#cdfac0"
        font.family: "Consolas"
        font.pixelSize: 9
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
    }

    // ---- Input ------------------------------------------------------
    MouseArea {
        anchors.fill: frameImage
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
