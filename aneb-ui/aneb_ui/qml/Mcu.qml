// Mcu.qml — daughterboard panel for the ATmega328PB controller.
// Uses QtQuick.Layouts to flex on resize.
import QtQuick 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    implicitWidth:  460
    implicitHeight: 360

    Rectangle {
        anchors.fill: parent
        color: "#0d2418"
        border.color: "#3e6b4d"
        border.width: 1
        radius: 6
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        Text {
            text: "MCU  ATmega328PB"
            color: "#cdfac0"
            font.family: "Consolas"
            font.pixelSize: 14
            font.bold: true
            Layout.preferredHeight: 22
        }

        // Mode-selector buttons.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            Layout.alignment: Qt.AlignHCenter
            spacing: 14
            Item { Layout.fillWidth: true }
            PushButton {
                chip: "mcu"; pin: "D8"; label: "Mode 1\nD8"; latching: true
                Layout.preferredWidth: 60; Layout.preferredHeight: 66
            }
            PushButton {
                chip: "mcu"; pin: "D9"; label: "Mode 2\nD9"; latching: true
                Layout.preferredWidth: 60; Layout.preferredHeight: 66
            }
            Item { Layout.fillWidth: true }
        }

        ArduinoNano {
            chip: "mcu"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(width * (160.0 / 420.0), 180)
            Layout.minimumHeight: 80
        }

        SerialConsole {
            chip: "mcu"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 60
        }
    }
}
