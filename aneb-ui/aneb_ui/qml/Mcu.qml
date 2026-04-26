// Mcu.qml — small daughterboard panel for the ATmega328PB controller.
//
// Visually a smaller Nano (uses the same render asset for now) plus the
// two latching mode-selector buttons that appear at the top of the real
// board, plus a serial console.
import QtQuick 2.15

Item {
    id: root
    implicitWidth:  500
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
        anchors.margins: 12
        spacing: 8

        Text {
            text: "MCU  ATmega328PB"
            color: "#cdfac0"
            font.family: "Consolas"
            font.pixelSize: 14
            font.bold: true
        }

        Row {
            spacing: 12
            anchors.horizontalCenter: parent.horizontalCenter
            PushButton { chip: "mcu"; pin: "D8"; label: "Mode 1\nD8"; latching: true }
            PushButton { chip: "mcu"; pin: "D9"; label: "Mode 2\nD9"; latching: true }
        }

        ArduinoNano {
            chip: "mcu"
            width: parent.width
            height: width * (160.0 / 420.0)
        }

        SerialConsole {
            chip: "mcu"
            width: parent.width
            height: parent.height - y - 8
        }
    }
}
