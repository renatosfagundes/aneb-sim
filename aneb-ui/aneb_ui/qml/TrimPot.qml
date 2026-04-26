// TrimPot.qml — two-layer composition.
//
// Layer 1 (static): trimpot_body.png — blue plastic body with the
// solder pads. The center is a transparent hole where the screw sits.
// Layer 2 (rotating): trimpot_screw.png — just the brass slotted disc,
// centered, square aspect, rotated to indicate the current value.
//
// If the assets are missing, falls back to QML primitives so the UI
// still functions — the body becomes a flat blue square and the screw
// becomes a brass disc with a black slot.
import QtQuick 2.15

Item {
    id: root

    property string chip:    ""
    property int    channel: 0
    property string label:   "AIN0"
    property int    adcValue: 0
    property int    minValue: 0
    property int    maxValue: 1023

    implicitWidth:  60
    implicitHeight: 92

    readonly property real _angle: -135 + (adcValue / 1023.0) * 270

    // ---- Body (static) ---------------------------------------------
    Image {
        id: bodyImage
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width:  parent.width
        height: parent.width
        source: "../qml_assets/trimpot_body.png"
        smooth: true
        antialiasing: true
        fillMode: Image.PreserveAspectFit
    }
    // Fallback body (only shown if the asset is missing).
    Rectangle {
        anchors.fill: bodyImage
        visible: bodyImage.status !== Image.Ready
        radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#3d8fc4" }
            GradientStop { position: 1.0; color: "#114a78" }
        }
        border.color: "#082030"; border.width: 1
    }

    // ---- Rotating screw --------------------------------------------
    Image {
        id: screwImage
        anchors.centerIn: bodyImage
        width:  bodyImage.width  * 0.50
        height: bodyImage.height * 0.50
        source: "../qml_assets/trimpot_screw.png"
        smooth: true
        antialiasing: true
        fillMode: Image.PreserveAspectFit
        rotation: root._angle
        Behavior on rotation { NumberAnimation { duration: 60 } }
    }
    // Fallback screw (only shown if asset missing).
    Item {
        anchors.fill: screwImage
        visible: screwImage.status !== Image.Ready
        rotation: root._angle
        Behavior on rotation { NumberAnimation { duration: 60 } }
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#e8c878" }
                GradientStop { position: 1.0; color: "#806030" }
            }
            border.color: "#403018"; border.width: 1
        }
        Rectangle {
            anchors.centerIn: parent
            width:  parent.width * 0.78
            height: parent.height * 0.18
            radius: 1
            color: "#1a1a1a"
        }
    }

    // ---- Labels -----------------------------------------------------
    Text {
        anchors.top: bodyImage.bottom
        anchors.topMargin: 2
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.adcValue
        color: "#cdfac0"
        font.family: "Consolas"
        font.pixelSize: 11
        font.bold: true
    }
    Text {
        anchors.top: bodyImage.bottom
        anchors.topMargin: 16
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.label
        color: "#a8d0b0"
        font.pixelSize: 9
    }

    // ---- Input ------------------------------------------------------
    MouseArea {
        anchors.fill: bodyImage
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
            var newV = startVal + dy * 6
            if (newV < root.minValue) newV = root.minValue
            if (newV > root.maxValue) newV = root.maxValue
            if (newV !== root.adcValue) {
                root.adcValue = newV
                if (typeof bridge !== "undefined" && bridge) {
                    bridge.setAdc(root.chip, root.channel, newV)
                }
            }
        }
        onWheel: function(evt) {
            var step = (evt.angleDelta.y > 0 ? 32 : -32)
            var newV = root.adcValue + step
            if (newV < root.minValue) newV = root.minValue
            if (newV > root.maxValue) newV = root.maxValue
            root.adcValue = newV
            if (typeof bridge !== "undefined" && bridge) {
                bridge.setAdc(root.chip, root.channel, newV)
            }
        }
    }
}
