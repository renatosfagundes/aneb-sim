// ArduinoNano.qml — top-down Nano illustration with live pin-state
// dots on the headers AND animated on-board status LEDs
// (PWR / L / TX / RX) overlaid where the asset shows them.
import QtQuick 2.15

Item {
    id: root

    property string chip: ""
    // Set by the parent panel from bridge.engineRunning so PWR comes
    // on only when the engine is alive. (Real Nanos light PWR from
    // VCC; "engine running" is the closest analog in our model.)
    property bool   power: false

    implicitWidth:  420
    implicitHeight: 160

    Image {
        id: nano
        anchors.fill: parent
        source: "../qml_assets/arduino.png"
        fillMode: Image.PreserveAspectFit
        smooth: true
        antialiasing: true
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

    // ---- TX/RX pulse helpers (called by parent panel on UART events).
    property real txGlow: 0.0
    property real rxGlow: 0.0
    function pulseTx() { txGlow = 1.0; txDecay.restart() }
    function pulseRx() { rxGlow = 1.0; rxDecay.restart() }
    Timer {
        id: txDecay; interval: 40; repeat: true; running: false
        onTriggered: { root.txGlow *= 0.6; if (root.txGlow < 0.05) { root.txGlow = 0; running = false } }
    }
    Timer {
        id: rxDecay; interval: 40; repeat: true; running: false
        onTriggered: { root.rxGlow *= 0.6; if (root.rxGlow < 0.05) { root.rxGlow = 0; running = false } }
    }

    // ---- Header pin overlays (live HIGH dots) ---------------------
    readonly property real pinDotR: Math.max(2, root.width * 0.011)

    readonly property var bottomMap: ({
        "PB5":  0.06,    // D13
        "PC0":  0.225,   // A0
        "PC1":  0.275,   // A1
        "PC2":  0.325,   // A2
        "PC3":  0.375,   // A3
        "PC4":  0.425,   // A4
        "PC5":  0.475,   // A5
    })
    readonly property var topMap: ({
        "PB4":  0.06, "PB3":  0.115, "PB2":  0.17, "PB1":  0.225,
        "PB0":  0.275, "PD7": 0.325, "PD6": 0.375, "PD5": 0.425,
        "PD4":  0.475, "PD3": 0.525, "PD2": 0.58,
        "PD0":  0.755, "PD1": 0.81,
    })

    Repeater {
        model: Object.keys(root.topMap)
        Rectangle {
            property string portPin: modelData
            property real lvl: Math.max(root.level(portPin), root.duty(portPin))
            width: root.pinDotR * 2; height: width; radius: width / 2
            x: root.width  * root.topMap[portPin] - width/2
            y: root.height * 0.085 - height/2
            color: "#ffd24a"
            opacity: lvl > 0 ? 0.85 * (lvl < 1 ? lvl : 1) : 0
            Behavior on opacity { NumberAnimation { duration: 80 } }
        }
    }
    Repeater {
        model: Object.keys(root.bottomMap)
        Rectangle {
            property string portPin: modelData
            property real lvl: Math.max(root.level(portPin), root.duty(portPin))
            width: root.pinDotR * 2; height: width; radius: width / 2
            x: root.width  * root.bottomMap[portPin] - width/2
            y: root.height * 0.918 - height/2
            color: "#ffd24a"
            opacity: lvl > 0 ? 0.85 * (lvl < 1 ? lvl : 1) : 0
            Behavior on opacity { NumberAnimation { duration: 80 } }
        }
    }

    // ---- Component: a small on-board status LED with a glow halo --
    component OnBoardLed: Item {
        id: lc
        property color color: "#ffaa22"
        property real  brightness: 0.0      // 0..1
        property string label: ""
        width: Math.max(4, root.width * 0.013)
        height: width
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 2.6; height: parent.height * 2.6
            radius: width / 2
            color: lc.color
            opacity: 0.45 * lc.brightness
            visible: lc.brightness > 0.05
        }
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            border.color: "#0a0a0a"; border.width: 0.5
            color: lc.brightness > 0.05 ? lc.color : "#161616"
            // Specular dot when on.
            Rectangle {
                anchors.top: parent.top; anchors.left: parent.left
                anchors.topMargin: parent.height * 0.18
                anchors.leftMargin: parent.width * 0.18
                width: parent.width * 0.32; height: parent.height * 0.32
                radius: width / 2
                color: "white"
                opacity: lc.brightness * 0.7
                visible: lc.brightness > 0.05
            }
        }
        Behavior on brightness { NumberAnimation { duration: 80 } }
    }

    // On-board LEDs cluster — to the right of the chip on the asset.
    // Coordinates are calibrated against the current arduino.png; if
    // the asset is replaced the .x/.y fractions may need a retune.
    OnBoardLed {                 // PWR — green, lit when engine running
        x: root.width  * 0.685; y: root.height * 0.42
        color: "#22cc44"
        brightness: root.power ? 1.0 : 0.0
    }
    OnBoardLed {                 // L (D13) — orange, tied to PB5 / PD6 PWM
        x: root.width  * 0.685; y: root.height * 0.50
        color: "#ffaa22"
        brightness: Math.max(root.level("PB5"), root.duty("PD6"))
    }
    OnBoardLed {                 // TX — red, flashes on UART output
        x: root.width  * 0.685; y: root.height * 0.34
        color: "#ff5544"
        brightness: root.txGlow
    }
    OnBoardLed {                 // RX — yellow, flashes on UART input
        x: root.width  * 0.685; y: root.height * 0.58
        color: "#ffe044"
        brightness: root.rxGlow
    }
}
