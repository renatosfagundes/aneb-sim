// CanInject.qml — form for sending arbitrary CAN frames onto the bus.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Frame {
    id: root
    background: Rectangle { color: "#102a1c"; border.color: "#3e6b4d"; radius: 4 }

    GridLayout {
        anchors.fill: parent
        columns: 4
        rowSpacing: 4
        columnSpacing: 6

        Label { text: "ID";   color: "#cdfac0" }
        TextField {
            id: idField; text: "0x123"
            Layout.preferredWidth: 90
            color: "#cdfac0"
            background: Rectangle { color: "#061410"; border.color: "#3e6b4d"; radius: 2 }
        }
        CheckBox { id: extBox; text: "ext"; }
        CheckBox { id: rtrBox; text: "rtr"; }

        Label { text: "DLC"; color: "#cdfac0" }
        SpinBox { id: dlcBox; from: 0; to: 8; value: 0; Layout.preferredWidth: 70 }
        Label { text: "data"; color: "#cdfac0" }
        TextField {
            id: dataField; placeholderText: "DEADBEEF (hex)"
            Layout.fillWidth: true
            color: "#cdfac0"
            background: Rectangle { color: "#061410"; border.color: "#3e6b4d"; radius: 2 }
        }

        Item { Layout.fillWidth: true; Layout.columnSpan: 3; height: 4 }
        Button {
            text: "Send"
            onClicked: {
                bridge.injectCan(idField.text, dataField.text,
                                 extBox.checked, rtrBox.checked, dlcBox.value)
            }
        }
    }
}
