// ArduinoNano.qml — top-down Arduino Nano drawn entirely in QML.
//
// All elements (PCB body, USB connector, chip, crystal, reset button,
// on-board LEDs, header pads, silkscreen labels) are first-class QML
// items and react to bridge state. Internally everything is laid out
// at a fixed 420x160 logical coordinate system; an outer Item scales
// it to fit whatever size the parent Layout assigns, so text stays
// crisp at any window size.
import QtQuick 2.15

Item {
    id: root

    property string chip:  ""
    property bool   power: false       // driven by parent (engineRunning)

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
        if (!c) return 0
        return c[p] ? 1 : 0
    }
    function duty(p) {
        if (!bridge || !bridge.pwmDuties) return 0.0
        var c = bridge.pwmDuties[root.chip]
        if (!c) return 0.0
        return c[p] ? c[p] : 0.0
    }

    implicitWidth:  420
    implicitHeight: 160

    // ---- Logical 420x160 board, scaled uniformly to fit -----------
    Item {
        id: stage
        width:  420
        height: 160
        anchors.centerIn: parent
        scale: Math.min(root.width / 420, root.height / 160)
        transformOrigin: Item.Center

        // ---- PCB body -----------------------------------------------
        Rectangle {
            id: pcb
            x: 12; y: 0
            width: 408; height: 160
            radius: 6
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#1f7878" }
                GradientStop { position: 1.0; color: "#0d4a4a" }
            }
            border.color: "#062828"
            border.width: 1
        }

        // ---- USB connector (silver block protruding off the left) --
        Rectangle {
            x: 0; y: 60
            width: 38; height: 40
            radius: 2
            border.color: "#444"; border.width: 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#dcdfe4" }
                GradientStop { position: 1.0; color: "#7a7d82" }
            }
            // Faint USB-port slot.
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.55; height: parent.height * 0.45
                radius: 1
                color: "#1a1a1a"
            }
        }

        // ---- ATmega328P chip ---------------------------------------
        Rectangle {
            id: chipBody
            x: 196; y: 60
            width: 60; height: 50
            radius: 1
            color: "#1c1c20"
            border.color: "#000"; border.width: 1
            // Pin-1 dot.
            Rectangle {
                x: 4; y: 4; width: 3; height: 3; radius: 1.5
                color: "#aaaaaa"
            }
            Text {
                anchors.centerIn: parent
                text: "ATmega\n328P"
                horizontalAlignment: Text.AlignHCenter
                color: "#cccccc"
                font.family: "Consolas"; font.pixelSize: 8; font.bold: true
            }
        }

        // ---- Crystal oscillator -----------------------------------
        Rectangle {
            x: 260; y: 76
            width: 22; height: 18; radius: 2
            border.color: "#666"; border.width: 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#cccccc" }
                GradientStop { position: 1.0; color: "#888888" }
            }
        }

        // ---- Reset button ------------------------------------------
        Rectangle {
            x: 290; y: 78; width: 14; height: 14; radius: 7
            color: "#222"
            border.color: "#444"; border.width: 1
        }

        // ---- ICSP header (6-pin grid) ------------------------------
        Grid {
            x: 358; y: 70; rows: 2; columns: 3; spacing: 3
            Repeater {
                model: 6
                Rectangle {
                    width: 7; height: 7; radius: 3.5
                    color: "#1a1a1a"
                    border.color: "#a0a0a0"; border.width: 1
                }
            }
        }

        // ---- On-board LEDs (PWR / L / TX / RX) ---------------------
        // Cluster between the chip and the ICSP header.
        OnBoardLed {
            x: 320; y: 56
            color: "#ff5544"; brightness: root.txGlow; label: "TX"
        }
        OnBoardLed {
            x: 320; y: 70
            color: "#ffe044"; brightness: root.rxGlow; label: "RX"
        }
        OnBoardLed {
            x: 320; y: 84
            color: "#ffaa22"
            brightness: Math.max(root.level("PB5"), root.duty("PD6"))
            label: "L"
        }
        OnBoardLed {
            x: 320; y: 98
            color: "#22cc44"
            brightness: root.power ? 1.0 : 0.0
            label: "PWR"
        }

        // ---- Header rows --------------------------------------------
        // Top row (15 pins). Labels and (optional) AVR-port mapping
        // for live state dots.
        readonly property var topRow: [
            { lbl: "D12", port: "PB4" },
            { lbl: "D11", port: "PB3" },
            { lbl: "D10", port: "PB2" },
            { lbl: "D9",  port: "PB1" },
            { lbl: "D8",  port: "PB0" },
            { lbl: "D7",  port: "PD7" },
            { lbl: "D6",  port: "PD6" },
            { lbl: "D5",  port: "PD5" },
            { lbl: "D4",  port: "PD4" },
            { lbl: "D3",  port: "PD3" },
            { lbl: "D2",  port: "PD2" },
            { lbl: "GND", port: ""    },
            { lbl: "RST", port: ""    },
            { lbl: "RX0", port: "PD0" },
            { lbl: "TX1", port: "PD1" },
        ]
        readonly property var bottomRow: [
            { lbl: "D13",  port: "PB5" },
            { lbl: "3V3",  port: ""    },
            { lbl: "AREF", port: ""    },
            { lbl: "A0",   port: "PC0" },
            { lbl: "A1",   port: "PC1" },
            { lbl: "A2",   port: "PC2" },
            { lbl: "A3",   port: "PC3" },
            { lbl: "A4",   port: "PC4" },
            { lbl: "A5",   port: "PC5" },
            { lbl: "A6",   port: ""    },
            { lbl: "A7",   port: ""    },
            { lbl: "5V",   port: ""    },
            { lbl: "RST",  port: ""    },
            { lbl: "GND",  port: ""    },
            { lbl: "VIN",  port: ""    },
        ]
        readonly property real headerStartX: 26
        readonly property real headerSpacing: 26

        Repeater {
            model: stage.topRow
            HeaderPad {
                x: stage.headerStartX + index * stage.headerSpacing - 7
                y: 6
                isTop: true
                label: modelData.lbl
                pinPort: modelData.port
            }
        }
        Repeater {
            model: stage.bottomRow
            HeaderPad {
                x: stage.headerStartX + index * stage.headerSpacing - 7
                y: 132
                isTop: false
                label: modelData.lbl
                pinPort: modelData.port
            }
        }
    }   // stage

    // ---- Inline components ---------------------------------------

    // OnBoardLed: a tiny status LED with a glow halo.
    component OnBoardLed: Item {
        id: lc
        property color color:      "#ffaa22"
        property real  brightness: 0.0     // 0..1
        property string label:     ""
        width: 6; height: 6                 // logical px

        // Glow halo.
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 3.0; height: parent.height * 3.0
            radius: width / 2
            color: lc.color
            opacity: 0.5 * lc.brightness
            visible: lc.brightness > 0.05
        }
        // Body.
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: lc.brightness > 0.05 ? lc.color : "#161616"
            border.color: "#0a0a0a"; border.width: 0.5
            // Specular dot.
            Rectangle {
                anchors.top: parent.top; anchors.left: parent.left
                anchors.topMargin: parent.height * 0.15
                anchors.leftMargin: parent.width * 0.15
                width: parent.width * 0.35; height: parent.height * 0.35
                radius: width / 2
                color: "white"
                opacity: lc.brightness * 0.7
                visible: lc.brightness > 0.05
            }
        }
        // Silkscreen label, just to the left.
        Text {
            anchors.right: parent.left
            anchors.rightMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            text: lc.label
            color: "#e8f0e8"
            font.family: "Consolas"; font.pixelSize: 5; font.bold: true
        }
        Behavior on brightness { NumberAnimation { duration: 80 } }
    }

    // HeaderPad: gold pad + center hole + state dot + silkscreen label.
    component HeaderPad: Item {
        id: hp
        property string label:   ""
        property string pinPort: ""        // empty for power/GND/etc.
        property bool   isTop:   true
        width: 14; height: 24

        function _level() {
            if (!hp.pinPort) return 0
            return Math.max(root.level(hp.pinPort), root.duty(hp.pinPort))
        }

        Rectangle {
            id: pad
            x: 0; width: 14; height: 14; radius: 7
            y: hp.isTop ? 0 : hp.height - 14
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#f6d278" }
                GradientStop { position: 1.0; color: "#a07820" }
            }
            border.color: "#503810"; border.width: 0.5

            // Center hole.
            Rectangle {
                anchors.centerIn: parent
                width: 5.5; height: 5.5; radius: 2.75
                color: "#101010"
            }
            // Live HIGH dot.
            Rectangle {
                anchors.centerIn: parent
                width: 3.5; height: 3.5; radius: 1.75
                color: "#ffd24a"
                opacity: hp._level() > 0 ? 0.95 * hp._level() : 0
                visible: opacity > 0.05
                Behavior on opacity { NumberAnimation { duration: 80 } }
            }
            // Halo for HIGH.
            Rectangle {
                anchors.centerIn: parent
                width: 9; height: 9; radius: 4.5
                color: "#ffd24a"
                opacity: 0.55 * hp._level()
                visible: hp._level() > 0.05
            }
        }

        Text {
            anchors.horizontalCenter: pad.horizontalCenter
            y: hp.isTop ? pad.bottom + 0 : pad.top - 8
            text: hp.label
            color: "#f0f5ed"
            font.family: "Consolas"
            font.pixelSize: 6
            font.bold: true
        }
    }
}
