// AvrdudeWindow.qml — floating window that shows avrdude flash progress
// for one chip.  Opened automatically when a flash job starts; the user
// can also open it manually with the "Flash" button in EcuPanel.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import "Theme.js" as T

Window {
    id: root
    property string chip:  ""
    property string label: ""

    width:  600
    height: 360
    minimumWidth:  420
    minimumHeight: 220
    title: (label.length ? label : chip.toUpperCase()) + " — avrdude"
    color: T.bg

    property bool _placed: false
    onVisibleChanged: {
        if (visible && !_placed) {
            x = Screen.virtualX + 120
            y = Screen.virtualY + 120
            _placed = true
        }
    }

    // ---- live state ------------------------------------------------
    property bool busy: false
    property string lastStatus: "Idle"     // "Idle" / "Flashing" / "Success" / "Failed"
    property color  statusColor: T.textMuted

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- header ------------------------------------------------
        PaneHeader {
            id: header
            Layout.fillWidth: true
            chip:    root.chip
            label:   root.label
            title:   "Flash"
            subtitle: root.lastStatus
            accent:  root.statusColor

            StatusDot {
                baseColor: root.statusColor
                active: root.busy || root.lastStatus !== "Idle"
                pulse: root.busy
            }
            PaneButton {
                text: "Flash again"
                enabled: !root.busy && bridge !== undefined && bridge !== null
                onClicked: { if (bridge) bridge.flashChipAvrdude(root.chip) }
            }
            PaneButton {
                text: "Copy"
                borderless: true
                enabled: log.length > 0
                onClicked: { log.selectAll(); log.copy() }
                ToolTip.visible: hovered
                ToolTip.text: "Copy the entire log to clipboard."
                ToolTip.delay: 500
            }
            PaneButton {
                text: "Clear"
                borderless: true
                enabled: log.length > 0
                onClicked: log.clear()
            }
        }

        // ---- log area ---------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: T.bgDeep
            border.color: T.borderSubtle

            ScrollView {
                id: scroll
                anchors.fill: parent
                anchors.margins: 1
                clip: true

                TextArea {
                    id: log
                    readOnly: true
                    wrapMode: TextArea.NoWrap
                    textFormat: TextEdit.RichText
                    color: T.text
                    selectionColor: T.accent
                    selectedTextColor: T.textBright
                    selectByMouse: true
                    font.family: T.monoFamily
                    font.pixelSize: T.fontBody
                    background: null
                    leftPadding: 8
                    rightPadding: 8
                    topPadding: 6
                    bottomPadding: 6
                    placeholderText: "(no avrdude output yet — click Flash again to start)"
                    placeholderTextColor: T.textDim
                }
            }
        }
    }

    // ---- helpers ---------------------------------------------------
    function _classify(line) {
        // Returns a CSS color string for the line based on lightweight
        // pattern matching against avrdude's typical phrasing.
        var l = line.toLowerCase()
        if (l.indexOf("error") >= 0 || l.indexOf("not in sync") >= 0
                || l.indexOf("failed") >= 0 || l.indexOf("timeout") >= 0
                || line.indexOf("✗") >= 0)        return T.error
        if (l.indexOf("warning") >= 0)            return T.warning
        if (l.indexOf("verified") >= 0
                || l.indexOf("done.") >= 0
                || l.indexOf("successful") >= 0
                || line.indexOf("✓") >= 0)        return T.success
        if (l.indexOf("writing") >= 0
                || l.indexOf("reading") >= 0
                || l.indexOf("verifying") >= 0)   return T.info
        return T.text
    }
    function _escape(s) {
        return s.replace(/&/g, "&amp;")
                .replace(/</g, "&lt;")
                .replace(/>/g, "&gt;")
    }
    function _appendLine(line) {
        var c = _classify(line)
        // Wrap each line in a <span> so RichText carries colour.
        var html = "<span style=\"color:" + c + "\">"
                 + _escape(line) + "</span><br/>"
        log.insert(log.length, html)
        log.cursorPosition = log.length
    }

    // ---- bridge wiring --------------------------------------------
    Connections {
        target: bridge
        function onAvrdudeOutput(chip, line) {
            if (chip !== root.chip) return
            root._appendLine(line)
        }
        function onAvrdudeStateChanged(chip, running) {
            if (chip !== root.chip) return
            root.busy = running
            if (running) {
                root.lastStatus = "Flashing…"
                root.statusColor = T.warning
                root.visible = true
            } else {
                // Final state inferred from the last log line.  We keep
                // it simple: if the log mentions "successful"/"verified"/
                // "done" we call it a success, otherwise we report "Done"
                // and let the user inspect.
                var t = log.text.toLowerCase()
                var ok = (t.indexOf("verified") >= 0
                       || t.indexOf("successful") >= 0
                       || t.indexOf("done.") >= 0)
                       && t.indexOf("error") < 0
                       && t.indexOf("not in sync") < 0
                root.lastStatus = ok ? "Success" : "Done"
                root.statusColor = ok ? T.success : T.warning
            }
        }
    }
}
