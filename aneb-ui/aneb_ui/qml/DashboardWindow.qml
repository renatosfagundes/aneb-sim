// DashboardWindow.qml — top-level window holding the global
// Dashboard widget. Toggled by a "Dashboard" button in the main
// toolbar.
import QtQuick 2.15
import QtQuick.Window 2.15

Window {
    id: root
    width:  920
    height: 460
    minimumWidth:  680
    minimumHeight: 360

    title: "ANEB simulator — dashboard"
    visible: false
    color: "#061410"

    property bool _placed: false
    onVisibleChanged: {
        if (visible && !_placed) {
            x = Screen.virtualX + 60
            y = Screen.virtualY + 60
            _placed = true
        }
    }

    Dashboard { anchors.fill: parent }
}
