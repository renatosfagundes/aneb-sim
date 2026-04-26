// SerialConsole.qml — terminal showing the chip's UART output, with an
// input box that pushes typed text back via the bridge.
import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root
    property string chip: ""

    implicitHeight: 120

    Rectangle {
        anchors.fill: parent
        color: "#061410"
        border.color: "#3e6b4d"
        border.width: 1
        radius: 3
    }

    Column {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 2

        ScrollView {
            id: scrollArea
            width: parent.width
            height: parent.height - 28
            clip: true

            TextArea {
                id: termText
                readOnly: true
                wrapMode: TextArea.NoWrap
                color: "#cdfac0"
                font.family: "Consolas"
                font.pixelSize: 11
                background: null
                text: ""
            }
        }

        Row {
            spacing: 4
            width: parent.width
            TextField {
                id: inputField
                width: parent.width - sendBtn.width - 4
                placeholderText: "send to " + root.chip + " UART"
                color: "#cdfac0"
                font.family: "Consolas"
                font.pixelSize: 11
                background: Rectangle { color: "#0a1a14"; border.color: "#3e6b4d"; radius: 2 }
                onAccepted: root._send()
            }
            Button {
                id: sendBtn
                text: "Send"
                onClicked: root._send()
            }
        }
    }

    function _send() {
        if (inputField.text.length === 0) return
        bridge.sendUart(root.chip, inputField.text + "\n")
        inputField.text = ""
    }

    Connections {
        target: bridge
        function onUartAppended(chip, data) {
            if (chip !== root.chip) return
            termText.text += data
            termText.cursorPosition = termText.length
        }
    }
}
