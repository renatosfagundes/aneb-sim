// AvrdudeWindow.qml — floating window that shows avrdude flash progress
// for one chip.  Opened automatically when a flash job starts; the user
// can also open it manually with the "Flash" button in EcuPanel.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

Window {
    id: root
    property string chip:  ""
    property string label: ""

    width:  560
    height: 300
    minimumWidth:  360
    minimumHeight: 180
    title: (label.length ? label : chip.toUpperCase()) + " — avrdude"
    color: "#061410"

    property bool _placed: false
    onVisibleChanged: {
        if (visible && !_placed) {
            x = Screen.virtualX + 120
            y = Screen.virtualY + 120
            _placed = true
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4

        // ---- output log -----------------------------------------------
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            TextArea {
                id: log
                readOnly: true
                wrapMode: TextArea.NoWrap
                color: "#cdfac0"
                font.family: "Consolas"
                font.pixelSize: 11
                background: Rectangle { color: "#061410" }
                selectByMouse: true
            }
        }

        // ---- bottom row: status + buttons ----------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle {
                width: 10; height: 10; radius: 5
                color: busy.running ? "#ffcc00" : "#22cc44"
                SequentialAnimation on opacity {
                    id: busy
                    running: false
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.3; duration: 500 }
                    NumberAnimation { to: 1.0; duration: 500 }
                }
            }
            Text {
                id: statusText
                text: busy.running ? "Flashing…" : "Idle"
                color: "#7aaa8a"
                font.family: "Consolas"
                font.pixelSize: 11
                verticalAlignment: Text.AlignVCenter
            }

            Item { Layout.fillWidth: true }

            Button {
                text: "Flash Again"
                enabled: !busy.running
                onClicked: { if (bridge) bridge.flashChipAvrdude(root.chip) }
            }
            Button {
                text: "Clear"
                onClicked: log.text = ""
            }
        }
    }

    Connections {
        target: bridge
        function onAvrdudeOutput(chip, line) {
            if (chip !== root.chip) return
            log.text += line + "\n"
            log.cursorPosition = log.length
        }
        function onAvrdudeStateChanged(chip, running) {
            if (chip !== root.chip) return
            busy.running = running
            if (running) root.visible = true   // auto-open when flash starts
        }
    }
}
