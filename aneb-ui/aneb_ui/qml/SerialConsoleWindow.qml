// SerialConsoleWindow.qml — top-level OS window wrapping a single
// chip's SerialConsole.
//
// Used so the serial console doesn't have to fight for vertical space
// inside the ECU/MCU panel. A "Console" button on each panel toggles
// its window's visibility; the user can move, resize, or close it
// independently of the main board view.
import QtQuick 2.15
import QtQuick.Window 2.15
import "Theme.js" as T

Window {
    id: root
    property string chip:  ""
    property string label: ""

    width:  560
    height: 360
    minimumWidth:  360
    minimumHeight: 200

    title: (label.length ? label : chip.toUpperCase()) + " — serial console"
    visible: false
    color: T.bg

    // Position the first time it's shown so all four ECU windows
    // don't pile up on top of each other. Subsequent opens reuse
    // whatever the user moved the window to.
    property bool _placed: false
    onVisibleChanged: {
        if (visible && !_placed) {
            x = Screen.virtualX + 80
            y = Screen.virtualY + 80
            _placed = true
        }
    }

    SerialConsole {
        anchors.fill: parent
        chip:  root.chip
        label: root.label
    }
}
