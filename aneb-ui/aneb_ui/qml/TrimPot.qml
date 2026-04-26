// TrimPot.qml — rotary blue trim pot using the rendered asset.
//
// The widget is the source of truth for the ADC value (the firmware reads
// what we set). Drag vertically to rotate; up = increase, down = decrease.
// Range maps -135° (min, lower-left) -> +135° (max, lower-right) over
// 0..1023.
import QtQuick 2.15

Item {
    id: root

    property string chip:    ""        // e.g. "ecu1"
    property int    channel: 0          // ADC channel 0..7
    property string label:   "AIN0"
    property int    adcValue: 0         // 0..1023 — internal state
    property int    minValue: 0
    property int    maxValue: 1023

    implicitWidth:  86
    implicitHeight: 110

    // The brass slot rotates from -135° to +135° as adcValue sweeps 0..1023.
    readonly property real _angle: -135 + (adcValue / 1023.0) * 270

    Image {
        id: knob
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width
        height: parent.width                       // square aspect
        source: "../qml_assets/trimpot.png"
        rotation: root._angle
        smooth: true
        antialiasing: true
        Behavior on rotation { NumberAnimation { duration: 60 } }
    }

    Text {
        id: valueText
        anchors.top: knob.bottom
        anchors.topMargin: 2
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.adcValue
        color: "#102018"
        font.family: "Consolas"
        font.pixelSize: 11
        font.bold: true
    }
    Text {
        anchors.top: valueText.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.label
        color: "#3a4a40"
        font.pixelSize: 9
    }

    MouseArea {
        anchors.fill: knob
        cursorShape: Qt.SizeVerCursor
        property real startY: 0
        property int  startVal: 0

        onPressed: function(evt) {
            startY = evt.y
            startVal = root.adcValue
        }
        onPositionChanged: function(evt) {
            if (!pressed) return
            var dy = startY - evt.y
            var newV = startVal + dy * 6   // 200 px drag ≈ full sweep
            if (newV < root.minValue) newV = root.minValue
            if (newV > root.maxValue) newV = root.maxValue
            if (newV !== root.adcValue) {
                root.adcValue = newV
                bridge.setAdc(root.chip, root.channel, newV)
            }
        }
        onWheel: function(evt) {
            var step = (evt.angleDelta.y > 0 ? 32 : -32)
            var newV = root.adcValue + step
            if (newV < root.minValue) newV = root.minValue
            if (newV > root.maxValue) newV = root.maxValue
            root.adcValue = newV
            bridge.setAdc(root.chip, root.channel, newV)
        }
    }
}
