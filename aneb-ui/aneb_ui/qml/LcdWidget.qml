// LcdWidget.qml — 16x2 character LCD per chip.
//
// Driven from `bridge.lcdLines[chip]` which is populated by the bridge
// when it sees `__LCD__<row>:<col>:<text>\n` lines in any chip's UART.
//
// Firmware example:
//     Serial.println("__LCD__0:0:Hello, World!");
//     Serial.println("__LCD__1:0:T = 23.4 C");
import QtQuick 2.15

Item {
    id: root
    property string chip: ""

    implicitWidth:  208
    implicitHeight: 44

    // ---- Helper -------------------------------------------------
    function lineFor(row) {
        if (!bridge || !bridge.lcdLines) return ""
        var chipLines = bridge.lcdLines[root.chip]
        if (!chipLines || chipLines.length <= row) return ""
        var s = chipLines[row]
        // Pad to 16 chars so the cursor position stays visible.
        while (s.length < 16) s += " "
        return s.substring(0, 16)
    }

    // ---- Bezel (the LCD module's plastic border) ---------------
    Rectangle {
        anchors.fill: parent
        radius: 4
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#384a40" }
            GradientStop { position: 1.0; color: "#1a2820" }
        }
        border.color: "#0a0f0c"; border.width: 1
    }

    // ---- The actual LCD glass ----------------------------------
    Rectangle {
        id: glass
        anchors.fill: parent
        anchors.margins: 4
        radius: 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0e3826" }
            GradientStop { position: 1.0; color: "#072115" }
        }
        border.color: "#000"; border.width: 1

        // Subtle scanlines for an LCD feel.
        Repeater {
            model: 8
            Rectangle {
                width: parent.width; height: 1
                y: index * (parent.height / 8) + 1
                color: "#0a3020"
                opacity: 0.18
            }
        }

        Column {
            anchors.fill: parent
            anchors.margins: 3
            spacing: 0

            Text {
                text: root.lineFor(0)
                color: "#7fff7f"
                font.family: "Consolas"
                font.pixelSize: 13
                font.bold: true
                font.letterSpacing: 1.2
                width: parent.width
                horizontalAlignment: Text.AlignLeft
                Behavior on text { PropertyAnimation { duration: 0 } }
            }
            Text {
                text: root.lineFor(1)
                color: "#7fff7f"
                font.family: "Consolas"
                font.pixelSize: 13
                font.bold: true
                font.letterSpacing: 1.2
                width: parent.width
                horizontalAlignment: Text.AlignLeft
            }
        }

        // Soft glow over the text.
        Rectangle {
            anchors.fill: parent
            color: "#7fff7f"
            opacity: 0.04
            radius: parent.radius
        }
    }

    // Faint label below the bezel — "LCD 16x2" — so it's obvious what
    // this widget is even before any text appears.
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
