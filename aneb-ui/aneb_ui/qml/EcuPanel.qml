// EcuPanel.qml — composes one ECU's worth of widgets.
//
// Layout v2: Nano dominates as the primary visual; the other widgets
// (LEDs, pots, buttons, CAN status, serial) sit around it in tighter
// rows so the chip remains the recognizable centerpiece.
//
//   Title row     — chip name + breakout LEDs + CAN status badge
//   Nano image    — full panel width, prominent
//   Pots row      — 4 trim pots
//   Buttons row   — 4 tactile pushbuttons
//   Serial console
import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root
    property string chip:  ""
    property string label: ""

    implicitWidth:  720
    implicitHeight: 440

    Rectangle {
        anchors.fill: parent
        color: "#0d2418"
        border.color: "#3e6b4d"
        border.width: 1
        radius: 6
    }

    Column {
        id: stack
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // ---- Title row (label + breakout LEDs + CAN badge) -----------
        Row {
            id: titleRow
            width: parent.width
            spacing: 12

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.label
                color: "#cdfac0"
                font.family: "Consolas"
                font.pixelSize: 14
                font.bold: true
            }

            // Compact LED cluster — DOUT0 / DOUT1 / LED / BUZZ.
            Row {
                spacing: 6
                anchors.verticalCenter: parent.verticalCenter
                Led {
                    width: 16; height: 16; onColor: "#22cc44"
                    brightness: nano.level("PD3")
                                + (nano.duty("PD3") * (1 - nano.level("PD3")))
                }
                Led {
                    width: 16; height: 16; onColor: "#22cc44"
                    brightness: nano.level("PD4")
                }
                Led {
                    width: 16; height: 16; onColor: "#ffaa22"
                    brightness: nano.level("PB5")
                                + (nano.duty("PD6") * (1 - nano.level("PB5")))
                }
                Led {
                    width: 16; height: 16; onColor: "#ff4444"
                    brightness: nano.level("PD7")
                }
            }

            CanIndicator {
                anchors.verticalCenter: parent.verticalCenter
                chip: root.chip
                width: 220
            }
        }

        // ---- Nano illustration — the centerpiece --------------------
        ArduinoNano {
            id: nano
            chip: root.chip
            width: parent.width
            // Native aspect ratio is 420:160 = 2.625:1.
            height: width * (160.0 / 420.0)
        }

        // ---- Pots row -----------------------------------------------
        Row {
            spacing: 6
            anchors.horizontalCenter: parent.horizontalCenter
            TrimPot { chip: root.chip; channel: 0; label: "AIN0  A0" }
            TrimPot { chip: root.chip; channel: 1; label: "AIN1  A1" }
            TrimPot { chip: root.chip; channel: 2; label: "AIN2  A2" }
            TrimPot { chip: root.chip; channel: 3; label: "AIN3  A3" }
        }

        // ---- Buttons row --------------------------------------------
        Row {
            spacing: 8
            anchors.horizontalCenter: parent.horizontalCenter
            PushButton { chip: root.chip; pin: "A4"; label: "DIN1  A4" }
            PushButton { chip: root.chip; pin: "A5"; label: "DIN2  A5" }
            PushButton { chip: root.chip; pin: "D9"; label: "DIN3  D9" }
            PushButton { chip: root.chip; pin: "D8"; label: "DIN4  D8" }
        }

        // ---- Serial console -----------------------------------------
        SerialConsole {
            chip: root.chip
            width: parent.width
            height: parent.height - y - 4
        }
    }
}
