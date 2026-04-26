// ArduinoNano.qml — top-down Arduino Nano in pure QML primitives.
//
// More board-like than previous passes: SMD component dots scattered
// around the chip, visible pin legs on the TQFP package, "Arduino
// Nano" silkscreen at the bottom edge, decoupling caps + crystal
// drawn as discrete components.
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

    implicitWidth: 420
    implicitHeight: 160

    Item {
        id: stage
        width: 420; height: 160
        anchors.centerIn: parent
        scale: Math.min(root.width / 420, root.height / 160)
        transformOrigin: Item.Center

        // --- USB Connector (silver shielding with port slot) -------
        Rectangle {
            x: 0; y: 64; width: 36; height: 32; radius: 2
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#9a9a9a" }
                GradientStop { position: 0.4; color: "#e2e2e2" }
                GradientStop { position: 1.0; color: "#7c7c7c" }
            }
            border.color: "#444"; border.width: 1
            Rectangle {
                anchors.centerIn: parent; width: 20; height: 14
                color: "#101010"; border.color: "#333"; border.width: 1
                Rectangle {                                // inner pin connector
                    anchors.centerIn: parent
                    width: 14; height: 4; color: "#3a3a3a"
                }
            }
        }

        // --- PCB Body ----------------------------------------------
        Rectangle {
            id: pcb
            x: 12; y: 8; width: 396; height: 144; radius: 6
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#0a7868" }
                GradientStop { position: 0.4; color: "#005f50" }
                GradientStop { position: 1.0; color: "#003a30" }
            }
            border.color: "#001a16"; border.width: 1

            // Faint horizontal traces.
            Rectangle { x: 30; y: 38; width: 340; height: 1; color: "#00a888"; opacity: 0.18 }
            Rectangle { x: 30; y: 102; width: 340; height: 1; color: "#00a888"; opacity: 0.18 }

            // "Arduino Nano" silkscreen (bottom-right corner).
            Text {
                anchors.bottom: parent.bottom; anchors.right: parent.right
                anchors.bottomMargin: 18; anchors.rightMargin: 16
                text: "Arduino  Nano"
                color: "#d0e0d0"
                font.family: "Consolas"; font.pixelSize: 5; font.italic: true
            }
        }

        // --- Scattered SMD components ------------------------------
        // Small surface-mount resistors / capacitors. Plain rectangles
        // suggest hardware density without needing a real schematic.
        Repeater {
            model: [
                { x: 50,  y: 38, w: 5, h: 2.4, c: "#1a1a1a" },
                { x: 70,  y: 50, w: 5, h: 2.4, c: "#b39d89" },
                { x: 95,  y: 38, w: 4, h: 2.0, c: "#1a1a1a" },
                { x: 120, y: 48, w: 5, h: 2.4, c: "#1a1a1a" },
                { x: 145, y: 38, w: 4, h: 2.0, c: "#b39d89" },
                { x: 170, y: 38, w: 4, h: 2.0, c: "#1a1a1a" },
                { x: 175, y: 110, w: 5, h: 2.4, c: "#1a1a1a" },
                { x: 200, y: 110, w: 5, h: 2.4, c: "#b39d89" },
                { x: 225, y: 110, w: 5, h: 2.4, c: "#1a1a1a" },
                { x: 250, y: 110, w: 4, h: 2.0, c: "#1a1a1a" },
                { x: 305, y: 38, w: 5, h: 2.4, c: "#1a1a1a" },
                { x: 305, y: 110, w: 4, h: 2.0, c: "#b39d89" },
            ]
            Rectangle {
                x: modelData.x; y: modelData.y
                width: modelData.w; height: modelData.h
                color: modelData.c; radius: 0.5
            }
        }

        // --- ATmega328P TQFP-32 chip -------------------------------
        Item {
            x: 178; y: 52; width: 70; height: 56

            // Faint pin legs on all four sides.
            Repeater {
                model: 8
                Rectangle {
                    x: 5 + index * 7.5; y: 1
                    width: 1.2; height: 3; color: "#3a3a3a"
                }
            }
            Repeater {
                model: 8
                Rectangle {
                    x: 5 + index * 7.5; y: 52
                    width: 1.2; height: 3; color: "#3a3a3a"
                }
            }
            Repeater {
                model: 8
                Rectangle {
                    x: 1; y: 5 + index * 5.7
                    width: 3; height: 1.2; color: "#3a3a3a"
                }
            }
            Repeater {
                model: 8
                Rectangle {
                    x: 66; y: 5 + index * 5.7
                    width: 3; height: 1.2; color: "#3a3a3a"
                }
            }

            // Chip body.
            Rectangle {
                x: 4; y: 4; width: 62; height: 48
                color: "#0a0a0a"
                border.color: "#1a1a1a"; border.width: 1
                radius: 1
                // Top-edge bevel highlight.
                Rectangle {
                    x: 1; y: 1; width: parent.width - 2; height: 2
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#3a3a3a" }
                        GradientStop { position: 1.0; color: "#0a0a0a" }
                    }
                }
                // Pin-1 dot.
                Rectangle {
                    x: 4; y: 4; width: 3.5; height: 3.5; radius: 1.75
                    color: "#444"; border.color: "#222"; border.width: 0.5
                }
                Text {
                    anchors.centerIn: parent
                    text: "ATMEGA\n328P"
                    color: "#bbb"
                    font.family: "Consolas"
                    font.pixelSize: 7
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        // --- 16 MHz crystal (silver oval can) ---------------------
        Rectangle {
            x: 260; y: 56; width: 24; height: 14; radius: 6
            border.color: "#888"; border.width: 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#dcdcdc" }
                GradientStop { position: 1.0; color: "#a0a0a0" }
            }
        }
        Text {
            x: 260; y: 70; width: 24
            text: "16M"
            color: "#d0e0d0"
            font.family: "Consolas"; font.pixelSize: 4
            horizontalAlignment: Text.AlignHCenter
        }

        // --- Decoupling caps (tan tantalum-ish blobs) -------------
        Rectangle { x: 260; y: 80; width: 10; height: 9; radius: 1; color: "#c0a890"; border.color: "#806550" }
        Rectangle { x: 274; y: 80; width: 10; height: 9; radius: 1; color: "#c0a890"; border.color: "#806550" }

        // --- Reset button ------------------------------------------
        Rectangle {
            x: 290; y: 56; width: 14; height: 14; radius: 2
            color: "#222"
            border.color: "#444"; border.width: 1
            Rectangle {
                anchors.centerIn: parent
                width: 8; height: 8; radius: 4
                color: "#0a0a0a"
            }
        }
        Text {
            x: 290; y: 70; width: 14
            text: "RST"; color: "#d0e0d0"
            font.family: "Consolas"; font.pixelSize: 4
            horizontalAlignment: Text.AlignHCenter
        }

        // --- ICSP header (6-pin grid in a black box) --------------
        Rectangle {
            x: 358; y: 60; width: 26; height: 38; radius: 2
            color: "#0a0a0a"; border.color: "#222"; border.width: 1
            Grid {
                anchors.centerIn: parent
                rows: 2; columns: 3; spacing: 3
                Repeater {
                    model: 6
                    Rectangle {
                        width: 6; height: 6; radius: 3
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#FFD700" }
                            GradientStop { position: 1; color: "#a07820" }
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            width: 3; height: 3; radius: 1.5
                            color: "#101010"
                        }
                    }
                }
            }
        }

        // --- On-board status LEDs (compact SMD strip) -------------
        Column {
            x: 322; y: 50; spacing: 5
            OnBoardLed { color: "#ff4444"; brightness: root.txGlow; label: "TX" }
            OnBoardLed { color: "#ffdd44"; brightness: root.rxGlow; label: "RX" }
            OnBoardLed { color: "#ffaa22"; brightness: Math.max(root.level("PB5"), root.duty("PD6")); label: "L" }
            OnBoardLed { color: "#22cc44"; brightness: root.power ? 1.0 : 0.0; label: "PWR" }
        }

        // --- Header rows ------------------------------------------
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

    // --- Components -----------------------------------------------
    component OnBoardLed: Item {
        id: lc
        property color color: "#ffaa22"
        property real  brightness: 0.0
        property string label: ""
        width: 28; height: 6
        Row {
            spacing: 4
            Rectangle {
                width: 6; height: 6; radius: 1
                color: lc.brightness > 0.1 ? lc.color : "#101010"
                border.color: "#000"; border.width: 0.5
                // Halo on bright.
                Rectangle {
                    anchors.centerIn: parent
                    width: 14; height: 14; radius: 7
                    color: lc.color
                    opacity: 0.35 * lc.brightness
                    visible: lc.brightness > 0.1
                    z: -1
                }
                // Specular dot.
                Rectangle {
                    anchors.centerIn: parent
                    width: 2.4; height: 2.4; radius: 1.2
                    color: "white"; opacity: lc.brightness * 0.7
                    visible: lc.brightness > 0.1
                }
            }
            Text {
                text: lc.label
                color: "#e8f0e8"
                font.family: "Consolas"
                font.pixelSize: 5; font.bold: true
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Behavior on brightness { NumberAnimation { duration: 80 } }
    }

    component HeaderPad: Item {
        id: hp
        property string label: ""
        property string pinPort: ""
        property bool   isTop: true
        width: 14; height: 24

        function _level() {
            if (!hp.pinPort) return 0
            return Math.max(root.level(hp.pinPort), root.duty(hp.pinPort))
        }

        Rectangle {
            id: pad
            x: 0; width: 14; height: 14; radius: 7
            y: hp.isTop ? 0 : 10
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#FFD700" }
                GradientStop { position: 1.0; color: "#A87814" }
            }
            border.color: "#503810"; border.width: 0.5

            // Plated through-hole.
            Rectangle {
                anchors.centerIn: parent
                width: 6; height: 6; radius: 3
                color: "#0d0d0d"
                // Live HIGH indicator.
                Rectangle {
                    anchors.centerIn: parent
                    width: 4; height: 4; radius: 2
                    color: "#ffd24a"
                    opacity: hp._level() * 0.95
                    visible: opacity > 0.05
                    Behavior on opacity { NumberAnimation { duration: 60 } }
                }
            }
            // Halo on HIGH.
            Rectangle {
                anchors.centerIn: parent
                width: 20; height: 20; radius: 10
                color: "#ffd24a"
                opacity: hp._level() * 0.4
                visible: hp._level() > 0.05
                z: -1
            }
        }

        Text {
            anchors.horizontalCenter: pad.horizontalCenter
            y: hp.isTop ? pad.bottom + 1 : pad.top - 7
            text: hp.label
            color: "#f0f5ed"
            font.pixelSize: 5
            font.family: "Consolas"
            font.bold: true
        }
    }
}
