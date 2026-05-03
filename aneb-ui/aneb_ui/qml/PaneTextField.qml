// PaneTextField.qml — single-line input with theme-matched chrome.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "Theme.js" as T

TextField {
    id: root
    color: T.text
    font.family: T.monoFamily
    font.pixelSize: T.fontBody
    selectByMouse: true
    leftPadding: 8
    rightPadding: 8
    topPadding: 4
    bottomPadding: 4
    placeholderTextColor: T.textDim
    background: Rectangle {
        color: T.bgDeep
        border.color: root.activeFocus ? T.borderStrong : T.border
        border.width: 1
        radius: T.radiusSmall
    }
}
