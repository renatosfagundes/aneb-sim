// Plotter.qml — rolling-window chart of selected signals from one
// chip. Shown inside a PlotterWindow.
//
// Signals come from `bridge.plotSignals()` (static catalogue). Their
// data comes from `bridge.plotSeries(chip, key)` and is refreshed on
// `bridge.plotSeqChanged` (fired ~20 Hz by the bridge's sample
// timer). The chart's X axis is "seconds before now"; the most
// recent sample sits at x=0 and older samples slide left.
//
// Three Y axes:
//   - ADC counts (0..1023) on the left
//   - PWM duty   (0..1)   on the right
//   - digital states are stacked at y values 0..N inside a small
//     band on top of the chart, one row per active digital trace
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtCharts 2.15

Item {
    id: root
    property string chip: ""

    // Static palette — colors cycle through traces as they're enabled.
    readonly property var _palette: [
        "#ff5566", "#22cc44", "#3aaaff", "#ffcc44",
        "#bb88ff", "#ff8855", "#44ddcc", "#cc6699",
        "#88ccff", "#ddaa44", "#66cc88", "#ff66bb",
        "#5566ff", "#aaaaaa"
    ]

    // [{ key, label, axis }] from the bridge.
    readonly property var _catalogue:
        (typeof bridge !== "undefined" && bridge && bridge.plotSignals)
        ? bridge.plotSignals()
        : []

    // Per-row visibility flags (parallel to _catalogue). Toggled by
    // the checkboxes in the legend.
    property var _enabled: ({})

    function _initEnabled() {
        var d = {}
        // Sensible defaults: AIN0, AIN1, the two main PWMs.
        for (var i = 0; i < _catalogue.length; i++) {
            var k = _catalogue[i].key
            d[k] = (k === "adc:0" || k === "adc:1"
                 || k === "pwm:PD3" || k === "pwm:PD6")
        }
        _enabled = d
    }

    function _color(idx) { return _palette[idx % _palette.length] }

    // ---- layout: chart on the left, legend on the right -----------
    RowLayout {
        anchors.fill: parent
        spacing: 4

        ChartView {
            id: chart
            Layout.fillWidth: true
            Layout.fillHeight: true
            antialiasing: true
            theme: ChartView.ChartThemeDark
            backgroundColor: "#0a1612"
            legend.visible: false
            margins.left: 4; margins.right: 4
            margins.top: 4;  margins.bottom: 4
            title: chip.toUpperCase()

            ValueAxis {
                id: xAxis
                min: -10; max: 0
                tickCount: 6
                labelFormat: "%.0fs"
                titleText: ""
            }
            ValueAxis {
                id: yAdc
                min: 0; max: 1023
                tickCount: 5
                labelFormat: "%.0f"
                titleText: "ADC"
            }
            ValueAxis {
                id: yPwm
                min: 0; max: 1.0
                tickCount: 5
                labelFormat: "%.1f"
                titleText: "PWM"
            }
        }

        // Legend / trace toggles
        Rectangle {
            Layout.preferredWidth: 130
            Layout.fillHeight: true
            color: "#0a1612"
            border.color: "#1a3024"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 2

                Repeater {
                    model: root._catalogue
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        CheckBox {
                            checked: !!root._enabled[modelData.key]
                            onToggled: {
                                var d = Object.assign({}, root._enabled)
                                d[modelData.key] = checked
                                root._enabled = d
                            }
                            implicitWidth: 18
                            implicitHeight: 18
                        }
                        Rectangle {
                            implicitWidth: 12; implicitHeight: 4
                            radius: 1
                            color: root._color(index)
                        }
                        Text {
                            text: modelData.label
                            color: "#cdfac0"
                            font.family: "Consolas"
                            font.pixelSize: 10
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }

    // ---- the actual line series -----------------------------------
    //
    // QML's Repeater can't create LineSeries inside a ChartView
    // declaratively at run-time, so we instantiate them imperatively
    // once and look them up by key. Series for unselected traces are
    // emptied out (no points), which makes them invisible without
    // tearing them down.

    property var _series: ({})

    Component.onDestruction: {
        // Avoid leaking series objects when the window closes.
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
            // QtCharts has no "set all points" API in older
            // versions, so clear+append is the portable path.
            s.clear()
            for (var j = 0; j < pts.length; j++) {
                s.append(pts[j].x, pts[j].y)
            }
        }
    }

    // `active` is set by the parent window — when false (window hidden)
    // we skip all chart work entirely and reconnect on first show.
    property bool active: true
    onActiveChanged: { if (active) Qt.callLater(_refresh) }

    Connections {
        target: bridge
        enabled: root.active
        function onPlotSeqChanged() { root._refresh() }
    }

    // Single onCompleted handler — initialises the trace toggles and
    // does a first refresh so the chart isn't blank before the first
    // plotSeqChanged tick arrives.
    Component.onCompleted: {
        _initEnabled()
        _refresh()
    }
}
