// PlotterWindow.qml — top-level OS window wrapping a Plotter for one
// chip. Toggled by the "Plot" button in each panel's title row.
//
// Same pattern as SerialConsoleWindow: hidden by default, position
// once on first show, can be moved/resized/closed independently.
import QtQuick 2.15
import QtQuick.Window 2.15

Window {
    id: root
    property string chip:  ""
    property string label: ""

    width:  640
    height: 360
    minimumWidth:  400
    minimumHeight: 240

    title: (label.length ? label : chip.toUpperCase()) + " — plotter"
    visible: false
    color: "#0a1612"

    property bool _placed: false
    onVisibleChanged: {
        if (visible && !_placed) {
            x = Screen.virtualX + 120
            y = Screen.virtualY + 120
            _placed = true
        }
    }

    Plotter {
        anchors.fill: parent
        anchors.margins: 4
        chip:   root.chip
        active: root.visible
    }
}
