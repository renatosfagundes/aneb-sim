// CanIndicator.qml — small pill showing the chip's CAN state.
//
// Two-line layout so it fits in a narrow info-sidebar column without
// the TEC/REC counters spilling past the rectangle's right edge:
//
//   ● CAN
//   TEC=0 REC=0
//
// The dot color (green / yellow / red) conveys error-active /
// error-passive / bus-off states; the TEC and REC numbers are the
// raw error counters.
import QtQuick 2.15

Rectangle {
    id: root
    property string chip: ""
    property int tec: 0
    property int rec: 0
    property string state: "active"

    implicitWidth:  120
    implicitHeight: 44
    radius: 4
    border.width: 1

    // Scale text proportionally to the rectangle's height so the pill
    // still reads when the parent panel shrinks the indicator down to
    // ~34 px.  Caps at the design size so it doesn't blow up on a huge
    // panel, and floors at 9 px so it stays legible at the limit.
    readonly property real _h: Math.max(28, Math.min(implicitHeight, height))
    readonly property real _fHeader: Math.max(9, Math.min(13, _h * 0.30))
    readonly property real _fLine:   Math.max(8, Math.min(12, _h * 0.26))

    color: {
        if (root.state === "bus-off") return "#552020"
        if (root.state === "passive") return "#5a4500"
        return "#1d3a26"
    }
    border.color: {
        if (root.state === "bus-off") return "#cc4444"
        if (root.state === "passive") return "#ddaa22"
        return "#3e6b4d"
    }

    // Two horizontally-centered rows, vertically centered as a group:
    //   ● CAN
    //   TEC=0 REC=0
    // Spacing of 6 px gives the two lines breathing room without making
    // the pill look hollow.
    Column {
        anchors.centerIn: parent
        spacing: 6

        Row {
            spacing: 6
            anchors.horizontalCenter: parent.horizontalCenter
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 9; height: 9; radius: 4.5
                color: {
                    if (root.state === "bus-off") return "#ff5555"
                    if (root.state === "passive") return "#ffcc44"
                    return "#22cc44"
                }
                Rectangle {  // halo
                    anchors.centerIn: parent
                    width: 16; height: 16; radius: 8
                    color: parent.color
                    opacity: 0.4
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "CAN"
                color: {
                    if (root.state === "bus-off") return "#ffb0b0"
                    if (root.state === "passive") return "#ffe089"
                    return "#cdfac0"
                }
                font.family: "Consolas"
                font.pixelSize: root._fHeader
                font.bold: true
            }
        }
        Text {
            // TEC = Transmit Error Counter, REC = Receive Error Counter.
            // Both 0 in active state; non-zero values indicate the chip
            // is heading toward error-passive (≥128) or bus-off (TEC≥256).
            anchors.horizontalCenter: parent.horizontalCenter
            text: "TEC=" + root.tec + " REC=" + root.rec
            color: "#a8d0b0"
            font.family: "Consolas"
            font.pixelSize: root._fLine
        }
    }

    // Pull values from bridge whenever it updates.
    Connections {
        target: bridge
        function onCanStateSeqChanged() {
            var s = bridge.canStateOf(root.chip)
            if (s) {
                root.tec   = s.tec   !== undefined ? s.tec   : 0
                root.rec   = s.rec   !== undefined ? s.rec   : 0
                root.state = s.state !== undefined ? s.state : "active"
            }
        }
    }
    Component.onCompleted: {
        var s = bridge.canStateOf(root.chip)
        if (s) {
            root.tec   = s.tec   !== undefined ? s.tec   : 0
            root.rec   = s.rec   !== undefined ? s.rec   : 0
            root.state = s.state !== undefined ? s.state : "active"
        }
    }
}
