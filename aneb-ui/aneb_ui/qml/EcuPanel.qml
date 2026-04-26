// EcuPanel.qml — composes one ECU's worth of widgets:
//   Nano illustration on the left (primary) with live header pin dots,
//   a column on the right with breakout LEDs, pots (4), buttons (4),
//   the CAN status pill, and a serial console below.
import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root
    property string chip:  ""
    property string label: ""

    implicitWidth:  720
    implicitHeight: 380

    Rectangle {
        anchors.fill: parent
        color: "#0d2418"
        border.color: "#3e6b4d"
        border.width: 1
        radius: 6
    }

    Column {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        Text {
            text: root.label
            color: "#cdfac0"
            font.family: "Consolas"
            font.pixelSize: 14
            font.bold: true
        }

        Row {
            id: topRow
            width: parent.width
            spacing: 12

            // Left: the Nano.
            ArduinoNano {
                id: nano
                chip: root.chip
                width: parent.width * 0.62
                height: width * (160.0 / 420.0)
            }

            // Right: breakout LEDs.
            Column {
                spacing: 4
                Row {
                    spacing: 8
                    Led {
                        onColor: "#22cc44"; width: 22; height: 22
                        brightness: nano.level("PD3")
                                    + (nano.duty("PD3") * (1 - nano.level("PD3")))
                    }
                    Led {
                        onColor: "#22cc44"; width: 22; height: 22
                        brightness: nano.level("PD4")
                    }
                    Led {
                        onColor: "#ffaa22"; width: 22; height: 22
                        brightness: nano.level("PB5")
                                    + (nano.duty("PD6") * (1 - nano.level("PB5")))
                    }
                    Led {
                        onColor: "#ff4444"; width: 22; height: 22
                        brightness: nano.level("PD7")
                    }
                }
                Row {
                    spacing: 8
                    Text { text: "DOUT0"; color: "#a8d0b0"; font.pixelSize: 8; width: 22; horizontalAlignment: Text.AlignHCenter }
                    Text { text: "DOUT1"; color: "#a8d0b0"; font.pixelSize: 8; width: 22; horizontalAlignment: Text.AlignHCenter }
                    Text { text: "LED";   color: "#a8d0b0"; font.pixelSize: 8; width: 22; horizontalAlignment: Text.AlignHCenter }
                    Text { text: "BUZZ";  color: "#a8d0b0"; font.pixelSize: 8; width: 22; horizontalAlignment: Text.AlignHCenter }
                }

                Item { width: 1; height: 6 }

                CanIndicator { chip: root.chip; width: 220 }
            }
        }

        // Pots row — four AIN channels.
        Row {
            spacing: 4
            anchors.horizontalCenter: parent.horizontalCenter
            TrimPot { chip: root.chip; channel: 0; label: "AIN0\nA0" }
            TrimPot { chip: root.chip; channel: 1; label: "AIN1\nA1" }
            TrimPot { chip: root.chip; channel: 2; label: "AIN2\nA2" }
            TrimPot { chip: root.chip; channel: 3; label: "AIN3\nA3" }
        }

        // Buttons row — four DIN inputs.
        Row {
            spacing: 6
            anchors.horizontalCenter: parent.horizontalCenter
            PushButton { chip: root.chip; pin: "A4"; label: "DIN1\nA4" }
            PushButton { chip: root.chip; pin: "A5"; label: "DIN2\nA5" }
            PushButton { chip: root.chip; pin: "D9"; label: "DIN3\nD9" }
            PushButton { chip: root.chip; pin: "D8"; label: "DIN4\nD8" }
        }

        // Serial console.
        SerialConsole {
            chip: root.chip
            width: parent.width
            height: 90
        }
    }
}
