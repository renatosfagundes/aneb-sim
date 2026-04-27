// EcuPanel.qml — composes one ECU's worth of widgets, using
// QtQuick.Layouts so children shrink gracefully when the window
// resizes and never overflow the panel.
//
// Layout from top to bottom:
//   - Title row : ECU label + Console toggle + CAN status indicator
//   - Nano      : the board illustration
//   - Hardware  : LCD on the left, buzzer on the right
//   - I/O row   : 2x2 LEDs + 4 trimpots + 2x2 buttons
//
// The serial console is no longer inline — clicking the Console
// toggle button pops up a separate top-level window per chip.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    property string chip:  ""
    property string label: ""

    implicitWidth:  720
    implicitHeight: 360
    Layout.minimumWidth:  340
    Layout.minimumHeight: 270

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

        // ---- Title row: label + Console toggle + CAN status --------
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
            Button {
                text: consoleWindow.visible ? "Console ▾" : "Console ▸"
                Layout.preferredHeight: 22
                onClicked: consoleWindow.visible = !consoleWindow.visible
            }
            Item { Layout.fillWidth: true }   // expanding spacer
            CanIndicator {
                chip: root.chip
                Layout.preferredWidth: 200
                Layout.preferredHeight: 22
            }
        }

        // The window itself is hidden by default; the Console button
        // above toggles it. Each ECU owns its own window so the user
        // can have multiple consoles open at once.
        SerialConsoleWindow {
            id: consoleWindow
            chip:  root.chip
            label: root.label
        }

        // ---- Nano illustration -------------------------------------
        // Layout.fillHeight makes the Nano absorb whatever vertical
        // space the title / hardware / I/O rows leave behind, so the
        // illustration scales up on tall panels and shrinks down when
        // the window gets squeezed. The Image element inside uses
        // PreserveAspectFit, so the painted board stays in proportion
        // at every size.
        ArduinoNano {
            id: nano
            chip: root.chip
            power: bridge && bridge.engineRunning
            Layout.fillWidth:  true
            Layout.fillHeight: true
            Layout.minimumHeight: 50
            Layout.maximumHeight: 220
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

        // ---- Hardware row: LCD + Buzzer (centered as a group) ------
        // Both flank-spacers expand equally, so the LCD/buzzer pair
        // sits in the middle of the row. The LCD width itself scales
        // with the panel width (clamped 200..340) so the text grows
        // on wider screens without ever overrunning the 16-char
        // capacity that the HD44780 actually has.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            spacing: 8

            Item { Layout.fillWidth: true }     // expanding left margin
            LcdWidget {
                chip: root.chip
                Layout.preferredWidth:  Math.max(200, Math.min(root.width * 0.55, 340))
                Layout.preferredHeight: 44
                Layout.maximumHeight:   44
            }
            BuzzerWidget {
                chip: root.chip
                Layout.preferredWidth:  44
                Layout.preferredHeight: 44
            }
            Item { Layout.fillWidth: true }     // expanding right margin
        }

        // ---- I/O row: LEDs (left) + Pots (center) + Buttons (right) -
        // One row holds every input and output control so vertical
        // space isn't wasted on a separate trimpot row. LEDs anchor to
        // the left edge, buttons to the right, and the four trimpots
        // float in the middle gap. Sized to exactly fit a 2x2 buttons
        // grid (2 * 56 button height + 4 rowSpacing) plus 4 px of
        // breathing room.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 116
            Layout.minimumHeight:   116
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

    }
}
