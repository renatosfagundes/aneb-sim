// ArduinoNano.qml — photoreal top-down Arduino Nano drawn in QML.
//
// Drop-in for the original — keeps the same `chip`, `power`, `txGlow`,
// `rxGlow` properties and the `pulseTx()` / `pulseRx()` / `level()` /
// `duty()` functions. Reads `bridge.pinStates[chip][port]` and
// `bridge.pwmDuties[chip][port]` for live state.
import QtQuick 2.15

Item {
    id: root

    property string chip: ""
    property bool   power: false

    property real   txGlow: 0.0
    property real   rxGlow: 0.0

    function pulseTx() { txGlow = 1.0; txDecay.restart() }
    function pulseRx() { rxGlow = 1.0; rxDecay.restart() }

    Timer {
        id: txDecay; interval: 40; repeat: true; running: false
        onTriggered: { root.txGlow *= 0.55; if (root.txGlow < 0.05) { root.txGlow = 0; running = false } }
    }
    Timer {
        id: rxDecay; interval: 40; repeat: true; running: false
        onTriggered: { root.rxGlow *= 0.55; if (root.rxGlow < 0.05) { root.rxGlow = 0; running = false } }
    }

    function level(p) {
        if (!bridge || !bridge.pinStates) return 0
        var c = bridge.pinStates[root.chip]
        return (c && c[p]) ? 1 : 0
    }
    function duty(p) {
        if (!bridge || !bridge.pwmDuties) return 0.0
        var c = bridge.pwmDuties[root.chip]
        return (c && c[p]) ? c[p] : 0.0
    }

    implicitWidth: 420
    implicitHeight: 160

    Item {
        id: stage
        width: 420; height: 160
        anchors.centerIn: parent
        scale: Math.min(root.width / 420, root.height / 160)
        transformOrigin: Item.Center

        // --- USB Connector ---
        Rectangle {
            x: 2; y: 65
            width: 30; height: 32
            radius: 2
            gradient: Gradient {
                GradientStop { position: 0; color: "#b0b0b0" }
                GradientStop { position: 1; color: "#707070" }
            }
            Rectangle {
                anchors.centerIn: parent
                width: 16; height: 12
                color: "#1a1a1a"
                border.color: "#333"
            }
        }

        // --- PCB Body ---
        Rectangle {
            id: pcb
            x: 12; y: 8; width: 396; height: 144
            radius: 6
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#00695C" }
                GradientStop { position: 1.0; color: "#004D40" }
            }
            border.color: "#00332B"
            border.width: 1

            // Subtle trace simulation
            Rectangle { x: 30; y: 40; width: 340; height: 1; color: "#00554A"; opacity: 0.4 }
            Rectangle { x: 30; y: 100; width: 340; height: 1; color: "#00554A"; opacity: 0.4 }
        }

        // --- ICSP Header (6-pin) ---
        Grid {
            x: 360; y: 68; rows: 2; columns: 3; spacing: 4
            Repeater {
                model: 6
                Rectangle {
                    width: 8; height: 8; radius: 4
                    color: "#1a1a1a"
                    border.color: "#444"; border.width: 1
                }
            }
        }

        // --- ATmega328P Chip ---
        Rectangle {
            x: 180; y: 55; width: 65; height: 50
            color: "#111"
            border.color: "#222"; border.width: 2

            // Pin 1 dot
            Rectangle { x: 4; y: 4; width: 4; height: 4; radius: 2; color: "#555" }

            Text {
                anchors.centerIn: parent
                text: "ATMEGA\n328P"
                color: "#aaa"; font.pixelSize: 7; font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }
        }

        // --- Crystal & Reset ---
        Rectangle { x: 260; y: 60; width: 20; height: 14; radius: 2; color: "#ccc"; border.color: "#999" }
        Rectangle { x: 290; y: 60; width: 14; height: 14; radius: 2; color: "#444"; border.color: "#222" }

        // --- SMD LEDs Cluster ---
        Column {
            x: 330; y: 50; spacing: 8
            OnBoardLed { color: "#ff4444"; brightness: root.txGlow; label: "TX" }
            OnBoardLed { color: "#ffdd44"; brightness: root.rxGlow; label: "RX" }
            OnBoardLed { color: "#ffaa22"; brightness: Math.max(root.level("PB5"), root.duty("PD6")); label: "L" }
            OnBoardLed { color: "#22cc44"; brightness: root.power ? 1.0 : 0.0; label: "PWR" }
        }

        // --- Headers ---
        readonly property var topRow: [
            { lbl: "D12", port: "PB4" }, { lbl: "D11", port: "PB3" }, { lbl: "D10", port: "PB2" },
            { lbl: "D9", port: "PB1" }, { lbl: "D8", port: "PB0" }, { lbl: "D7", port: "PD7" },
            { lbl: "D6", port: "PD6" }, { lbl: "D5", port: "PD5" }, { lbl: "D4", port: "PD4" },
            { lbl: "D3", port: "PD3" }, { lbl: "D2", port: "PD2" }, { lbl: "GND", port: "" },
            { lbl: "RST", port: "" }, { lbl: "RX0", port: "PD0" }, { lbl: "TX1", port: "PD1" }
        ]
        readonly property var bottomRow: [
            { lbl: "D13", port: "PB5" }, { lbl: "3V3", port: "" }, { lbl: "AREF", port: "" },
            { lbl: "A0", port: "PC0" }, { lbl: "A1", port: "PC1" }, { lbl: "A2", port: "PC2" },
            { lbl: "A3", port: "PC3" }, { lbl: "A4", port: "PC4" }, { lbl: "A5", port: "PC5" },
            { lbl: "A6", port: "" }, { lbl: "A7", port: "" }, { lbl: "5V", port: "" },
            { lbl: "RST", port: "" }, { lbl: "GND", port: "" }, { lbl: "VIN", port: "" }
        ]

        Repeater {
            model: stage.topRow
            HeaderPad {
                x: 24 + index * 26; y: 0
                isTop: true; label: modelData.lbl; pinPort: modelData.port
            }
        }
        Repeater {
            model: stage.bottomRow
            HeaderPad {
                x: 24 + index * 26; y: 136
                isTop: false; label: modelData.lbl; pinPort: modelData.port
            }
        }
    }

    // --- Components ---
    component OnBoardLed: Item {
        id: lc
        property color color: "#ffaa22"; property real brightness: 0.0; property string label: ""
        width: 12; height: 6
        Row {
            spacing: 4
            Rectangle {
                width: 6; height: 6; radius: 1
                color: lc.brightness > 0.1 ? lc.color : "#111"
                border.color: "#000"; border.width: 0.5
                Rectangle {
                    anchors.centerIn: parent; width: 2; height: 2; radius: 1
                    color: "white"; opacity: lc.brightness
                }
            }
            Text { text: lc.label; color: "#e8f0e8"; font.pixelSize: 6; font.bold: true }
        }
    }

    component HeaderPad: Item {
        id: hp
        property string label: ""; property string pinPort: ""; property bool isTop: true
        width: 14; height: 24

        function _level() { return (!hp.pinPort) ? 0 : Math.max(root.level(hp.pinPort), root.duty(hp.pinPort)) }

        // Gold Pad
        Rectangle {
            id: pad
            x: 0; width: 14; height: 14; radius: 7
            y: hp.isTop ? 0 : 10
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#FFD700" }
                GradientStop { position: 1.0; color: "#B8860B" }
            }
            border.color: "#8B4513"; border.width: 0.5

            // Hole
            Rectangle {
                anchors.centerIn: parent; width: 6; height: 6; radius: 3; color: "#1a1a1a"
                // Activity Light
                Rectangle {
                    anchors.centerIn: parent; width: 4; height: 4; radius: 2
                    color: "#ffd24a"; opacity: hp._level() * 0.8
                    Behavior on opacity { NumberAnimation { duration: 50 } }
                }
            }
        }

        Text {
            anchors.horizontalCenter: pad.horizontalCenter
            y: hp.isTop ? pad.bottom + 1 : pad.top - 8
            text: hp.label; color: "#f0f5ed"; font.pixelSize: 6; font.family: "Consolas"
        }
    }
}
