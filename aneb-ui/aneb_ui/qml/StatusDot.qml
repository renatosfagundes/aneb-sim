// StatusDot.qml — small colored circle used as a status/activity
// indicator (e.g. RX/TX blink on the console, "flashing" pulse on the
// avrdude pane).  Optionally pulses by adjusting opacity.

import QtQuick 2.15
import "Theme.js" as T

Rectangle {
    id: root
    property color baseColor: T.success
    property bool  active:    false       // true → pulse / brighten
    property bool  pulse:     false       // animate opacity when active

    implicitWidth: 8
    implicitHeight: 8
    radius: 4
    color: active ? baseColor : T.bgHover
    opacity: active ? 1.0 : 0.55

    SequentialAnimation on opacity {
        running: root.active && root.pulse
        loops: Animation.Infinite
        NumberAnimation { to: 0.35; duration: 400; easing.type: Easing.InOutSine }
        NumberAnimation { to: 1.00; duration: 400; easing.type: Easing.InOutSine }
    }
}
