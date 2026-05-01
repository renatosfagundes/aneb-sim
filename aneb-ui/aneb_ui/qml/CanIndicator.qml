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
    implicitHeight: 38
    radius: 4
    border.width: 1

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

    Column {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 6
        anchors.topMargin: 3
        anchors.bottomMargin: 3
        spacing: 1

        Row {
            spacing: 6
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
                font.pixelSize: 11
                font.bold: true
            }
        }
        Text {
            // TEC = Transmit Error Counter, REC = Receive Error Counter.
            // Both 0 in active state; non-zero values indicate the chip
            // is heading toward error-passive (≥128) or bus-off (TEC≥256).
            text: "TEC=" + root.tec + " REC=" + root.rec
            color: "#a8d0b0"
            font.family: "Consolas"
            font.pixelSize: 10
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
