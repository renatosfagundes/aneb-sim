// PaneCheckBox.qml — small themed checkbox used in the plotter legend
// and other dense control rows.  The default QML CheckBox indicator is
// large and light-themed; this is compact and uses the PCB-green palette.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "Theme.js" as T

CheckBox {
    id: root
    padding: 0
    spacing: 4
    indicator: Rectangle {
        implicitWidth: 14
        implicitHeight: 14
        x: root.leftPadding
        y: root.topPadding + (root.availableHeight - height) / 2
        radius: 2
        color: root.checked ? T.accent : T.bgDeep
        border.color: root.hovered ? T.borderStrong : T.border
        border.width: 1
        // Inner check mark.
        Rectangle {
            anchors.centerIn: parent
            width: 6; height: 6; radius: 1
            color: T.textBright
            visible: root.checked
        }
    }
    contentItem: Text {
        text: root.text
        color: T.text
        font.family: T.sansFamily
        font.pixelSize: T.fontSmall
        leftPadding: root.indicator.width + root.spacing
        verticalAlignment: Text.AlignVCenter
    }
}
