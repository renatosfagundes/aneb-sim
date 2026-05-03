// PaneButton.qml — pushbutton that matches the QSS theme used by the
// main board view (#1c2a20 fill, #4a8a5d border, hover #284033).  QML's
// default Button on Windows renders light/grey, which clashes with the
// dark palette — this overrides the contentItem and background so the
// floating panes look uniform.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "Theme.js" as T

Button {
    id: root
    property color tint: T.text          // override for accent buttons (e.g. red Cancel)
    // `borderless` (not `flat` — Button declares `flat` as FINAL since
    // Qt 5.7 and shadowing it is rejected at QML load time).  When true,
    // the button has no border or background fill in the resting state;
    // hover still shows a subtle background to telegraph clickability.
    property bool  borderless: false

    font.family: T.sansFamily
    font.pixelSize: T.fontSmall
    padding: 4
    leftPadding: 10
    rightPadding: 10

    background: Rectangle {
        radius: T.radiusSmall
        color: !root.enabled
                 ? T.bgRaised
                 : (root.borderless
                    ? (root.hovered ? T.bgHover : "transparent")
                    : (root.pressed
                        ? T.accentPress
                        : (root.hovered ? "#284033" : "#1c2a20")))
        border.color: root.borderless ? "transparent" : T.borderStrong
        border.width: root.borderless ? 0 : 1
    }
    contentItem: Text {
        text: root.text
        color: root.enabled ? root.tint : T.textDim
        font: root.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
