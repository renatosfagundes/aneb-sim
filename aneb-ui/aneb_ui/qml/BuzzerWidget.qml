// BuzzerWidget.qml — speaker icon that "buzzes" while PD7 is HIGH.
//
// Visualises the BUZZER pin on each ECU. The real ANEB v1.1 board
// only has a buzzer on ECU1, but in the simulator we render the
// widget on every ECU panel and rely on whichever firmware drives
// PD7 to animate it. If a chip never drives PD7, the widget sits
// quiet — same as a real installed-but-unused speaker.
import QtQuick 2.15

Item {
    id: root
    property string chip: ""

    readonly property bool active: {
        if (!bridge || !bridge.pinStates) return false
        var c = bridge.pinStates[root.chip]
        return (c && c["PD7"]) ? true : false
    }

    implicitWidth:  46
    implicitHeight: 46

    // ---- Outer chrome ring -------------------------------------
    Rectangle {
        id: ring
        anchors.fill: parent
        radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#929aa0" }
            GradientStop { position: 0.5; color: "#5a6168" }
            GradientStop { position: 1.0; color: "#2a3036" }
        }
        border.color: "#1a1d20"; border.width: 1
    }

    // ---- Speaker cone (concentric rings) -----------------------
    Rectangle {
        id: cone
        anchors.centerIn: parent
        width:  parent.width  * 0.78
        height: parent.height * 0.78
        radius: width / 2
        color: root.active ? "#2a1a18" : "#1a1d20"
        border.color: "#0a0a0a"; border.width: 1
        Behavior on color { ColorAnimation { duration: 80 } }
    }
    Rectangle {
        anchors.centerIn: parent
        width:  parent.width  * 0.50
        height: parent.height * 0.50
        radius: width / 2
        color: "#0d0f12"
        border.color: "#202428"; border.width: 1
    }
    // Center "voice coil".
    Rectangle {
        anchors.centerIn: parent
        width:  parent.width  * 0.18
        height: parent.height * 0.18
        radius: width / 2
        color: root.active ? "#ff6644" : "#070808"
        border.color: "#0a0a0a"; border.width: 0.5
        Behavior on color { ColorAnimation { duration: 60 } }
    }

    // ---- Animated sound waves rippling outward when active ----
    Repeater {
        model: 3
        Rectangle {
            anchors.centerIn: parent
            color: "transparent"
            border.color: "#ff8866"
            border.width: 2
            radius: width / 2
            visible: root.active

            // Each ring starts at the speaker size and grows + fades.
            property real phase: index / 3.0
            ParallelAnimation {
                running: root.active
                loops: Animation.Infinite

                NumberAnimation {
                    target: parent
                    property: "width"
                    from: ring.width * 0.78
                    to:   ring.width * 1.6
                    duration: 600
                    easing.type: Easing.OutQuad
                }
                NumberAnimation {
                    target: parent
                    property: "height"
                    from: ring.height * 0.78
                    to:   ring.height * 1.6
                    duration: 600
                    easing.type: Easing.OutQuad
                }
                NumberAnimation {
                    target: parent
                    property: "opacity"
                    from: 0.85
                    to: 0
                    duration: 600
                }
            }
        }
    }

    // ---- BUZZ silkscreen-ish label below ----------------------
    Text {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: -11
        anchors.horizontalCenter: parent.horizontalCenter
        text: "BUZZ"
        color: root.active ? "#ff8866" : "#a8d0b0"
        font.family: "Consolas"; font.pixelSize: 7; font.bold: true
        Behavior on color { ColorAnimation { duration: 80 } }
    }
}
