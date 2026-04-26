// CanMonitor.qml — scrolling list of can_tx events.
import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root
    implicitHeight: 200

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

        // Header.
        Rectangle {
            width: parent.width; height: 22
            color: "#102a1c"
            Row {
                anchors.fill: parent
                anchors.leftMargin: 6
                spacing: 12
                Text { text: "ts";   color: "#cdfac0"; font.bold: true; font.pixelSize: 11; width: 80 }
                Text { text: "src";  color: "#cdfac0"; font.bold: true; font.pixelSize: 11; width: 50 }
                Text { text: "id";   color: "#cdfac0"; font.bold: true; font.pixelSize: 11; width: 80 }
                Text { text: "dlc";  color: "#cdfac0"; font.bold: true; font.pixelSize: 11; width: 32 }
                Text { text: "data"; color: "#cdfac0"; font.bold: true; font.pixelSize: 11 }
            }
        }

        ListView {
            id: list
            width: parent.width
            height: parent.height - 24
            clip: true
            model: bridge ? bridge.canFrames : []
            spacing: 1

            delegate: Rectangle {
                width: list.width
                height: 18
                color: index % 2 === 0 ? "#08180f" : "#0c1d14"
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    spacing: 12
                    Text { text: modelData.ts || "";   color: "#a8d0b0"; font.family: "Consolas"; font.pixelSize: 11; width: 80 }
                    Text { text: modelData.src || "";  color: "#cdfac0"; font.family: "Consolas"; font.pixelSize: 11; width: 50 }
                    Text { text: modelData.id  || "";  color: "#ffd24a"; font.family: "Consolas"; font.pixelSize: 11; font.bold: true; width: 80 }
                    Text { text: (modelData.dlc !== undefined ? modelData.dlc : ""); color: "#cdfac0"; font.family: "Consolas"; font.pixelSize: 11; width: 32 }
                    Text { text: modelData.data || ""; color: "#cdfac0"; font.family: "Consolas"; font.pixelSize: 11 }
                }
            }

            // Auto-scroll to the bottom on each frame append.
            onCountChanged: positionViewAtEnd()
        }
    }
}
