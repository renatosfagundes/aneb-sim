// EcuPanel.qml — composes one ECU's worth of widgets, using
// QtQuick.Layouts so children shrink gracefully when the window
// resizes and never overflow the panel.
//
// Layout from top to bottom:
//   - Title row     : ECU label + CAN status indicator
//   - Nano          : the board illustration
//   - Hardware row  : LCD on the left, buzzer on the right
//   - Pots row      : four trim pots
//   - I/O row       : 2x2 grid of breakout LEDs + 2x2 grid of buttons
//   - Serial console: fills the remaining vertical space
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

        // ---- Title row: label + CAN status -------------------------
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
            Item { Layout.fillWidth: true }   // left spacer
            CanIndicator {
                chip: root.chip
                Layout.preferredWidth: 200
                Layout.preferredHeight: 22
            }
        }

        // ---- Nano illustration -------------------------------------
        // Height tracks the image's actual 1500x571 aspect (ratio
        // 2.627) so the painted region exactly fills the layout cell.
        ArduinoNano {
            id: nano
            chip: root.chip
            power: bridge && bridge.engineRunning
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(width / (1500.0 / 571.0), 150)
            Layout.minimumHeight: 80
        }

        Connections {
            target: bridge
            function onUartAppended(chip, data) {
                if (chip === root.chip) nano.pulseTx()
            }
            function onUartSent(chip) {
                if (chip === root.chip) nano.pulseRx()
            }
        }

        // ---- Hardware row: LCD + Buzzer ----------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            spacing: 12

            LcdWidget {
                chip: root.chip
                Layout.preferredWidth:  208
                Layout.preferredHeight: 44
            }
            Item { Layout.fillWidth: true }    // spacer
            BuzzerWidget {
                chip: root.chip
                Layout.preferredWidth:  46
                Layout.preferredHeight: 46
            }
            Item { Layout.preferredWidth: 8 }
        }

        // ---- I/O row: LEDs (left) + Pots (center) + Buttons (right) -
        // One row holds every input and output control so vertical
        // space isn't wasted on a separate trimpot row. LEDs anchor to
        // the left edge, buttons to the right, and the four trimpots
        // float in the middle gap.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            spacing: 8

            // 2x2 LED grid: DOUT0 amber, DOUT1 green, L red, LDR blue.
            GridLayout {
                columns: 2
                rowSpacing: 6
                columnSpacing: 8
                Layout.alignment: Qt.AlignVCenter

                ColumnLayout {
                    spacing: 0
                    Layout.alignment: Qt.AlignHCenter
                    Led {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 24; Layout.preferredHeight: 24
                        onColor: "#ffaa22"     // amber — DOUT0 (PD3, dimmable)
                        brightness: nano.level("PD3")
                                    + (nano.duty("PD3") * (1 - nano.level("PD3")))
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "DOUT0"; color: "#a8d0b0"
                        font.family: "Consolas"; font.pixelSize: 7
                    }
                }
                ColumnLayout {
                    spacing: 0
                    Layout.alignment: Qt.AlignHCenter
                    Led {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 24; Layout.preferredHeight: 24
                        onColor: "#22cc44"     // green — DOUT1 (PD4)
                        brightness: nano.level("PD4")
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "DOUT1"; color: "#a8d0b0"
                        font.family: "Consolas"; font.pixelSize: 7
                    }
                }
                ColumnLayout {
                    spacing: 0
                    Layout.alignment: Qt.AlignHCenter
                    Led {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 24; Layout.preferredHeight: 24
                        onColor: "#ff3344"     // red — Nano on-board L LED (PB5)
                        brightness: nano.level("PB5")
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "L"; color: "#a8d0b0"
                        font.family: "Consolas"; font.pixelSize: 7
                    }
                }
                ColumnLayout {
                    spacing: 0
                    Layout.alignment: Qt.AlignHCenter
                    Led {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 24; Layout.preferredHeight: 24
                        onColor: "#3aaaff"     // blue — LDR_LED (PD6 PWM)
                        brightness: nano.duty("PD6")
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "LDR"; color: "#a8d0b0"
                        font.family: "Consolas"; font.pixelSize: 7
                    }
                }
            }

            Item { Layout.fillWidth: true }   // expanding spacer

            // Pots row floats in the middle of the I/O row.
            RowLayout {
                spacing: 6
                Layout.alignment: Qt.AlignVCenter
                TrimPot { chip: root.chip; channel: 0; label: "AIN0  A0"
                          Layout.preferredWidth: 40; Layout.preferredHeight: 60 }
                TrimPot { chip: root.chip; channel: 1; label: "AIN1  A1"
                          Layout.preferredWidth: 40; Layout.preferredHeight: 60 }
                TrimPot { chip: root.chip; channel: 2; label: "AIN2  A2"
                          Layout.preferredWidth: 40; Layout.preferredHeight: 60 }
                TrimPot { chip: root.chip; channel: 3; label: "AIN3  A3"
                          Layout.preferredWidth: 40; Layout.preferredHeight: 60 }
            }

            Item { Layout.fillWidth: true }   // expanding spacer

            // 2x2 button grid: DIN1..DIN4, each a different remote-cap color.
            GridLayout {
                columns: 2
                rowSpacing: 4
                columnSpacing: 6
                Layout.alignment: Qt.AlignVCenter

                PushButton {
                    chip: root.chip; pin: "A4"; label: "DIN1  A4"
                    color: "#e04a4a"            // red
                    Layout.preferredWidth: 42; Layout.preferredHeight: 56
                }
                PushButton {
                    chip: root.chip; pin: "A5"; label: "DIN2  A5"
                    color: "#e8c440"            // yellow
                    Layout.preferredWidth: 42; Layout.preferredHeight: 56
                }
                PushButton {
                    chip: root.chip; pin: "D9"; label: "DIN3  D9"
                    color: "#3ec85a"            // green
                    Layout.preferredWidth: 42; Layout.preferredHeight: 56
                }
                PushButton {
                    chip: root.chip; pin: "D8"; label: "DIN4  D8"
                    color: "#3a8fe8"            // blue
                    Layout.preferredWidth: 42; Layout.preferredHeight: 56
                }
            }
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
