// Dashboard.qml — compact at-a-glance view of every chip's live
// state in one place. Lives inside DashboardWindow.
//
// One card per chip: ADC values, PWM duty bars, digital LED state
// dots, button state dots, CAN error counters + state pill. All
// data comes straight from existing QmlBridge properties — no new
// wiring is needed; the dashboard is a renderer, not a recorder.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root

    // ---- helpers (chip-keyed lookups against the bridge) --------
    function adcValue(chip, ch) {
        if (!bridge || !bridge.adcValues) return 0
        var c = bridge.adcValues[chip]
        if (!c) return 0
        var v = c[String(ch)]
        return v === undefined ? 0 : v
    }
    function pwmDuty(chip, pin) {
        if (!bridge || !bridge.pwmDuties) return 0
        var c = bridge.pwmDuties[chip]
        if (!c || c[pin] === undefined) return 0
        return c[pin]
    }
    function pinLevel(chip, pin) {
        if (!bridge || !bridge.pinStates) return 0
        var c = bridge.pinStates[chip]
        if (!c || c[pin] === undefined) return 0
        return c[pin] ? 1 : 0
    }
    function canStateOf(chip) {
        if (!bridge || !bridge.canStateOf) return ({tec: 0, rec: 0, state: ""})
        return bridge.canStateOf(chip) || ({tec: 0, rec: 0, state: ""})
    }
    function stateColor(s) {
        if (s === "active")  return "#22cc44"
        if (s === "passive") return "#ddaa22"
        if (s === "bus-off") return "#ff3344"
        return "#666666"
    }

    // ---- layout: 2x2 of ECU cards + MCU card on the right --------
    GridLayout {
        anchors.fill: parent
        anchors.margins: 8
        columns: 3
        rowSpacing: 8
        columnSpacing: 8

        ChipCard { chipId: "ecu1"; chipTitle: "ECU 1"
                   Layout.fillWidth: true; Layout.fillHeight: true
                   Layout.column: 0; Layout.row: 0 }
        ChipCard { chipId: "ecu2"; chipTitle: "ECU 2"
                   Layout.fillWidth: true; Layout.fillHeight: true
                   Layout.column: 1; Layout.row: 0 }
        ChipCard { chipId: "mcu";  chipTitle: "MCU";  isMcu: true
                   Layout.fillWidth: true; Layout.fillHeight: true
                   Layout.column: 2; Layout.row: 0; Layout.rowSpan: 2 }
        ChipCard { chipId: "ecu4"; chipTitle: "ECU 4"
                   Layout.fillWidth: true; Layout.fillHeight: true
                   Layout.column: 0; Layout.row: 1 }
        ChipCard { chipId: "ecu3"; chipTitle: "ECU 3"
                   Layout.fillWidth: true; Layout.fillHeight: true
                   Layout.column: 1; Layout.row: 1 }
    }

    // ---- one chip card -------------------------------------------
    component ChipCard: Rectangle {
        id: card
        property string chipId:    ""
        property string chipTitle: ""
        property bool   isMcu:     false

        color: "#0d2418"
        border.color: "#3e6b4d"
        border.width: 1
        radius: 6

        readonly property var canSnap: root.canStateOf(card.chipId)

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6

            // Title + CAN state pill
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: card.chipTitle
                    color: "#cdfac0"
                    font.family: "Consolas"
                    font.pixelSize: 14
                    font.bold: true
                }
                Item { Layout.fillWidth: true }
                Rectangle {
                    visible: !card.isMcu
                    Layout.preferredWidth:  84
                    Layout.preferredHeight: 18
                    radius: 3
                    color: root.stateColor(card.canSnap.state)
                    Text {
                        anchors.centerIn: parent
                        text: card.canSnap.state || "—"
                        color: "#0a1612"
                        font.family: "Consolas"
                        font.pixelSize: 10
                        font.bold: true
                    }
                }
            }

            // ADC strip — 4 numbers
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: 4
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        color: "#0a1a14"
                        border.color: "#1a3024"; border.width: 1
                        radius: 3
                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 0
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: "AIN" + index
                                color: "#5a8a6a"
                                font.family: "Consolas"; font.pixelSize: 8
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: root.adcValue(card.chipId, index)
                                color: "#cdfac0"
                                font.family: "Consolas"; font.pixelSize: 12
                                font.bold: true
                            }
                        }
                    }
                }
            }

            // PWM duty bars — DOUT0 (PD3) and LDR (PD6).
            DutyBar { Layout.fillWidth: true
                      label: "DOUT0"; barColor: "#ffaa22"
                      duty: root.pwmDuty(card.chipId, "PD3") }
            DutyBar { Layout.fillWidth: true
                      label: "LDR";   barColor: "#3aaaff"
                      duty: root.pwmDuty(card.chipId, "PD6") }

            // Digital state dots row
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                StateDot { dotLabel: "L";    on: root.pinLevel(card.chipId, "PB5"); onColor: "#ff3344" }
                StateDot { dotLabel: "OUT1"; on: root.pinLevel(card.chipId, "PD4"); onColor: "#22cc44" }
                Item { Layout.preferredWidth: 8 }
                StateDot { dotLabel: "IN1"; on: !root.pinLevel(card.chipId, "PC4"); onColor: "#e04a4a" }
                StateDot { dotLabel: "IN2"; on: !root.pinLevel(card.chipId, "PC5"); onColor: "#e8c440" }
                StateDot { dotLabel: "IN3"; on: !root.pinLevel(card.chipId, "PB1"); onColor: "#3ec85a" }
                StateDot { dotLabel: "IN4"; on: !root.pinLevel(card.chipId, "PB0"); onColor: "#3a8fe8" }
                Item { Layout.fillWidth: true }
            }

            // CAN counters (ECUs only)
            Text {
                visible: !card.isMcu
                text: "TEC " + card.canSnap.tec + "   REC " + card.canSnap.rec
                color: "#a8d0b0"
                font.family: "Consolas"; font.pixelSize: 10
            }

            Item { Layout.fillHeight: true }
        }
    }

    // ---- duty bar ------------------------------------------------
    component DutyBar: Item {
        id: bar
        property string label:    ""
        property color  barColor: "#ffffff"
        property real   duty:     0.0
        implicitHeight: 14

        RowLayout {
            anchors.fill: parent
            spacing: 6
            Text {
                text: bar.label
                color: "#a8d0b0"
                font.family: "Consolas"; font.pixelSize: 9
                Layout.preferredWidth: 40
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 10
                color: "#0a1a14"
                border.color: "#1a3024"; border.width: 1
                radius: 1
                Rectangle {
                    anchors.left: parent.left
                    anchors.top:  parent.top
                    height: parent.height
                    width:  parent.width * Math.max(0, Math.min(1, bar.duty))
                    color: bar.barColor
                    radius: 1
                }
            }
            Text {
                text: Math.round(bar.duty * 100) + "%"
                color: "#cdfac0"
                font.family: "Consolas"; font.pixelSize: 9
                Layout.preferredWidth: 32
                horizontalAlignment: Text.AlignRight
            }
        }
    }

    // ---- digital state dot --------------------------------------
    component StateDot: ColumnLayout {
        id: dot
        property string dotLabel: ""
        property bool   on:       false
        property color  onColor:  "#ffffff"
        spacing: 0

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth:  12
            Layout.preferredHeight: 12
            radius: 6
            color: dot.on ? dot.onColor : "#1a2a22"
            border.color: "#0a0a0a"; border.width: 1
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: dot.dotLabel
            color: "#5a8a6a"
            font.family: "Consolas"; font.pixelSize: 7
        }
    }
}
