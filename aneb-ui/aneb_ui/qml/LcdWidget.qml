// LcdWidget.qml — 16x2 character LCD per chip (HD44780-style).
//
// Driven from `bridge.lcdLines[chip]` which is now populated from the
// engine's `lcd` event (PCF8574 backpack at I2C 0x27). Visually mimics
// a classic 1602 module: dark plastic bezel, deep blue glass with a
// bright cyan glow, and pixelated white characters in monospace.
import QtQuick 2.15

Item {
    id: root
    property string chip: ""

    implicitWidth:  208
    implicitHeight: 44

    function lineFor(row) {
        if (!bridge || !bridge.lcdLines) return ""
        var chipLines = bridge.lcdLines[root.chip]
        if (!chipLines || chipLines.length <= row) return ""
        var s = chipLines[row]
        while (s.length < 16) s += " "
        return s.substring(0, 16)
    }

    // ---- Bezel — the LCD module's plastic frame ----------------
    Rectangle {
        anchors.fill: parent
        radius: 4
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#1a1f2a" }
            GradientStop { position: 1.0; color: "#0a0d14" }
        }
        border.color: "#000"; border.width: 1
    }

    // Tiny mounting-hole accents at the corners — sells the
    // "physical module" look without taking real space.
    Repeater {
        model: [
            { x: 0.02, y: 0.10 },
            { x: 0.98, y: 0.10 },
            { x: 0.02, y: 0.90 },
            { x: 0.98, y: 0.90 }
        ]
        Rectangle {
            x: root.width  * modelData.x - 2
            y: root.height * modelData.y - 2
            width: 4; height: 4
            radius: 2
            color: "#04060a"
            border.color: "#3a4256"; border.width: 1
        }
    }

    // ---- LCD glass --------------------------------------------
    Rectangle {
        id: glass
        anchors.fill: parent
        anchors.margins: 5
        anchors.leftMargin:  10
        anchors.rightMargin: 10
        radius: 1
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#2a4ec8" }
            GradientStop { position: 1.0; color: "#1230a0" }
        }
        border.color: "#000"; border.width: 1

        // Soft cyan top-edge glow — backlight bleed.
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: parent.height * 0.35
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0.55, 0.75, 1.0, 0.25) }
                GradientStop { position: 1.0; color: Qt.rgba(0.55, 0.75, 1.0, 0.0) }
            }
        }

        Column {
            anchors.fill: parent
            anchors.leftMargin: 5
            anchors.rightMargin: 5
            anchors.topMargin: 2
            spacing: 0

            Text {
                text: root.lineFor(0)
                color: "#ffffff"
                font.family: "Consolas"
                font.pixelSize: Math.max(10, glass.height * 0.40)
                font.bold: true
                font.letterSpacing: 1.5
                width: parent.width
                horizontalAlignment: Text.AlignLeft
                style: Text.Raised
                styleColor: Qt.rgba(0.7, 0.85, 1.0, 0.4)
            }
            Text {
                text: root.lineFor(1)
                color: "#ffffff"
                font.family: "Consolas"
                font.pixelSize: Math.max(10, glass.height * 0.40)
                font.bold: true
                font.letterSpacing: 1.5
                width: parent.width
                horizontalAlignment: Text.AlignLeft
                style: Text.Raised
                styleColor: Qt.rgba(0.7, 0.85, 1.0, 0.4)
            }
        }

        // Subtle vignette to add depth around the edges.
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: Qt.rgba(0, 0, 0, 0.4)
            border.width: 1
        }
    }

    // Faint label below the bezel.
    Text {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: -10
        anchors.right: parent.right
        anchors.rightMargin: 4
        text: "16x2"
        color: "#5a7a64"
        font.family: "Consolas"
        font.pixelSize: 7
    }
}
