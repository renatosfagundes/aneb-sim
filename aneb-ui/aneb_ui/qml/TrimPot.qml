// TrimPot.qml — circular knob with a rotating arrow indicator.
//
// Body is a static blue plastic disc; the indicator (a thin pointer
// from center toward the rim) rotates with the value. Drag vertically
// or scroll the wheel to change the value.
//
// Indicator angle (using Qt rotation: 0° = up, positive = clockwise):
//   -135° at value 0    -> arrow at SW (~7 o'clock)
//   -45°  at value 1023 -> arrow at NW (~10 o'clock)
// The arrow sweeps the LEFT side of the dial through 9 o'clock as the
// value rises, like a left-handed fuel gauge.
import QtQuick 2.15

Item {
    id: root

    property string chip:    ""
    property int    channel: 0
    property string label:   "AIN0"
    property int    adcValue: 0           // 0..1023 — internal state
    property int    minValue: 0
    property int    maxValue: 1023

    implicitWidth:  40
    implicitHeight: 60

    readonly property real _angle: -135 + (adcValue / 1023.0) * 90

    // ---- Body (blue plastic) --------------------------------------
    Rectangle {
        id: body
        width: parent.width
        height: parent.width
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        radius: width / 2

        gradient: Gradient {
            GradientStop { position: 0.0; color: "#3d8fc4" }
            GradientStop { position: 1.0; color: "#114a78" }
        }
        border.color: "#082030"; border.width: 1

        // Subtle inner darkening so the body reads as a recessed cup
        // rather than a flat sticker.
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: width / 2
            color: "transparent"
            border.color: Qt.rgba(0, 0, 0, 0.28)
            border.width: 1
        }

        // Center pivot — small dark dot that visually anchors the
        // arrow at the knob's center of rotation.
        Rectangle {
            anchors.centerIn: parent
            width:  parent.width  * 0.18
            height: parent.height * 0.18
            radius: width / 2
            color: "#0a1622"
            border.color: "#04080d"; border.width: 1
        }

        // Arrow indicator — a thin white marker that rotates with
        // the value. Wrapped in an Item with the rotation applied so
        // the rectangle's anchors stay simple.
        Item {
            anchors.centerIn: parent
            width:  parent.width
            height: parent.height
            rotation: root._angle
            transformOrigin: Item.Center
            Behavior on rotation { NumberAnimation { duration: 60 } }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: parent.height * 0.10
                width:  Math.max(2, parent.width * 0.07)
                height: parent.height * 0.36
                radius: width / 2
                color: "#fafaff"
                border.color: Qt.rgba(0, 0, 0, 0.35)
                border.width: 1
            }
        }
    }

    // ---- Labels ---------------------------------------------------
    Text {
        anchors.top: body.bottom
        anchors.topMargin: 1
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.adcValue
        color: "#cdfac0"
        font.family: "Consolas"
        font.pixelSize: 9
        font.bold: true
    }
    Text {
        anchors.top: body.bottom
        anchors.topMargin: 11
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.label
        color: "#a8d0b0"
        font.pixelSize: 7
    }

    // ---- Input ----------------------------------------------------
    MouseArea {
        anchors.fill: body
        cursorShape: Qt.SizeVerCursor
        property real startY: 0
        property int  startVal: 0

        onPressed: function(evt) {
            startY = evt.y
            startVal = root.adcValue
        }
        onPositionChanged: function(evt) {
            if (!pressed) return
            var dy = startY - evt.y
            var newV = startVal + dy * 6
            if (newV < root.minValue) newV = root.minValue
            if (newV > root.maxValue) newV = root.maxValue
            if (newV !== root.adcValue) {
                root.adcValue = newV
                if (typeof bridge !== "undefined" && bridge) {
                    bridge.setAdc(root.chip, root.channel, newV)
                }
            }
        }
        onWheel: function(evt) {
            var step = (evt.angleDelta.y > 0 ? 32 : -32)
            var newV = root.adcValue + step
            if (newV < root.minValue) newV = root.minValue
            if (newV > root.maxValue) newV = root.maxValue
            root.adcValue = newV
            if (typeof bridge !== "undefined" && bridge) {
                bridge.setAdc(root.chip, root.channel, newV)
            }
        }
    }
}
