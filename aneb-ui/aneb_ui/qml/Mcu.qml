// Mcu.qml — daughterboard panel for the ATmega328PB controller.
// Uses QtQuick.Layouts to flex on resize. Every preferred size scales
// with `_s` so the whole panel content shrinks/grows uniformly with
// the slot the parent layout gives it.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    implicitWidth:  460
    implicitHeight: 240

    readonly property real _s: Math.max(0.5, Math.min(width / 460, height / 240, 1.4))

    Rectangle {
        anchors.fill: parent
        color: "#0d2418"
        border.color: "#3e6b4d"
        border.width: 1
        radius: 6
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10 * root._s
        spacing: 6 * root._s

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 22 * root._s
            spacing: 10 * root._s

            Text {
                text: "MCU  ATmega328PB"
                color: "#cdfac0"
                font.family: "Consolas"
                font.pixelSize: 14 * root._s
                font.bold: true
            }
            Button {
                text: mcuConsole.visible ? "Console ▾" : "Console ▸"
                Layout.preferredHeight: 22 * root._s
                font.pixelSize: 10 * root._s
                onClicked: mcuConsole.visible = !mcuConsole.visible
            }
            Item { Layout.fillWidth: true }
        }

        SerialConsoleWindow {
            id: mcuConsole
            chip:  "mcu"
            label: "MCU"
        }

        // Mode-selector buttons.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 66 * root._s
            Layout.alignment: Qt.AlignHCenter
            spacing: 14 * root._s
            Item { Layout.fillWidth: true }
            PushButton {
                chip: "mcu"; pin: "D8"; label: "Mode 1\nD8"; latching: true
                Layout.preferredWidth:  60 * root._s
                Layout.preferredHeight: 66 * root._s
            }
            PushButton {
                chip: "mcu"; pin: "D9"; label: "Mode 2\nD9"; latching: true
                Layout.preferredWidth:  60 * root._s
                Layout.preferredHeight: 66 * root._s
            }
            Item { Layout.fillWidth: true }
        }

        ArduinoNano {
            id: mcuNano
            chip: "mcu"
            power: bridge && bridge.engineRunning
            Layout.fillWidth: true
            Layout.preferredHeight: width / (1500.0 / 571.0)
            Layout.minimumHeight: 40
            Layout.maximumHeight: 220
        }
        Item { Layout.fillHeight: true }   // soaks up any remaining height
        Connections {
            target: bridge
            function onUartAppended(chip, data) {
                if (chip === "mcu") mcuNano.pulseTx()
            }
            function onUartSent(chip) {
                if (chip === "mcu") mcuNano.pulseRx()
            }
        }
    }
}
