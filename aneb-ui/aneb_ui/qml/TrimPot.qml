// TrimPot.qml — circular knob with a brass center and a rotating
// arrow indicator.
//
// Body is a static blue plastic disc; on top of it sits a brass-
// colored center cap, and on top of that an arrow shape that rotates
// with the value (see _angle below). Drag vertically or scroll the
// wheel to change the value.
//
// Indicator angle.
//
// User spec, in math angles (CCW from +X axis):
//   value 0    -> 240°  (≈ 7 o'clock)
//   value 1023 -> 300°  (≈ 5 o'clock)
// with the arrow moving clockwise between them, i.e. the long way
// through the top of the dial — the classic ~270° potentiometer
// sweep. In Qt rotation (CW from up):
//   value 0    -> 210°
//   value 1023 -> 510°  (≡ 150°)
// QML interpolates 210→510 linearly so the tip travels through
// 270 (W), 360 (N), 90 (E), 150 (5 o'clock).
import QtQuick 2.15
import QtQuick.Shapes 1.15

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

    readonly property real _angle: 210 + (adcValue / 1023.0) * 300

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

        // Brass center disc — same yellow-ish cap as before.
        Rectangle {
            id: brass
            anchors.centerIn: parent
            width:  parent.width  * 0.56
            height: parent.height * 0.56
            radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#e8c878" }
                GradientStop { position: 1.0; color: "#806030" }
            }
            border.color: "#403018"; border.width: 1

            // Arrow indicator — triangular tip + thin shaft, drawn
            // with QtQuick.Shapes so the tip is a real triangle
            // instead of a clipped rectangle. Rotates around the
            // brass disc's center as the value changes.
            Shape {
                id: arrow
                anchors.fill: parent
                rotation: root._angle
                transformOrigin: Item.Center
                Behavior on rotation { NumberAnimation { duration: 60 } }

                ShapePath {
                    strokeColor: "transparent"
                    fillColor:   "#1a1a1a"

                    // Path traces an arrow pointing straight up
                    // within the brass disc, anchored at its
                    // bounding-box center for clean rotation.
                    startX: arrow.width / 2
                    startY: arrow.height * 0.08
                    PathLine { x: arrow.width / 2 + arrow.width * 0.22
                               y: arrow.height * 0.42 }
                    PathLine { x: arrow.width / 2 + arrow.width * 0.07
                               y: arrow.height * 0.42 }
                    PathLine { x: arrow.width / 2 + arrow.width * 0.07
                               y: arrow.height * 0.92 }
                    PathLine { x: arrow.width / 2 - arrow.width * 0.07
                               y: arrow.height * 0.92 }
                    PathLine { x: arrow.width / 2 - arrow.width * 0.07
                               y: arrow.height * 0.42 }
                    PathLine { x: arrow.width / 2 - arrow.width * 0.22
                               y: arrow.height * 0.42 }
                }
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
    //
    // Angular drag — click anywhere on the knob and the arrow snaps
    // to the click angle, then drag to spin it like a real knob.
    // Maps the click position to the same 240°→300° (long-way CW)
    // arc the visual indicator uses; positions inside the dead-zone
    // (between 5 o'clock and 7 o'clock) snap to whichever end is
    // closer.
    function _setFromMouse(mx, my) {
        var cx = body.width  / 2
        var cy = body.height / 2
        // atan2 in screen coords (Y down): 0=E, +90=S, -90=N, ±180=W.
        // Qt rotation is CW from N, i.e. screen-atan2 + 90.
        var qtRot = Math.atan2(my - cy, mx - cx) * 180 / Math.PI + 90
        if (qtRot < 0)   qtRot += 360
        if (qtRot >= 360) qtRot -= 360
        // Unroll relative to the value-0 anchor (210°), CW.
        var unrolled = (qtRot - 210 + 360) % 360
        var v
        if (unrolled <= 300) {
            v = Math.round(unrolled / 300 * 1023)
        } else {
            // Dead-zone (between 5 and 7 o'clock). Snap to whichever
            // end the click was nearer to.
            v = (unrolled - 300) < 30 ? 1023 : 0
        }
        if (v < root.minValue) v = root.minValue
        if (v > root.maxValue) v = root.maxValue
        if (v !== root.adcValue) {
            root.adcValue = v
            if (typeof bridge !== "undefined" && bridge) {
                bridge.setAdc(root.chip, root.channel, v)
            }
        }
    }

    MouseArea {
        anchors.fill: body
        cursorShape: Qt.PointingHandCursor

        onPressed:          function(evt) { root._setFromMouse(evt.x, evt.y) }
        onPositionChanged:  function(evt) {
            if (pressed) root._setFromMouse(evt.x, evt.y)
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
