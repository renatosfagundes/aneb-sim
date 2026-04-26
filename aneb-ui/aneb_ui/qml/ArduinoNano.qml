// ArduinoNano.qml — SVG static base + QML dynamic overlays.
//
// The static board art comes from `qml_assets/arduino-vector.svg`
// (viewBox 300 x 135.83). On top, QML draws:
//   - tinted rectangles where the on-board status LEDs sit
//     (TX / RX / L / PWR), animating with engine + UART events
//   - small glowing dots over each header through-hole, lit when
//     the firmware drives the corresponding AVR pin HIGH
//
// All overlay positions are declared as numeric constants below in
// the SVG's native 300 x 135.83 coordinate space. Calibrate against
// the rendered SVG by tweaking the constants — no SVG editing
// required.
import QtQuick 2.15

Item {
    id: root

    // --- Public API ---
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

    // SVG's natural aspect ratio (matches arduino-nano.svg viewBox).
    readonly property real _svgW: 400
    readonly property real _svgH: 260
    implicitWidth: _svgW
    implicitHeight: _svgH

    // --- Stage scaled to fit the parent while preserving aspect ---
    Item {
        id: stage
        width:  root._svgW
        height: root._svgH
        anchors.centerIn: parent
        scale: Math.min(root.width / root._svgW, root.height / root._svgH)
        transformOrigin: Item.Center

        // ---- Static SVG base art ---------------------------------
        Image {
            id: nano
            anchors.fill: parent
            source: "../qml_assets/arduino-nano.svg"
            fillMode: Image.PreserveAspectFit
            sourceSize.width:  root._svgW * 3
            sourceSize.height: root._svgH * 3
            smooth: true
            antialiasing: true
        }

        // ---- Calibration constants ------------------------------
        // Coordinates come straight from arduino-nano.svg (viewBox
        // 0 0 400 260). LED <rect> bodies are 6x12, centered at
        // x+3, y+6. Pad <circle> elements have explicit cx/cy.
        readonly property var leds: ({
            tx:  { x: 308, y:  96, color: "#ff4444" },
            rx:  { x: 308, y: 116, color: "#ffdd44" },
            l:   { x: 308, y: 136, color: "#ffaa22" },
            pwr: { x: 308, y: 156, color: "#22cc44" },
        })

        // Header pads sit on a regular 22px-pitch grid starting at
        // cx=45, with the top row at cy=40 and the bottom row at
        // cy=215.
        readonly property real padTopY: 40
        readonly property real padBotY: 215
        readonly property real padStartX: 45
        readonly property real padPitch: 22

        readonly property var topRow: [
            { lbl: "D12", port: "PB4" }, { lbl: "D11", port: "PB3" }, { lbl: "D10", port: "PB2" },
            { lbl: "D9",  port: "PB1" }, { lbl: "D8",  port: "PB0" }, { lbl: "D7",  port: "PD7" },
            { lbl: "D6",  port: "PD6" }, { lbl: "D5",  port: "PD5" }, { lbl: "D4",  port: "PD4" },
            { lbl: "D3",  port: "PD3" }, { lbl: "D2",  port: "PD2" }, { lbl: "GND", port: ""    },
            { lbl: "RST", port: ""    }, { lbl: "RX0", port: "PD0" }, { lbl: "TX1", port: "PD1" }
        ]
        readonly property var bottomRow: [
            { lbl: "D13",  port: "PB5" }, { lbl: "3V3",  port: ""    }, { lbl: "AREF", port: ""    },
            { lbl: "A0",   port: "PC0" }, { lbl: "A1",   port: "PC1" }, { lbl: "A2",   port: "PC2" },
            { lbl: "A3",   port: "PC3" }, { lbl: "A4",   port: "PC4" }, { lbl: "A5",   port: "PC5" },
            { lbl: "A6",   port: ""    }, { lbl: "A7",   port: ""    }, { lbl: "5V",   port: ""    },
            { lbl: "RST",  port: ""    }, { lbl: "GND",  port: ""    }, { lbl: "VIN",  port: ""    }
        ]

        // ---- LED overlays ---------------------------------------
        Repeater {
            model: [
                { key: "tx",  brightness: root.txGlow },
                { key: "rx",  brightness: root.rxGlow },
                { key: "l",   brightness: Math.max(root.level("PB5"), root.duty("PD6")) },
                { key: "pwr", brightness: root.power ? 1.0 : 0.0 },
            ]
            Item {
                property var cfg: stage.leds[modelData.key]
                // SVG LED rect is 6 wide x 12 tall; center it.
                x: cfg.x - 3; y: cfg.y - 6
                width: 6; height: 12
                // Glow halo.
                Rectangle {
                    anchors.centerIn: parent
                    width: 22; height: 22; radius: 11
                    color: parent.cfg.color
                    opacity: 0.45 * modelData.brightness
                    visible: modelData.brightness > 0.05
                }
                // Tinted SMD body — only visible while bright.
                Rectangle {
                    anchors.fill: parent
                    radius: 1
                    color: parent.cfg.color
                    opacity: modelData.brightness
                    visible: modelData.brightness > 0.05
                }
                Behavior on opacity { NumberAnimation { duration: 60 } }
            }
        }

        // ---- Header pin-HIGH overlays ----------------------------
        Repeater {
            model: stage.topRow
            PadOverlay {
                x: stage.padStartX + index * stage.padPitch - 3
                y: stage.padTopY - 3
                pinPort: modelData.port
            }
        }
        Repeater {
            model: stage.bottomRow
            PadOverlay {
                x: stage.padStartX + index * stage.padPitch - 3
                y: stage.padBotY - 3
                pinPort: modelData.port
            }
        }
    }

    component PadOverlay: Item {
        id: po
        property string pinPort: ""
        width: 6; height: 6

        function _level() {
            if (!po.pinPort) return 0
            return Math.max(root.level(po.pinPort), root.duty(po.pinPort))
        }

        // Halo around the pad.
        Rectangle {
            anchors.centerIn: parent
            width: 12; height: 12; radius: 6
            color: "#ffd24a"
            opacity: po._level() * 0.5
            visible: po._level() > 0.05
        }
        // Bright dot at the pad center.
        Rectangle {
            anchors.fill: parent
            radius: 3
            color: "#ffd24a"
            opacity: po._level() * 0.95
            visible: po._level() > 0.05
            Behavior on opacity { NumberAnimation { duration: 60 } }
        }
    }
}
