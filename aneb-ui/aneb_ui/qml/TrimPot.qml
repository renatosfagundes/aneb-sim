// TrimPot.qml — circular trim pot drawn entirely in QML primitives.
//
// Body is a static blue circle with a brass center disc; only the
// dark slot inside the brass rotates to indicate the value. Avoids
// the diamond-corner problem that hits when you rotate a square asset
// around its center, and removes the asset-image dependency.
import QtQuick 2.15

Item {
    id: root

    property string chip:    ""
    property int    channel: 0
    property string label:   "AIN0"
    property int    adcValue: 0           // 0..1023 — internal state
    property int    minValue: 0
    property int    maxValue: 1023

    implicitWidth:  50
    implicitHeight: 78

    readonly property real _angle: -135 + (adcValue / 1023.0) * 270

    // ---- Body (blue plastic) --------------------------------------
    Rectangle {
        id: body
        width: parent.width
        height: parent.width
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        radius: width / 2

        gradient: Gradient {
            GradientStop { position: 0.0; color: "#3d8fc4" }
            GradientStop { position: 1.0; color: "#114a78" }
        }
        border.color: "#082030"; border.width: 1

        // Brass center disc.
        Rectangle {
            anchors.centerIn: parent
            width:  parent.width  * 0.56
            height: parent.height * 0.56
            radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#e8c878" }
                GradientStop { position: 1.0; color: "#806030" }
            }
            border.color: "#403018"; border.width: 1

            // Rotating slot — the only thing that moves with the value.
            Rectangle {
                anchors.centerIn: parent
                width:  parent.width  * 0.78
                height: parent.height * 0.18
                radius: 1
                color: "#1a1a1a"
                rotation: root._angle
                transformOrigin: Item.Center
                Behavior on rotation { NumberAnimation { duration: 60 } }
            }
        }
    }

    // ---- Labels ---------------------------------------------------
    Text {
        anchors.top: body.bottom
        anchors.topMargin: 1
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.adcValue
        color: "#cdfac0"
        font.family: "Consolas"
        font.pixelSize: 10
        font.bold: true
    }
    Text {
        anchors.top: body.bottom
        anchors.topMargin: 13
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.label
        color: "#a8d0b0"
        font.pixelSize: 8
    }

    // ---- Input ----------------------------------------------------
    MouseArea {
        anchors.fill: body
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
