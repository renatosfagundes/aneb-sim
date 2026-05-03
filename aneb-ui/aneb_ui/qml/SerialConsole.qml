// SerialConsole.qml — terminal showing the chip's UART output, with an
// input box that pushes typed text back via the bridge.  Wraps a
// PaneHeader on top, a TextArea in the middle, and a styled input row
// at the bottom.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "Theme.js" as T

Item {
    id: root
    property string chip:  ""
    property string label: ""

    // Hard cap on retained terminal text.  TextArea.insert at length is
    // O(1); trimming is O(n) so we only do it when length crosses the
    // high-water mark, dropping back to maxChars in one shot.
    readonly property int maxChars:   200000
    readonly property int trimSlack:   50000

    // Rolling byte counter for the header subtitle.  Bridges in/out
    // counts as "RX" — for now we don't separately track the user's
    // typed input.
    property int bytesRx: 0

    implicitHeight: 240

    // ---- background ------------------------------------------------
    Rectangle {
        anchors.fill: parent
        color: T.bg
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- header ------------------------------------------------
        PaneHeader {
            id: header
            Layout.fillWidth: true
            chip:    root.chip
            label:   root.label
            title:   "Console"
            subtitle: root.bytesRx > 0
                       ? root.bytesRx + " B"
                       : "waiting for data…"
            accent:  T.info

            StatusDot {
                id: rxDot
                baseColor: T.success
                active: false
                Timer {
                    id: rxFade
                    interval: 120
                    onTriggered: rxDot.active = false
                }
            }
            PaneButton {
                text: termText.length > 0 ? "Clear" : ""
                visible: termText.length > 0
                borderless: true
                onClicked: { termText.clear(); root.bytesRx = 0 }
            }
            PaneCheckBox {
                id: autoScroll
                text: "Follow"
                checked: true
                ToolTip.visible: hovered
                ToolTip.text: "Auto-scroll to the latest output."
                ToolTip.delay: 500
            }
        }

        // ---- terminal area ----------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: T.bgDeep
            border.color: T.borderSubtle
            border.width: 1

            ScrollView {
                id: scroll
                anchors.fill: parent
                anchors.margins: 1
                clip: true

                TextArea {
                    id: termText
                    readOnly: true
                    wrapMode: TextArea.NoWrap
                    color: T.text
                    selectionColor: T.accent
                    selectedTextColor: T.textBright
                    selectByMouse: true
                    font.family: T.monoFamily
                    font.pixelSize: T.fontBody
                    background: null
                    leftPadding: 8
                    rightPadding: 8
                    topPadding: 6
                    bottomPadding: 6
                    text: ""
                    placeholderText: "(no UART output yet)"
                    placeholderTextColor: T.textDim
                }
            }
        }

        // ---- input footer -----------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 42
            color: T.bgRaised
            border.color: T.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: T.pad
                anchors.rightMargin: T.padSmall
                anchors.topMargin: T.padSmall
                anchors.bottomMargin: T.padSmall
                spacing: T.padSmall

                PaneTextField {
                    id: inputField
                    Layout.fillWidth: true
                    placeholderText: "send to " + root.chip.toUpperCase() + " UART  (Enter)"
                    onAccepted: root._send()
                }
                PaneButton {
                    text: "Send"
                    enabled: inputField.text.length > 0
                    onClicked: root._send()
                }
            }

            // Top edge of footer doubles as the divider.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: T.borderSubtle
            }
        }
    }

    // ---- behavior --------------------------------------------------
    function _send() {
        if (inputField.text.length === 0) return
        bridge.sendUart(root.chip, inputField.text + "\n")
        inputField.text = ""
    }

    Connections {
        target: bridge
        function onUartAppended(chip, data) {
            if (chip !== root.chip) return
            // O(1) append — TextArea.insert at length doesn't rebuild
            // the document the way `text += data` would.
            termText.insert(termText.length, data)
            if (termText.length > root.maxChars + root.trimSlack) {
                termText.remove(0, termText.length - root.maxChars)
            }
            root.bytesRx += data.length
            // Activity blink.
            rxDot.active = true
            rxFade.restart()
            if (autoScroll.checked) {
                termText.cursorPosition = termText.length
            }
        }
    }
}
