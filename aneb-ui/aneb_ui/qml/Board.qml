// Board.qml — main view. Layouts-based so the four ECUs and the right
// column flex when the window resizes.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    // No explicit width/height — QQuickWidget sets the size via
    // SizeRootObjectToView so the QML respects the host window.

    Rectangle { anchors.fill: parent; color: "#9fbfd0" }

    // Top-level dashboard window — hidden until the toolbar toggles it.
    DashboardWindow { id: dashboardWindow }
    Image {
        anchors.fill: parent
        source: "../qml_assets/background.png"
        fillMode: Image.PreserveAspectCrop
        opacity: 0.55
        smooth: true
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        // ---- Title strip / toolbar --------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            color: "#0d2418"
            opacity: 0.92
            border.color: "#3e6b4d"

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                anchors.topMargin: 4
                anchors.bottomMargin: 4
                spacing: 2

                // ---- Row 1: title + load buttons + pause/resume + status
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: "ANEB v1.1 — simulator"
                        color: "#cdfac0"
                        font.family: "Consolas"
                        font.pixelSize: 14
                        font.bold: true
                    }
                    Item { Layout.preferredWidth: 6 }
                    Repeater {
                        model: ["ecu1", "ecu2", "ecu3", "ecu4", "mcu"]
                        Button {
                            text: "Load " + modelData.toUpperCase()
                            onClicked: { if (bridge) bridge.openLoadDialog(modelData) }
                        }
                    }
                    Button { text: "Pause";  onClicked: { if (bridge) bridge.pauseEngine() } }
                    Button { text: "Resume"; onClicked: { if (bridge) bridge.resumeEngine() } }
                    Button {
                        text: dashboardWindow.visible ? "Dashboard ▾" : "Dashboard ▸"
                        onClicked: dashboardWindow.visible = !dashboardWindow.visible
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: "Engine: " + (bridge && bridge.engineRunning ? "running" : "stopped")
                        color: (bridge && bridge.engineRunning) ? "#22cc44" : "#ddaa22"
                        font.family: "Consolas"
                        font.pixelSize: 11
                    }
                }

                // ---- Row 2: flash-all + speed control
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        text: "Flash All ECUs"
                        onClicked: { if (bridge) bridge.openLoadAllDialog() }
                    }

                    Item { Layout.preferredWidth: 12 }

                    Text {
                        text: "Speed:"
                        color: "#cdfac0"
                        font.family: "Consolas"
                        font.pixelSize: 12
                        verticalAlignment: Text.AlignVCenter
                    }
                    TextField {
                        id: speedField
                        implicitWidth: 68
                        implicitHeight: 26
                        text: bridge ? bridge.speed.toFixed(2) : "0.00"
                        color: "#cdfac0"
                        font.family: "Consolas"
                        font.pixelSize: 12
                        background: Rectangle {
                            color: "#1a3a28"; radius: 3
                            border.color: "#3e6b4d"; border.width: 1
                        }
                        Connections {
                            target: bridge
                            function onSpeedChanged() {
                                if (!speedField.activeFocus)
                                    speedField.text = bridge.speed.toFixed(2)
                            }
                        }
                        onEditingFinished: {
                            var v = parseFloat(text)
                            if (!isNaN(v) && v >= 0.0 && bridge) bridge.setSpeed(v)
                            else text = bridge ? bridge.speed.toFixed(2) : "0.00"
                        }
                    }
                    Text {
                        text: "(0 = max)"
                        color: "#7aaa8a"
                        font.family: "Consolas"
                        font.pixelSize: 10
                        verticalAlignment: Text.AlignVCenter
                    }
                    Button {
                        text: "1× RT"
                        onClicked: { if (bridge) bridge.setSpeed(1.0) }
                    }
                    Button {
                        text: "Max"
                        onClicked: { if (bridge) bridge.setSpeed(0.0) }
                    }

                    Item { Layout.fillWidth: true }
                }
            }
        }

        // ---- Main work area: ECU grid (left) + right column -------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // Left: 2x2 ECU grid.
            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 8
                columns: 2
                columnSpacing: 8
                rowSpacing: 8

                EcuPanel { chip: "ecu1"; label: "ECU 1"
                           Layout.fillWidth: true; Layout.fillHeight: true }
                EcuPanel { chip: "ecu2"; label: "ECU 2"
                           Layout.fillWidth: true; Layout.fillHeight: true }
                EcuPanel { chip: "ecu4"; label: "ECU 4"
                           Layout.fillWidth: true; Layout.fillHeight: true }
                EcuPanel { chip: "ecu3"; label: "ECU 3"
                           Layout.fillWidth: true; Layout.fillHeight: true }
            }

            // Right: MCU + CAN inject + CAN monitor.
            ColumnLayout {
                Layout.preferredWidth: 460
                Layout.minimumWidth: 380
                Layout.fillHeight: true
                Layout.margins: 8
                spacing: 8

                Mcu {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 240
                    Layout.minimumHeight: 200
                }
                CanInject {
                    Layout.fillWidth: true
                }
                CanMonitor {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 120
                }
            }
        }
    }
}
