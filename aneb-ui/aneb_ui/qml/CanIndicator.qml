// CanIndicator.qml — small pill showing the chip's CAN state.
import QtQuick 2.15

Rectangle {
    id: root
    property string chip: ""
    property int tec: 0
    property int rec: 0
    property string state: "active"

    implicitWidth:  220
    implicitHeight: 28
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

    Row {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 8

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 10; height: 10; radius: 5
            color: {
                if (root.state === "bus-off") return "#ff5555"
                if (root.state === "passive") return "#ffcc44"
                return "#22cc44"
            }
            Rectangle {  // halo
                anchors.centerIn: parent
                width: 18; height: 18; radius: 9
                color: parent.color
                opacity: 0.4
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "CAN  " + root.state
            color: {
                if (root.state === "bus-off") return "#ffb0b0"
                if (root.state === "passive") return "#ffe089"
                return "#cdfac0"
            }
            font.family: "Consolas"
            font.pixelSize: 11
            font.bold: true
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
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
