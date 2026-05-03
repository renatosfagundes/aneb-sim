// Plotter.qml — rolling-window chart of selected signals from one
// chip. Shown inside a PlotterWindow.
//
// Signals come from `bridge.plotSignals()` (static catalogue). Their
// data comes from `bridge.plotSeries(chip, key)` and is refreshed on
// `bridge.plotSeqChanged` (fired ~20 Hz by the bridge's sample
// timer). The X axis is "seconds before now"; the most recent sample
// sits at x=0 and older samples slide left.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15
import "Theme.js" as T

Item {
    id: root
    property string chip:  ""
    property string label: ""

    // Active when the parent window is visible — when false we skip
    // refreshes entirely so a hidden plotter costs nothing.
    property bool active: true

    // ---- trace catalogue + per-trace enable map -------------------
    readonly property var _palette: [
        "#ff5566", "#22cc44", "#3aaaff", "#ffcc44",
        "#bb88ff", "#ff8855", "#44ddcc", "#cc6699",
        "#88ccff", "#ddaa44", "#66cc88", "#ff66bb",
        "#5566ff", "#aaaaaa"
    ]
    readonly property var _catalogue:
        (typeof bridge !== "undefined" && bridge && bridge.plotSignals)
        ? bridge.plotSignals()
        : []
    property var _enabled: ({})
    function _initEnabled() {
        var d = {}
        for (var i = 0; i < _catalogue.length; i++) {
            var k = _catalogue[i].key
            d[k] = (k === "adc:0" || k === "adc:1"
                 || k === "pwm:PD3" || k === "pwm:PD6")
        }
        _enabled = d
    }
    function _color(idx) { return _palette[idx % _palette.length] }
    function _enabledCount() {
        var n = 0
        for (var k in _enabled) if (_enabled[k]) n++
        return n
    }

    // ---- time-window selector (controls xAxis.min) ----------------
    readonly property var _windowOptions: [
        { label: "10 s", seconds: 10 },
        { label: "30 s", seconds: 30 },
        { label: "1 min", seconds: 60 },
        { label: "5 min", seconds: 300 }
    ]
    property int _windowIndex: 0
    property bool paused: false

    // ---- background ------------------------------------------------
    Rectangle { anchors.fill: parent; color: T.bg }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- header ------------------------------------------------
        PaneHeader {
            id: header
            Layout.fillWidth: true
            chip:    root.chip
            label:   root.label
            title:   "Plot"
            subtitle: root._enabledCount() + " trace" + (root._enabledCount() === 1 ? "" : "s")
            accent:  T.warning

            ComboBox {
                id: windowSelect
                model: root._windowOptions.map(function (w) { return w.label })
                currentIndex: root._windowIndex
                onActivated: root._windowIndex = currentIndex
                font.family: T.sansFamily
                font.pixelSize: T.fontSmall
                implicitWidth: 80
                implicitHeight: 26
                background: Rectangle {
                    color: T.bgDeep
                    border.color: T.border
                    radius: T.radiusSmall
                }
                contentItem: Text {
                    text: windowSelect.currentText
                    color: T.text
                    font: windowSelect.font
                    leftPadding: 8
                    rightPadding: 18
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                indicator: Text {
                    x: parent.width - 14
                    y: (parent.height - implicitHeight) / 2
                    text: "▾"
                    color: T.textMuted
                    font.pixelSize: 11
                }
                ToolTip.visible: hovered
                ToolTip.text: "Visible time window."
                ToolTip.delay: 500
            }
            PaneButton {
                text: root.paused ? "Resume" : "Pause"
                onClicked: root.paused = !root.paused
                tint: root.paused ? T.warning : T.text
                ToolTip.visible: hovered
                ToolTip.text: root.paused
                                ? "Resume live updates."
                                : "Freeze the plot but keep collecting data."
                ToolTip.delay: 500
            }
        }

        // ---- chart + legend ---------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ChartView {
                id: chart
                Layout.fillWidth: true
                Layout.fillHeight: true
                antialiasing: true
                theme: ChartView.ChartThemeDark
                backgroundColor: T.bg
                plotAreaColor: T.bgDeep
                legend.visible: false
                title: ""
                margins.left: 4; margins.right: 4
                margins.top: 6;  margins.bottom: 4

                ValueAxis {
                    id: xAxis
                    min: -root._windowOptions[root._windowIndex].seconds
                    max: 0
                    tickCount: 6
                    labelFormat: "%.0fs"
                    titleText: ""
                    gridLineColor: T.borderSubtle
                    minorGridLineColor: T.bgRaised
                    labelsColor: T.textMuted
                    color: T.border
                }
                ValueAxis {
                    id: yAdc
                    min: 0; max: 1023
                    tickCount: 5
                    labelFormat: "%.0f"
                    titleText: "ADC"
                    titleFont.pixelSize: T.fontSmall
                    gridLineColor: T.borderSubtle
                    labelsColor: T.textMuted
                    color: T.border
                }
                ValueAxis {
                    id: yPwm
                    min: 0; max: 1.0
                    tickCount: 5
                    labelFormat: "%.1f"
                    titleText: "PWM"
                    titleFont.pixelSize: T.fontSmall
                    gridLineColor: T.borderSubtle
                    labelsColor: T.textMuted
                    color: T.border
                }
            }

            // Legend / trace toggles
            Rectangle {
                Layout.preferredWidth: 144
                Layout.fillHeight: true
                color: T.bgRaised
                border.color: T.borderSubtle

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: T.padSmall
                    spacing: 1

                    Text {
                        text: "TRACES"
                        color: T.textDim
                        font.family: T.sansFamily
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                        Layout.bottomMargin: 2
                    }
                    Repeater {
                        model: root._catalogue
                        delegate: RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            PaneCheckBox {
                                checked: !!root._enabled[modelData.key]
                                onToggled: {
                                    var d = Object.assign({}, root._enabled)
                                    d[modelData.key] = checked
                                    root._enabled = d
                                }
                            }
                            // Color swatch — the line color in the chart.
                            Rectangle {
                                implicitWidth: 14
                                implicitHeight: 4
                                radius: 1
                                color: root._color(index)
                                opacity: root._enabled[modelData.key] ? 1 : 0.35
                            }
                            Text {
                                text: modelData.label
                                color: root._enabled[modelData.key] ? T.text : T.textDim
                                font.family: T.monoFamily
                                font.pixelSize: T.fontSmall
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }

    // ---- the actual line series ----------------------------------
    property var _series: ({})

    Component.onDestruction: {
        for (var k in _series) {
            chart.removeSeries(_series[k])
        }
    }

    function _ensureSeries() {
        for (var i = 0; i < _catalogue.length; i++) {
            var entry = _catalogue[i]
            if (_series[entry.key]) continue
            var ax = (entry.axis === "pwm") ? yPwm : yAdc
            var s = chart.createSeries(ChartView.SeriesTypeLine,
                                       entry.label, xAxis, ax)
            s.color = _color(i)
            s.width = 2
            _series[entry.key] = s
        }
    }

    function _refresh() {
        if (!bridge || !bridge.plotSeries) return
        if (root.paused) return
        _ensureSeries()
        for (var i = 0; i < _catalogue.length; i++) {
            var entry = _catalogue[i]
            var s = _series[entry.key]
            if (!s) continue
            if (!_enabled[entry.key]) {
                if (s.count > 0) s.clear()
                continue
            }
            var pts = bridge.plotSeries(chip, entry.key)
            s.clear()
            for (var j = 0; j < pts.length; j++) {
                s.append(pts[j].x, pts[j].y)
            }
        }
    }

    onActiveChanged: { if (active) Qt.callLater(_refresh) }

    Connections {
        target: bridge
        enabled: root.active
        function onPlotSeqChanged() { root._refresh() }
    }

    Component.onCompleted: {
        _initEnabled()
        _refresh()
    }
}
