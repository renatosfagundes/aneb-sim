// ArduinoNano.qml — top-down Nano illustration with live pin-state dots.
//
// The pre-rendered arduino.png shows the board with header pads. We
// overlay small colored dots at each pin's pixel location to indicate
// the live logic level driven by the firmware.
import QtQuick 2.15

Item {
    id: root
    property string chip: ""

    // Aspect ratio of the source image; preserved as the widget grows.
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

    // Helper: read a pin's logic level from the bridge (0/1).
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

    // Header pin coordinates as fractions of the image size. The Nano
    // top-down render places a row of pads near the top edge and another
    // near the bottom edge. These positions were calibrated against the
    // current asset; if the image is replaced, retune.
    readonly property real pinDotR: Math.max(2, root.width * 0.011)

    // Bottom row, left-to-right matches the silkscreen on the asset:
    //   D13 3V3 AREF A0 A1 A2 A3 A4 A5 A6 A7 5V RST GND VIN
    // We only paint dots for the pins backed by an AVR port.
    readonly property var bottomMap: ({
        "PB5":  0.06,    // D13
        "PC0":  0.225,   // A0
        "PC1":  0.275,   // A1
        "PC2":  0.325,   // A2
        "PC3":  0.375,   // A3
        "PC4":  0.425,   // A4
        "PC5":  0.475,   // A5
    })
    // Top row: D12 D11 D10 D9 D8 D7 D6 D5 D4 D3 D2 GND RST RX0 TX1
    readonly property var topMap: ({
        "PB4":  0.06,    // D12
        "PB3":  0.115,   // D11
        "PB2":  0.17,    // D10
        "PB1":  0.225,   // D9
        "PB0":  0.275,   // D8
        "PD7":  0.325,   // D7
        "PD6":  0.375,   // D6 (LDR_LED PWM)
        "PD5":  0.425,   // D5 (LOOP PWM)
        "PD4":  0.475,   // D4 (DOUT1)
        "PD3":  0.525,   // D3 (DOUT0)
        "PD2":  0.58,    // D2 (CAN INT)
        "PD0":  0.755,   // RX0
        "PD1":  0.81,    // TX1
    })

    // Top header dots.
    Repeater {
        model: Object.keys(root.topMap)
        Rectangle {
            property string portPin: modelData
            property real lvl: Math.max(root.level(portPin), root.duty(portPin))
            width: root.pinDotR * 2
            height: width
            radius: width / 2
            x: root.width  * root.topMap[portPin] - width/2
            y: root.height * 0.085 - height/2
            color: "#ffd24a"
            opacity: lvl > 0 ? 0.85 * (lvl < 1 ? lvl : 1) : 0
            visible: lvl > 0
            Behavior on opacity { NumberAnimation { duration: 80 } }
        }
    }
    // Bottom header dots.
    Repeater {
        model: Object.keys(root.bottomMap)
        Rectangle {
            property string portPin: modelData
            property real lvl: Math.max(root.level(portPin), root.duty(portPin))
            width: root.pinDotR * 2
            height: width
            radius: width / 2
            x: root.width  * root.bottomMap[portPin] - width/2
            y: root.height * 0.918 - height/2
            color: "#ffd24a"
            opacity: lvl > 0 ? 0.85 * (lvl < 1 ? lvl : 1) : 0
            visible: lvl > 0
            Behavior on opacity { NumberAnimation { duration: 80 } }
        }
    }

    // Live "L" LED — D13 = PB5. Sits next to the chip in the asset.
    Rectangle {
        x: root.width  * 0.66
        y: root.height * 0.32
        width: root.pinDotR * 2.2
        height: width
        radius: width / 2
        color: "#ffaa22"
        opacity: root.level("PB5") ? 1 : 0
        visible: opacity > 0.05
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 2.2
            height: parent.height * 2.2
            radius: width / 2
            color: "#ffaa22"
            opacity: 0.4 * parent.opacity
        }
        Behavior on opacity { NumberAnimation { duration: 80 } }
    }
}
