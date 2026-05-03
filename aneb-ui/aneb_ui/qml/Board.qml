// Board.qml — main view. Layouts-based so the four ECUs and the right
// column flex when the window resizes.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    // No explicit width/height — QQuickWidget sets the size via
    // SizeRootObjectToView so the QML respects the host window.

    Rectangle { anchors.fill: parent; color: "#9fbfd0" }

    // Top-level dashboard window — hidden until the toolbar toggles it.
    DashboardWindow { id: dashboardWindow }
    Image {
        anchors.fill: parent
        source: "../qml_assets/background.png"
        fillMode: Image.PreserveAspectCrop
        opacity: 0.55
        smooth: true
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        // ---- Title strip / toolbar --------------------------------
        // Two-row layout:
        //   Row 1 — primary actions, grouped by function with thin
        //           dividers: Load ▾ dropdown │ Pause/Resume │ Speed
        //           controls │ Dashboard │ engine status indicator.
        //   Row 2 — port reference info (TCP + COM badges), smaller and
        //           dimmer because it's not interactive.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: toolbarColumn.implicitHeight + 14
            color: "#0d2418"
            opacity: 0.92
            border.color: "#3e6b4d"

            // Reusable thin vertical divider for grouping the toolbar.
            component ToolbarDivider: Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 22
                Layout.alignment: Qt.AlignVCenter
                color: "#3e6b4d"
                opacity: 0.6
            }

            ColumnLayout {
                id: toolbarColumn
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                anchors.topMargin: 6
                anchors.bottomMargin: 6
                spacing: 6

                // ---- Row 1: primary actions ------------------------
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Text {
                        text: "ANEB v1.1"
                        color: "#cdfac0"
                        font.family: "Consolas"
                        font.pixelSize: 17
                        font.bold: true
                    }

                    ToolbarDivider {}

                    // ---- Load dropdown (replaces 6 separate buttons)
                    Button {
                        id: loadBtn
                        text: "Load ▾"
                        font.pixelSize: 13
                        onClicked: loadMenu.popup(loadBtn, 0, loadBtn.height)
                        Menu {
                            id: loadMenu
                            MenuItem {
                                text: "Load ECU 1…"
                                onTriggered: { if (bridge) bridge.openLoadDialog("ecu1") }
                            }
                            MenuItem {
                                text: "Load ECU 2…"
                                onTriggered: { if (bridge) bridge.openLoadDialog("ecu2") }
                            }
                            MenuItem {
                                text: "Load ECU 3…"
                                onTriggered: { if (bridge) bridge.openLoadDialog("ecu3") }
                            }
                            MenuItem {
                                text: "Load ECU 4…"
                                onTriggered: { if (bridge) bridge.openLoadDialog("ecu4") }
                            }
                            MenuItem {
                                text: "Load MCU…"
                                onTriggered: { if (bridge) bridge.openLoadDialog("mcu") }
                            }
                            MenuSeparator {}
                            MenuItem {
                                text: "Flash All ECUs…"
                                onTriggered: { if (bridge) bridge.openLoadAllDialog() }
                            }
                        }
                    }

                    Button {
                        text: "Pause"
                        font.pixelSize: 13
                        onClicked: { if (bridge) bridge.pauseEngine() }
                    }
                    Button {
                        text: "Resume"
                        font.pixelSize: 13
                        onClicked: { if (bridge) bridge.resumeEngine() }
                    }

                    ToolbarDivider {}

                    // ---- Speed cluster --------------------------------
                    Text {
                        text: "Speed"
                        color: "#cdfac0"
                        font.family: "Consolas"
                        font.pixelSize: 13
                    }
                    TextField {
                        id: speedField
                        implicitWidth: 70
                        implicitHeight: 28
                        text: bridge ? bridge.speed.toFixed(2) : "0.00"
                        color: "#cdfac0"
                        font.family: "Consolas"
                        font.pixelSize: 13
                        horizontalAlignment: TextInput.AlignHCenter
                        background: Rectangle {
                            color: "#1a3a28"; radius: 3
                            border.color: "#3e6b4d"; border.width: 1
                        }
                        Connections {
                            target: bridge
                            function onSpeedChanged() {
                                if (!speedField.activeFocus)
                                    speedField.text = bridge.speed.toFixed(2)
                            }
                        }
                        onEditingFinished: {
                            var v = parseFloat(text)
                            if (!isNaN(v) && v >= 0.0 && bridge) bridge.setSpeed(v)
                            else text = bridge ? bridge.speed.toFixed(2) : "0.00"
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "Real-time multiplier (0 = unthrottled)"
                    }
                    Button {
                        text: "1×"
                        font.pixelSize: 13
                        onClicked: { if (bridge) bridge.setSpeed(1.0) }
                        ToolTip.visible: hovered
                        ToolTip.text: "Real time (required for avrdude flash)"
                    }
                    Button {
                        text: "Max"
                        font.pixelSize: 13
                        onClicked: { if (bridge) bridge.setSpeed(0.0) }
                        ToolTip.visible: hovered
                        ToolTip.text: "Run unthrottled"
                    }

                    Item { Layout.fillWidth: true }   // pushes status to the right

                    Button {
                        text: dashboardWindow.visible ? "Dashboard ▾" : "Dashboard ▸"
                        font.pixelSize: 13
                        onClicked: dashboardWindow.visible = !dashboardWindow.visible
                    }

                    ToolbarDivider {}

                    // ---- Engine status indicator ----------------------
                    Rectangle {
                        Layout.preferredWidth: 10
                        Layout.preferredHeight: 10
                        Layout.alignment: Qt.AlignVCenter
                        radius: 5
                        color: (bridge && bridge.engineRunning) ? "#22cc44" : "#ddaa22"
                    }
                    Text {
                        text: bridge && bridge.engineRunning ? "running" : "stopped"
                        color: (bridge && bridge.engineRunning) ? "#cdfac0" : "#ddaa22"
                        font.family: "Consolas"
                        font.pixelSize: 13
                    }
                }

                // ---- Row 2: port reference info -------------------
                // Smaller, dim — purely informational.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: "TCP"
                        color: "#7aaa8a"
                        font.family: "Consolas"
                        font.pixelSize: 11
                    }
                    Repeater {
                        model: bridge ? [
                            {chip:"ecu1", port: bridge.uartPorts["ecu1"]},
                            {chip:"ecu2", port: bridge.uartPorts["ecu2"]},
                            {chip:"ecu3", port: bridge.uartPorts["ecu3"]},
                            {chip:"ecu4", port: bridge.uartPorts["ecu4"]},
                            {chip:"mcu",  port: bridge.uartPorts["mcu"]}
                        ] : []
                        Rectangle {
                            implicitHeight: 18
                            implicitWidth:  portLabel.implicitWidth + 10
                            color: "#1a3a28"
                            border.color: "#3e6b4d"
                            border.width: 1
                            radius: 3
                            Text {
                                id: portLabel
                                anchors.centerIn: parent
                                text: modelData.chip.toUpperCase() + ":" + modelData.port
                                color: "#cdfac0"
                                font.family: "Consolas"
                                font.pixelSize: 10
                            }
                            ToolTip.visible: portHover.hovered
                            ToolTip.text: "avrdude -c arduino -P net:localhost:"
                                          + modelData.port + " ...\nChip resets on connect. Set Speed=1.0 first."
                            HoverHandler { id: portHover }
                        }
                    }

                    // Spacer — keeps TCP and COM groups visually separate
                    // even when both are showing.
                    Item {
                        Layout.preferredWidth: 16
                        visible: bridge && Object.keys(bridge.userComPorts).length > 0
                    }

                    Text {
                        visible: bridge && Object.keys(bridge.userComPorts).length > 0
                        text: "COM"
                        color: "#b0855a"
                        font.family: "Consolas"
                        font.pixelSize: 11
                    }
                    Repeater {
                        model: {
                            if (!bridge) return []
                            var u = bridge.userComPorts
                            var out = []
                            for (var i = 0; i < 5; i++) {
                                var c = ["ecu1","ecu2","ecu3","ecu4","mcu"][i]
                                if (u[c]) out.push({chip: c, com: u[c]})
                            }
                            return out
                        }
                        Rectangle {
                            implicitHeight: 18
                            implicitWidth:  comLabel.implicitWidth + 10
                            color: "#3a2a18"
                            border.color: "#7a5a30"
                            border.width: 1
                            radius: 3
                            Text {
                                id: comLabel
                                anchors.centerIn: parent
                                text: modelData.chip.toUpperCase() + ":" + modelData.com
                                color: "#fad8a0"
                                font.family: "Consolas"
                                font.pixelSize: 10
                            }
                            ToolTip.visible: comHover.hovered
                            ToolTip.text: "Open " + modelData.com
                                          + " at 115200 baud in your serial tool"
                                          + "\n(paused automatically while flashing)."
                            HoverHandler { id: comHover }
                        }
                    }

                    // Setup-COM-ports button: only shown when no bridge is
                    // active.  Clicking spawns scripts/setup_com.ps1 (UAC
                    // prompt will appear); the bridges are restarted
                    // automatically once the script finishes.  Hidden once
                    // the bridges are up — at that point the COM badges
                    // above already prove everything works.
                    Button {
                        id: setupComBtn
                        property bool _busy: false
                        visible: bridge && Object.keys(bridge.userComPorts).length === 0
                        text: _busy ? "Setting up… (UAC)" : "Setup COM ports…"
                        font.pixelSize: 10
                        enabled: !_busy
                        ToolTip.visible: hovered
                        ToolTip.text: "Install com0com pairs (will prompt for admin)."
                                    + "\nCreates ECU1..ECU4 + MCU as COM10..COM19 with"
                                    + "\nfriendly names so Serial Monitor / pyserial /"
                                    + "\nremote_flasher can identify each ECU."
                        onClicked: {
                            if (!bridge) return
                            _busy = true
                            bridge.installComPorts()
                        }
                        // The bridge fires uartBridgeChanged when the
                        // restart attempt completes — clear the busy
                        // state so the button can be hidden if bridges
                        // are now up, or re-tried otherwise.
                        Connections {
                            target: bridge
                            function onUartBridgeChanged() {
                                setupComBtn._busy = false
                            }
                        }
                    }

                    // Remove-COM-ports button: only shown when at least
                    // one bridge is active.  Stops the bridges, runs
                    // setup_com.ps1 -Remove (UAC prompt), then leaves the
                    // bridges down (the Setup button will appear again).
                    Button {
                        id: removeComBtn
                        property bool _busy: false
                        visible: bridge && Object.keys(bridge.userComPorts).length > 0
                        text: _busy ? "Removing… (UAC)" : "Remove…"
                        font.pixelSize: 10
                        enabled: !_busy
                        palette.buttonText: "#cc7777"
                        ToolTip.visible: hovered
                        ToolTip.text: "Uninstall the aneb-sim com0com pairs."
                                    + "\nFrees COM10..COM19; the com0com driver"
                                    + "\nitself stays installed.  Will prompt for admin."
                        onClicked: {
                            if (!bridge) return
                            _busy = true
                            bridge.removeComPorts()
                        }
                        Connections {
                            target: bridge
                            function onUartBridgeChanged() {
                                removeComBtn._busy = false
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }   // trailing flex space
                }
            }
        }

        // ---- Main work area: ECU grid (left) + right column -------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // Left: 2x2 ECU grid.
            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 8
                columns: 2
                columnSpacing: 8
                rowSpacing: 8

                EcuPanel { chip: "ecu1"; label: "ECU 1"
                           Layout.fillWidth: true; Layout.fillHeight: true }
                EcuPanel { chip: "ecu2"; label: "ECU 2"
                           Layout.fillWidth: true; Layout.fillHeight: true }
                EcuPanel { chip: "ecu4"; label: "ECU 4"
                           Layout.fillWidth: true; Layout.fillHeight: true }
                EcuPanel { chip: "ecu3"; label: "ECU 3"
                           Layout.fillWidth: true; Layout.fillHeight: true }
            }

            // Right: MCU + CAN inject + CAN monitor.
            ColumnLayout {
                Layout.preferredWidth: 460
                Layout.minimumWidth: 280
                Layout.fillHeight: true
                Layout.margins: 8
                spacing: 8

                Mcu {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 240
                    Layout.minimumHeight: 160
                }
                CanInject {
                    Layout.fillWidth: true
                }
                CanMonitor {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 120
                }
            }
        }
    }
}
