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
// Every interactive widget's preferred size is scaled by `_s`, a
// panel-relative scale factor, so the whole panel content shrinks or
// grows uniformly with the available slot. Nothing is fixed in
// absolute pixels — at the minimum window the controls compress
// proportionally instead of clipping off the bottom.
//
// The serial console is a separate top-level window per chip — a
// "Console" toggle button in the title row pops it up.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    property string chip:  ""
    property string label: ""

    implicitWidth:  720
    implicitHeight: 360
    Layout.minimumWidth:  240
    Layout.minimumHeight: 200

    // Scale factor — 1.0 at the "comfortable" reference size of
    // 600x320, 0.5 at the floor (so the smallest widgets still render
    // with usable shapes), and up to 1.4 when the panel grows. Driven
    // by whichever dimension is more constrained, so a tall narrow
    // panel doesn't make the controls overflow horizontally.
    readonly property real _s: Math.max(0.5, Math.min(width / 600, height / 320, 1.4))

    Rectangle {
        anchors.fill: parent
        color: "#0d2418"
        border.color: "#3e6b4d"
        border.width: 1
        radius: 6
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8 * root._s
        spacing: 4 * root._s

        // ---- Title row: label + Console toggle + CAN status --------
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 22 * root._s
            spacing: 10 * root._s

            Text {
                text: root.label
                color: "#cdfac0"
                font.family: "Consolas"
                font.pixelSize: 14 * root._s
                font.bold: true
                Layout.preferredWidth: 60 * root._s
            }
            Button {
                text: consoleWindow.visible ? "Console ▾" : "Console ▸"
                Layout.preferredHeight: 22 * root._s
                font.pixelSize: 10 * root._s
                onClicked: consoleWindow.visible = !consoleWindow.visible
            }
            Item { Layout.fillWidth: true }   // expanding spacer
            CanIndicator {
                chip: root.chip
                Layout.preferredWidth:  200 * root._s
                Layout.preferredHeight: 22  * root._s
            }
        }

        SerialConsoleWindow {
            id: consoleWindow
            chip:  root.chip
            label: root.label
        }

        // ---- Nano illustration -------------------------------------
        // Height is driven by width via the image's 1500x571 aspect
        // ratio, so the Nano fills the panel horizontally without
        // letterboxing.
        ArduinoNano {
            id: nano
            chip: root.chip
            power: bridge && bridge.engineRunning
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(width / (1500.0 / 571.0), 140)
            Layout.minimumHeight: 40
            Layout.maximumHeight: 150
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
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 44 * root._s
            spacing: 8 * root._s

            Item { Layout.fillWidth: true }
            LcdWidget {
                chip: root.chip
                // Aim for ~70% of the panel width with no upper cap
                // beyond what the panel itself imposes — at 16 chars
                // the HD44780 has a lot of horizontal real estate.
                Layout.preferredWidth:  Math.max(160, root.width * 0.7)
                Layout.preferredHeight: 44 * root._s
            }
            BuzzerWidget {
                chip: root.chip
                Layout.preferredWidth:  44 * root._s
                Layout.preferredHeight: 44 * root._s
            }
            Item { Layout.fillWidth: true }
        }

        // ---- I/O row: LEDs (left) + Pots (center) + Buttons (right) -
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 116 * root._s
            spacing: 8 * root._s

            // 2x2 LED grid: DOUT0 amber, DOUT1 green, L red, LDR blue.
            GridLayout {
                columns: 2
                rowSpacing: 6 * root._s
                columnSpacing: 8 * root._s
                Layout.alignment: Qt.AlignVCenter

                ColumnLayout {
                    spacing: 0
                    Layout.alignment: Qt.AlignHCenter
                    Led {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth:  24 * root._s
                        Layout.preferredHeight: 24 * root._s
                        onColor: "#ffaa22"     // amber — DOUT0 (PD3, dimmable)
                        brightness: nano.level("PD3")
                                    + (nano.duty("PD3") * (1 - nano.level("PD3")))
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "DOUT0"; color: "#a8d0b0"
                        font.family: "Consolas"; font.pixelSize: 7 * root._s
                    }
                }
                ColumnLayout {
                    spacing: 0
                    Layout.alignment: Qt.AlignHCenter
                    Led {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth:  24 * root._s
                        Layout.preferredHeight: 24 * root._s
                        onColor: "#22cc44"     // green — DOUT1 (PD4)
                        brightness: nano.level("PD4")
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "DOUT1"; color: "#a8d0b0"
                        font.family: "Consolas"; font.pixelSize: 7 * root._s
                    }
                }
                ColumnLayout {
                    spacing: 0
                    Layout.alignment: Qt.AlignHCenter
                    Led {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth:  24 * root._s
                        Layout.preferredHeight: 24 * root._s
                        onColor: "#ff3344"     // red — Nano on-board L LED (PB5)
                        brightness: nano.level("PB5")
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "L"; color: "#a8d0b0"
                        font.family: "Consolas"; font.pixelSize: 7 * root._s
                    }
                }
                ColumnLayout {
                    spacing: 0
                    Layout.alignment: Qt.AlignHCenter
                    Led {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth:  24 * root._s
                        Layout.preferredHeight: 24 * root._s
                        onColor: "#3aaaff"     // blue — LDR_LED (PD6 PWM)
                        brightness: nano.duty("PD6")
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "LDR"; color: "#a8d0b0"
                        font.family: "Consolas"; font.pixelSize: 7 * root._s
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // Pots row floats in the middle of the I/O row.
            RowLayout {
                spacing: 6 * root._s
                Layout.alignment: Qt.AlignVCenter
                TrimPot { chip: root.chip; channel: 0; label: "AIN0  A0"
                          Layout.preferredWidth:  40 * root._s
                          Layout.preferredHeight: 60 * root._s }
                TrimPot { chip: root.chip; channel: 1; label: "AIN1  A1"
                          Layout.preferredWidth:  40 * root._s
                          Layout.preferredHeight: 60 * root._s }
                TrimPot { chip: root.chip; channel: 2; label: "AIN2  A2"
                          Layout.preferredWidth:  40 * root._s
                          Layout.preferredHeight: 60 * root._s }
                TrimPot { chip: root.chip; channel: 3; label: "AIN3  A3"
                          Layout.preferredWidth:  40 * root._s
                          Layout.preferredHeight: 60 * root._s }
            }

            Item { Layout.fillWidth: true }

            // 2x2 button grid.
            GridLayout {
                columns: 2
                rowSpacing: 4 * root._s
                columnSpacing: 6 * root._s
                Layout.alignment: Qt.AlignVCenter

                PushButton {
                    chip: root.chip; pin: "A4"; label: "DIN1  A4"
                    color: "#e04a4a"
                    Layout.preferredWidth:  42 * root._s
                    Layout.preferredHeight: 56 * root._s
                }
                PushButton {
                    chip: root.chip; pin: "A5"; label: "DIN2  A5"
                    color: "#e8c440"
                    Layout.preferredWidth:  42 * root._s
                    Layout.preferredHeight: 56 * root._s
                }
                PushButton {
                    chip: root.chip; pin: "D9"; label: "DIN3  D9"
                    color: "#3ec85a"
                    Layout.preferredWidth:  42 * root._s
                    Layout.preferredHeight: 56 * root._s
                }
                PushButton {
                    chip: root.chip; pin: "D8"; label: "DIN4  D8"
                    color: "#3a8fe8"
                    Layout.preferredWidth:  42 * root._s
                    Layout.preferredHeight: 56 * root._s
                }
            }
        }
    }
}
