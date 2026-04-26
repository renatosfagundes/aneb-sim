// ArduinoNano.qml — PNG base + JSON-driven overlay coordinates.
//
// The static art comes from `qml_assets/arduino.png`. The position
// of every LED / header pad comes from `qml_assets/arduino-coords.json`
// (produced by `scripts/calibrate-nano.py`). The bridge reads the JSON
// at startup and exposes it as `bridge.nanoCoords` — the QML below
// just looks each item up by name.
//
// All overlay coordinates are normalised (0..1) to image dimensions,
// so the layout stays correct at any rendered size.
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

    // Bridge-provided coordinate dict. Empty until calibration is done.
    readonly property var coords: (typeof bridge !== "undefined" && bridge && bridge.nanoCoords) ? bridge.nanoCoords : ({})

    // Pad-id -> AVR port mapping (which pin's HIGH state lights this pad).
    // Only digital pins are listed; power / GND / RST entries are absent.
    readonly property var padPort: ({
        "top.d12": "PB4", "top.d11": "PB3", "top.d10": "PB2",
        "top.d9":  "PB1", "top.d8":  "PB0", "top.d7":  "PD7",
        "top.d6":  "PD6", "top.d5":  "PD5", "top.d4":  "PD4",
        "top.d3":  "PD3", "top.d2":  "PD2",
        "top.rx0": "PD0", "top.tx1": "PD1",
        "bot.d13": "PB5",
        "bot.a0":  "PC0", "bot.a1":  "PC1", "bot.a2":  "PC2",
        "bot.a3":  "PC3", "bot.a4":  "PC4", "bot.a5":  "PC5"
    })

    implicitWidth:  710
    implicitHeight: 351

    // ---- The PNG, aspect-fit into the widget ---------------------
    Image {
        id: nano
        anchors.fill: parent
        source: "../qml_assets/arduino.png"
        fillMode: Image.PreserveAspectFit
        smooth: true
        antialiasing: true
        sourceSize.width:  710 * 2     // crisp at high DPI
        sourceSize.height: 351 * 2
    }

    // The actual rendered area of the PNG inside the widget. With
    // PreserveAspectFit, the image is letterboxed — overlays must
    // anchor to this area, not the full widget bounds.
    readonly property real _imgScale: Math.min(width / 710, height / 351)
    readonly property real _imgW: 710 * _imgScale
    readonly property real _imgH: 351 * _imgScale
    readonly property real _imgX: (width  - _imgW) / 2
    readonly property real _imgY: (height - _imgH) / 2

    // ---- LED overlays --------------------------------------------
    Repeater {
        model: [
            { key: "tx",  brightness: root.txGlow,                        color: "#ff4444" },
            { key: "rx",  brightness: root.rxGlow,                        color: "#ffdd44" },
            { key: "l",   brightness: Math.max(root.level("PB5"), root.duty("PD6")), color: "#ffaa22" },
            { key: "pwr", brightness: root.power ? 1.0 : 0.0,             color: "#22cc44" }
        ]
        OnBoardLedOverlay {
            property var entry: (root.coords.leds && root.coords.leds[modelData.key])
                                ? root.coords.leds[modelData.key] : null
            xNorm: entry ? entry.x   : -1
            yNorm: entry ? entry.y   : -1
            wNorm: entry ? (entry.w !== undefined ? entry.w : 0.014) : 0.014
            hNorm: entry ? (entry.h !== undefined ? entry.h : 0.018) : 0.018
            rotation: entry ? (entry.rot !== undefined ? entry.rot : 0) : 0
            brightness: modelData.brightness
            color: modelData.color
        }
    }

    // ---- Header pad overlays -------------------------------------
    Repeater {
        model: Object.keys(root.padPort)
        PadOverlay {
            property var parts: modelData.split(".")
            property var entry: (root.coords.pads
                                 && root.coords.pads[parts[0]]
                                 && root.coords.pads[parts[0]][parts[1]])
                               ? root.coords.pads[parts[0]][parts[1]] : null
            xNorm: entry ? entry.x : -1
            yNorm: entry ? entry.y : -1
            rNorm: entry ? (entry.r !== undefined ? entry.r : 0.006) : 0.006
            pinPort: root.padPort[modelData]
        }
    }

    // ---- Components ----------------------------------------------
    component OnBoardLedOverlay: Item {
        id: lo
        property real xNorm: -1
        property real yNorm: -1
        // Body size in normalised image coords (separate from the
        // outer Item's width/height so we can compute pixels here).
        property real wNorm: 0.014
        property real hNorm: 0.018
        property real brightness: 0
        property color color: "#ffaa22"

        visible: xNorm >= 0 && yNorm >= 0

        // Anchor at the rendered image rect, not the widget bounds.
        // Width / height come from wNorm / hNorm × the rendered image.
        x: root._imgX + xNorm * root._imgW - width  / 2
        y: root._imgY + yNorm * root._imgH - height / 2
        width:  Math.max(2, wNorm * root._imgW)
        height: Math.max(2, hNorm * root._imgH)
        transformOrigin: Item.Center

        // Halo around the LED.
        Rectangle {
            anchors.centerIn: parent
            width:  parent.width  * 2.4
            height: parent.height * 2.0
            radius: Math.min(width, height) / 2
            color: lo.color
            opacity: 0.5 * lo.brightness
            visible: lo.brightness > 0.05
        }
        // SMD body — rounded-rectangle.
        Rectangle {
            anchors.fill: parent
            radius: 1.5
            color: lo.color
            opacity: lo.brightness
            visible: lo.brightness > 0.05
            border.color: "#1a1a1a"; border.width: 1
            Rectangle {
                anchors.top: parent.top; anchors.left: parent.left
                anchors.topMargin:  parent.height * 0.10
                anchors.leftMargin: parent.width  * 0.18
                width:  parent.width  * 0.40
                height: parent.height * 0.18
                radius: 1
                color: "white"
                opacity: lo.brightness * 0.6
            }
        }
        Behavior on opacity { NumberAnimation { duration: 60 } }
    }

    component PadOverlay: Item {
        id: po
        property real xNorm: -1
        property real yNorm: -1
        property real rNorm: 0.006
        property string pinPort: ""

        visible: xNorm >= 0 && yNorm >= 0

        function _level() {
            if (!po.pinPort) return 0
            return Math.max(root.level(po.pinPort), root.duty(po.pinPort))
        }

        x: root._imgX + xNorm * root._imgW - width  / 2
        y: root._imgY + yNorm * root._imgH - height / 2
        width:  Math.max(3, rNorm * root._imgW * 2)
        height: width

        // Halo.
        Rectangle {
            anchors.centerIn: parent
            width:  parent.width  * 1.8
            height: parent.height * 1.8
            radius: width / 2
            color: "#ffd24a"
            opacity: po._level() * 0.5
            visible: po._level() > 0.05
        }
        // Bright dot.
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "#ffd24a"
            opacity: po._level() * 0.95
            visible: po._level() > 0.05
            Behavior on opacity { NumberAnimation { duration: 60 } }
        }
    }
}
