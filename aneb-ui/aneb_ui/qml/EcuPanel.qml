// EcuPanel.qml — composes one ECU's worth of widgets, using
// QtQuick.Layouts so children shrink gracefully when the window
// resizes and never overflow the panel.
//
// Layout:
//
//   ┌───────────────────────────────────────────────────────┐
//   │ ECU 1 │ Console │ Plot │ Flash │ Eject                │  title row
//   ├──────────┬──────────────────────────────┬────────────┤
//   │ ● CAN    │                              │   DIN1     │
//   │  active  │   [ Arduino Nano image ]     │   DIN3     │  main row
//   │ Hex: …   │                              │   DIN2     │
//   │ RAM: …   │   [ 16x2 LCD ]               │   DIN4     │
//   │ COM: …   │                              │            │
//   ├──────────┴──────────────────────────────┴────────────┤
//   │ DOUT0 DOUT1   AIN0 AIN1 AIN2 AIN3   BUZZ              │  bottom row
//   │   L   LDR                                             │
//   └───────────────────────────────────────────────────────┘
//
// Every interactive widget's preferred size is scaled by `_s`, a
// panel-relative scale factor. The Nano image uses Layout.fillHeight
// so it absorbs leftover vertical space, growing on big panels and
// giving way on tight ones (instead of clipping the bottom row).
//
// The serial console, plotter, and avrdude output are separate
// top-level windows per chip — toggle buttons in the title row pop
// them up.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    property string chip:  ""
    property string label: ""

    implicitWidth:  720
    implicitHeight: 360
    Layout.minimumWidth:  220
    Layout.minimumHeight: 220

    // Scale factor — 1.0 at the "comfortable" reference size of
    // 600x320, 0.4 at the floor (so the smallest widgets still render
    // with usable shapes), and up to 1.8 when the panel grows.  Driven
    // by whichever dimension is more constrained, so a tall narrow
    // panel doesn't make the controls overflow horizontally.
    readonly property real _s: Math.max(0.4, Math.min(width / 600, height / 320, 1.8))

    Rectangle {
        anchors.fill: parent
        color: "#0d2418"
        border.color: "#3e6b4d"
        border.width: 1
        radius: 6
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8 * root._s
        spacing: 6 * root._s

        // ---- Title row: label + window toggles --------------------
        Flow {
            Layout.fillWidth: true
            Layout.preferredHeight: childrenRect.height
            spacing: 8 * root._s

            Text {
                text: root.label
                color: "#cdfac0"
                font.family: "Consolas"
                font.pixelSize: 14 * root._s
                font.bold: true
            }
            Button {
                text: consoleWindow.visible ? "Console ▾" : "Console ▸"
                font.pixelSize: Math.max(12, 14 * root._s)
                onClicked: consoleWindow.visible = !consoleWindow.visible
            }
            Button {
                text: plotterWindow.visible ? "Plot ▾" : "Plot ▸"
                font.pixelSize: Math.max(12, 14 * root._s)
                onClicked: plotterWindow.visible = !plotterWindow.visible
            }
            Button {
                id: flashBtn
                property bool _busy: false
                text: _busy ? "Flashing…" : (avrdudeWindow.visible ? "Flash ▾" : "Flash ▸")
                font.pixelSize: Math.max(12, 14 * root._s)
                enabled: !_busy
                palette.buttonText: _busy ? "#ffcc00" : "#44ddff"
                onClicked: {
                    if (!avrdudeWindow.visible) avrdudeWindow.visible = true
                    if (bridge) bridge.flashChipAvrdude(root.chip)
                }
                Connections {
                    target: bridge
                    function onAvrdudeStateChanged(chip, running) {
                        if (chip === root.chip) flashBtn._busy = running
                    }
                }
            }
            Button {
                text: "Eject"
                font.pixelSize: Math.max(12, 14 * root._s)
                palette.buttonText: "#ff6655"
                onClicked: { if (bridge) bridge.unloadChip(root.chip) }
            }
        }

        SerialConsoleWindow {
            id: consoleWindow
            chip:  root.chip
            label: root.label
        }
        PlotterWindow {
            id: plotterWindow
            chip:  root.chip
            label: root.label
        }
        AvrdudeWindow {
            id: avrdudeWindow
            chip:  root.chip
            label: root.label
        }

        // ---- Main row: info | Nano+LCD | DIN buttons --------------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8 * root._s

            // ---- Info sidebar ----------------------------------
            // CAN status + per-chip data points (firmware, RAM, COM).
            // The Hex/RAM placeholders are not yet wired to engine
            // state — they show "—" until the bridge exposes the
            // values; the COM line reflects the live bridge mapping.
            ColumnLayout {
                id: sidebar
                // Reserve more breathing room for the info column so
                // "Hex: Optiboot (built-in)" doesn't elide to "(bu…".
                // The Nano's max-width below caps the center column,
                // letting the sidebar take what it needs.
                Layout.preferredWidth: Math.max(130, 160 * root._s)
                Layout.minimumWidth: 100
                Layout.fillHeight: true
                spacing: 5 * root._s

                // The text font's only purpose at tiny sizes is to mark
                // the line as present; let it shrink to 10 px before
                // bottoming out so the column matches the panel's scale.
                readonly property real _font: Math.max(10, 13 * root._s)

                // Live chip metadata, refreshed each chipStatChanged tick.
                // Properties live on the ColumnLayout (qualified through
                // `sidebar.` from child bindings — child Text items can't
                // resolve unqualified names against an unnamed parent).
                property string hexName: "—"
                property string hexPath: ""
                property int    freeRam: -1
                property int    ramSize: 0

                function refreshStat() {
                    if (!bridge || !bridge.chipStat) return
                    var s = bridge.chipStat(root.chip)
                    if (!s) return
                    hexName = s.hex_name && s.hex_name.length ? s.hex_name : "—"
                    hexPath = s.hex_path || ""
                    freeRam = (s.free_ram !== undefined) ? s.free_ram : -1
                    ramSize = s.ram_size || 0
                }

                CanIndicator {
                    id: canBadge
                    chip: root.chip
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(34, 44 * root._s)

                    ToolTip.visible: canHover.containsMouse
                    ToolTip.delay: 400
                    ToolTip.text:
                        "CAN bus state for " + root.label + "\n" +
                        "TEC = transmit error counter\n" +
                        "REC = receive error counter\n" +
                        "Bus state: active (≤95) → passive (≥128) → off (TEC≥256)"
                    MouseArea {
                        id: canHover
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                }

                Connections {
                    target: bridge
                    function onChipStatChanged(chip) {
                        if (chip === root.chip) sidebar.refreshStat()
                    }
                }
                Component.onCompleted: sidebar.refreshStat()

                // Hex name — truncated to fit; full path on hover.
                Text {
                    text: "Hex: " + sidebar.hexName
                    color: "#cdfac0"
                    font.family: "Consolas"
                    font.pixelSize: sidebar._font
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    ToolTip.visible: hexHover.containsMouse && sidebar.hexName !== "—"
                    ToolTip.delay: 400
                    ToolTip.text: sidebar.hexPath.length > 0
                                    ? sidebar.hexName + "\n" + sidebar.hexPath
                                    : sidebar.hexName
                    MouseArea {
                        id: hexHover
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                }
                // Free RAM — shown in bytes once the engine has emitted
                // its first chipstat tick (so we don't print "0 B" before
                // the chip is up).
                Text {
                    text: sidebar.freeRam < 0
                            ? "Free RAM: —"
                            : "Free RAM: " + sidebar.freeRam + " / " + sidebar.ramSize + " B"
                    color: "#cdfac0"
                    font.family: "Consolas"
                    font.pixelSize: sidebar._font
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    ToolTip.visible: ramHover.containsMouse && sidebar.freeRam >= 0
                    ToolTip.delay: 400
                    ToolTip.text:
                        "Free SRAM = current SP − data start.\n" +
                        "Approximates stack head-room: tight values mean\n" +
                        "the sketch is close to overflowing into the heap."
                    MouseArea {
                        id: ramHover
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                }
                Text {
                    text: {
                        if (!bridge || !bridge.userComPorts) return "COM: —"
                        var p = bridge.userComPorts[root.chip]
                        return p ? "COM: " + p : "COM: —"
                    }
                    color: "#cdfac0"
                    font.family: "Consolas"
                    font.pixelSize: sidebar._font
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    ToolTip.visible: comHover.containsMouse
                    ToolTip.delay: 400
                    ToolTip.text: {
                        if (!bridge || !bridge.userComPorts)
                            return "COM port for this chip — bridge not running."
                        var p = bridge.userComPorts[root.chip]
                        return p
                            ? "Open " + p + " in any serial tool to read this " +
                              "chip's UART (115200 baud)."
                            : "No COM mapping yet — bridge starting…"
                    }
                    MouseArea {
                        id: comHover
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                }

                Item { Layout.fillHeight: true }   // push entries to top
            }

            // ---- Center: Nano illustration + LCD ---------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6 * root._s

                ArduinoNano {
                    id: nano
                    chip: root.chip
                    power: bridge && bridge.engineRunning
                    // fillHeight lets the Nano absorb any vertical space
                    // left over after LCD takes its preferred height.
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: Math.min(width / (1500.0 / 571.0), 140)
                    Layout.minimumHeight: 40
                    Layout.maximumHeight: 200
                    // Cap the Nano's width so the info sidebar isn't
                    // squeezed past the point where its lines elide on
                    // medium-sized panels.  fillWidth still lets it use
                    // any extra space below this cap.
                    Layout.maximumWidth: Math.max(360, 480 * root._s)
                }

                Connections {
                    target: bridge
                    function onUartAppended(chip, data) {
                        if (chip === root.chip) nano.pulseTx()
                    }
                    function onUartSent(chip) {
                        if (chip === root.chip) nano.pulseRx()
                    }
                }

                LcdWidget {
                    id: lcd
                    chip: root.chip
                    fontScale: root._s
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(36, Math.min(width / 6, 72))
                }
            }

            // ---- Right: DIN buttons stacked ---------------------
            // Buttons keep their natural (collar + 2-line label) size;
            // a single fillHeight spacer at the bottom absorbs any
            // surplus column height so the buttons cluster at the top
            // without crushing each other's labels.
            ColumnLayout {
                Layout.preferredWidth: 50 * root._s
                Layout.minimumWidth: 44 * root._s
                Layout.fillHeight: true
                spacing: 6 * root._s

                PushButton {
                    chip: root.chip; pin: "A4"; label: "DIN1  A4"
                    color: "#e04a4a"
                    fontScale: root._s
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth:  44 * root._s
                    Layout.preferredHeight: 56 * root._s
                    Layout.minimumHeight:   38 * root._s
                }
                PushButton {
                    chip: root.chip; pin: "D9"; label: "DIN3  D9"
                    color: "#3ec85a"
                    fontScale: root._s
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth:  44 * root._s
                    Layout.preferredHeight: 56 * root._s
                    Layout.minimumHeight:   38 * root._s
                }
                PushButton {
                    chip: root.chip; pin: "A5"; label: "DIN2  A5"
                    color: "#e8c440"
                    fontScale: root._s
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth:  44 * root._s
                    Layout.preferredHeight: 56 * root._s
                    Layout.minimumHeight:   38 * root._s
                }
                PushButton {
                    chip: root.chip; pin: "D8"; label: "DIN4  D8"
                    color: "#3a8fe8"
                    fontScale: root._s
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth:  44 * root._s
                    Layout.preferredHeight: 56 * root._s
                    Layout.minimumHeight:   38 * root._s
                }
                Item { Layout.fillHeight: true }
            }
        }

        // ---- Bottom row: LEDs + AIN trimpots + Buzzer -------------
        // Four fillWidth spacers (one before LEDs, one between LEDs and
        // pots, one between pots and buzzer, one after buzzer) split
        // available whitespace into four equal gaps — so the LEDs sit
        // exactly halfway between the panel's left margin and the
        // trimpot row, and the buzzer sits halfway between the trimpot
        // row and the right margin.  minimumHeight forces the row to
        // keep its full vertical space even when the panel is tight,
        // so the trimpot body + value + label stack never gets clipped.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 120 * root._s
            // Lower minimum than preferred so the row CAN shrink when
            // the panel is tight (e.g. minimum window with title row
            // wrapped to 2 lines) — avoids forcing the row to claim
            // space the panel doesn't have, which was pushing the
            // bottom-row content past the panel's bottom border.
            Layout.minimumHeight:   80 * root._s
            spacing: 0

            Item { Layout.fillWidth: true }   // gap: margin → LEDs

            // 2x2 LED grid: DOUT0 amber, DOUT1 green, L red, LDR blue.
            GridLayout {
                columns: 2
                rowSpacing: 6 * root._s
                columnSpacing: 10 * root._s
                Layout.alignment: Qt.AlignVCenter

                ColumnLayout {
                    spacing: 0
                    Layout.alignment: Qt.AlignHCenter
                    Led {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth:  24 * root._s
                        Layout.preferredHeight: 24 * root._s
                        onColor: "#ffaa22"     // amber — DOUT0 (PD3, dimmable)
                        brightness: nano.level("PD3")
                                    + (nano.duty("PD3") * (1 - nano.level("PD3")))
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "DOUT0"; color: "#a8d0b0"
                        font.family: "Consolas"; font.pixelSize: 10 * root._s
                    }
                }
                ColumnLayout {
                    spacing: 0
                    Layout.alignment: Qt.AlignHCenter
                    Led {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth:  24 * root._s
                        Layout.preferredHeight: 24 * root._s
                        onColor: "#22cc44"     // green — DOUT1 (PD4)
                        brightness: nano.level("PD4")
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "DOUT1"; color: "#a8d0b0"
                        font.family: "Consolas"; font.pixelSize: 10 * root._s
                    }
                }
                ColumnLayout {
                    spacing: 0
                    Layout.alignment: Qt.AlignHCenter
                    Led {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth:  24 * root._s
                        Layout.preferredHeight: 24 * root._s
                        onColor: "#ff3344"     // red — Nano on-board L LED (PB5)
                        brightness: nano.level("PB5")
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "L"; color: "#a8d0b0"
                        font.family: "Consolas"; font.pixelSize: 10 * root._s
                    }
                }
                ColumnLayout {
                    spacing: 0
                    Layout.alignment: Qt.AlignHCenter
                    Led {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth:  24 * root._s
                        Layout.preferredHeight: 24 * root._s
                        onColor: "#3aaaff"     // blue — LDR_LED (PD6 PWM)
                        brightness: nano.duty("PD6")
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "LDR"; color: "#a8d0b0"
                        font.family: "Consolas"; font.pixelSize: 10 * root._s
                    }
                }
            }

            Item { Layout.fillWidth: true }   // gap: LEDs → pots

            // Trimpots row (4 channels, AIN0–AIN3).
            RowLayout {
                spacing: 8 * root._s
                Layout.alignment: Qt.AlignVCenter

                TrimPot { chip: root.chip; channel: 0; label: "AIN0  A0"
                          fontScale: root._s
                          Layout.preferredWidth:  48 * root._s
                          Layout.preferredHeight: 84 * root._s }
                TrimPot { chip: root.chip; channel: 1; label: "AIN1  A1"
                          fontScale: root._s
                          Layout.preferredWidth:  48 * root._s
                          Layout.preferredHeight: 84 * root._s }
                TrimPot { chip: root.chip; channel: 2; label: "AIN2  A2"
                          fontScale: root._s
                          Layout.preferredWidth:  48 * root._s
                          Layout.preferredHeight: 84 * root._s }
                TrimPot { chip: root.chip; channel: 3; label: "AIN3  A3"
                          fontScale: root._s
                          Layout.preferredWidth:  48 * root._s
                          Layout.preferredHeight: 84 * root._s }
            }

            Item { Layout.fillWidth: true }   // gap: pots → buzzer

            BuzzerWidget {
                chip: root.chip
                fontScale: root._s
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth:  56 * root._s
                Layout.preferredHeight: 56 * root._s
            }

            Item { Layout.fillWidth: true }   // gap: buzzer → margin
        }
    }
}
