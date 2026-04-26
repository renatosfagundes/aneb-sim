// Board.qml — main view.
//
// Background: the rendered cyan PCB texture. Foreground: a 2x2 grid of
// EcuPanels mirroring the physical board (ECU1 top-left, ECU2 top-right,
// ECU4 bottom-left, ECU3 bottom-right) and a column on the right with
// the MCU panel + CAN inject + CAN monitor.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    width:  1700
    height: 1000

    // PCB background — tinted Image with a fallback solid color so a
    // missing asset doesn't break the layout.
    Rectangle {
        anchors.fill: parent
        color: "#9fbfd0"
    }
    Image {
        anchors.fill: parent
        source: "../qml_assets/background.png"
        fillMode: Image.PreserveAspectCrop
        opacity: 0.55
        smooth: true
    }

    // Title strip.
    Rectangle {
        id: title
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 44
        color: "#0d2418"
        opacity: 0.92
        border.color: "#3e6b4d"
        Row {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 14
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Automotive Network Evaluation Board v1.1 — simulator"
                color: "#cdfac0"
                font.family: "Consolas"
                font.pixelSize: 16
                font.bold: true
            }
            Item { width: 30; height: 1 }   // spacer
            Repeater {
                model: ["ecu1", "ecu2", "ecu3", "ecu4", "mcu"]
                Button {
                    text: "Load " + modelData.toUpperCase()
                    onClicked: { if (bridge) bridge.openLoadDialog(modelData) }
                }
            }
            Button { text: "Pause";  onClicked: { if (bridge) bridge.pauseEngine() } }
            Button { text: "Resume"; onClicked: { if (bridge) bridge.resumeEngine() } }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Engine: " + (bridge && bridge.engineRunning ? "running" : "stopped")
                color: (bridge && bridge.engineRunning) ? "#22cc44" : "#ddaa22"
                font.family: "Consolas"
                font.pixelSize: 12
            }
        }
    }

    // Main work area below the title.
    Item {
        id: work
        anchors.top: title.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12

        // Right column: MCU + CAN inject + CAN monitor.
        Column {
            id: rightCol
            width: 460
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            spacing: 8

            Mcu {
                width: parent.width
                height: 360
            }
            CanInject {
                width: parent.width
            }
            CanMonitor {
                width: parent.width
                height: parent.height - y - 4
            }
        }

        // Left side: 2x2 grid of ECU panels.
        Grid {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.right: rightCol.left
            anchors.bottom: parent.bottom
            anchors.rightMargin: 12
            columns: 2
            spacing: 10

            EcuPanel { chip: "ecu1"; label: "ECU 1"; width: (parent.width - 10) / 2; height: (parent.height - 10) / 2 }
            EcuPanel { chip: "ecu2"; label: "ECU 2"; width: (parent.width - 10) / 2; height: (parent.height - 10) / 2 }
            EcuPanel { chip: "ecu4"; label: "ECU 4"; width: (parent.width - 10) / 2; height: (parent.height - 10) / 2 }
            EcuPanel { chip: "ecu3"; label: "ECU 3"; width: (parent.width - 10) / 2; height: (parent.height - 10) / 2 }
        }
    }
}
