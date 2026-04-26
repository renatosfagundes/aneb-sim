// EcuPanel.qml — composes one ECU's worth of widgets, using
// QtQuick.Layouts so children shrink gracefully when the window
// resizes and never overflow the panel.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    property string chip:  ""
    property string label: ""

    implicitWidth:  720
    implicitHeight: 460

    Rectangle {
        anchors.fill: parent
        color: "#0d2418"
        border.color: "#3e6b4d"
        border.width: 1
        radius: 6
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        // ---- Title row: label + breakout LEDs + CAN status ---------
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            spacing: 10

            Text {
                text: root.label
                color: "#cdfac0"
                font.family: "Consolas"
                font.pixelSize: 14
                font.bold: true
                Layout.preferredWidth: 60
            }
            // LEDs
            RowLayout {
                spacing: 4
                Layout.fillWidth: false
                Led {
                    Layout.preferredWidth: 14; Layout.preferredHeight: 14
                    onColor: "#22cc44"
                    brightness: nano.level("PD3")
                                + (nano.duty("PD3") * (1 - nano.level("PD3")))
                }
                Led {
                    Layout.preferredWidth: 14; Layout.preferredHeight: 14
                    onColor: "#22cc44"; brightness: nano.level("PD4")
                }
                Led {
                    Layout.preferredWidth: 14; Layout.preferredHeight: 14
                    onColor: "#ffaa22"
                    brightness: nano.level("PB5")
                                + (nano.duty("PD6") * (1 - nano.level("PB5")))
                }
                Led {
                    Layout.preferredWidth: 14; Layout.preferredHeight: 14
                    onColor: "#ff4444"; brightness: nano.level("PD7")
                }
            }

            CanIndicator {
                chip: root.chip
                Layout.preferredWidth: 200
                Layout.preferredHeight: 22
            }
            Item { Layout.fillWidth: true }   // right spacer
        }

        // ---- Nano illustration -------------------------------------
        // Aspect-locked, capped so it doesn't dominate when the window
        // is tall but narrow.
        ArduinoNano {
            id: nano
            chip: root.chip
            power: bridge && bridge.engineRunning
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(width * (160.0 / 420.0), 140)
            Layout.minimumHeight: 70
        }

        // Bridge UART events to the Nano's TX/RX flash LEDs.
        Connections {
            target: bridge
            function onUartAppended(chip, data) {
                if (chip === root.chip) nano.pulseTx()
            }
            function onUartSent(chip) {
                if (chip === root.chip) nano.pulseRx()
            }
        }

        // ---- Pots row ----------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 78
            Layout.alignment: Qt.AlignHCenter
            spacing: 6
            Item { Layout.fillWidth: true }
            TrimPot { chip: root.chip; channel: 0; label: "AIN0  A0"
                      Layout.preferredWidth: 50; Layout.preferredHeight: 78 }
            TrimPot { chip: root.chip; channel: 1; label: "AIN1  A1"
                      Layout.preferredWidth: 50; Layout.preferredHeight: 78 }
            TrimPot { chip: root.chip; channel: 2; label: "AIN2  A2"
                      Layout.preferredWidth: 50; Layout.preferredHeight: 78 }
            TrimPot { chip: root.chip; channel: 3; label: "AIN3  A3"
                      Layout.preferredWidth: 50; Layout.preferredHeight: 78 }
            Item { Layout.fillWidth: true }
        }

        // ---- Buttons row -------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            Layout.alignment: Qt.AlignHCenter
            spacing: 6
            Item { Layout.fillWidth: true }
            PushButton { chip: root.chip; pin: "A4"; label: "DIN1  A4"
                         Layout.preferredWidth: 38; Layout.preferredHeight: 54 }
            PushButton { chip: root.chip; pin: "A5"; label: "DIN2  A5"
                         Layout.preferredWidth: 38; Layout.preferredHeight: 54 }
            PushButton { chip: root.chip; pin: "D9"; label: "DIN3  D9"
                         Layout.preferredWidth: 38; Layout.preferredHeight: 54 }
            PushButton { chip: root.chip; pin: "D8"; label: "DIN4  D8"
                         Layout.preferredWidth: 38; Layout.preferredHeight: 54 }
            Item { Layout.fillWidth: true }
        }

        // ---- Serial console — fills remaining space ----------------
        SerialConsole {
            chip: root.chip
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 50
        }
    }
}
