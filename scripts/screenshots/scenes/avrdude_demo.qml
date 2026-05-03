// avrdude_demo.qml — fakes a finished avrdude flash so AvrdudeWindow's
// log is populated for the screenshot.  We bypass the actual Window
// wrapper (top-level OS chrome) and embed the inner content in a
// Rectangle so it can be grabbed off-screen.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../../aneb-ui/aneb_ui/qml" as Aneb
import "../../../aneb-ui/aneb_ui/qml/Theme.js" as T

Rectangle {
    width: 640
    height: 380
    color: T.bg

    // We can't instantiate AvrdudeWindow directly off-screen (it's
    // a top-level Window).  Reproduce the same content layout here —
    // PaneHeader + log + footer — and feed it sample lines.
    property bool busy: false
    property string statusLabel: "Success"
    property color  statusColor: T.success

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Aneb.PaneHeader {
            id: header
            Layout.fillWidth: true
            chip: "ecu1"; label: "ECU 1"
            title: "Flash"
            subtitle: parent.parent.statusLabel
            accent: parent.parent.statusColor

            Aneb.StatusDot {
                baseColor: T.success
                active: true
                pulse: false
            }
            Aneb.PaneButton { text: "Flash again" }
            Aneb.PaneButton { text: "Copy"; borderless: true }
            Aneb.PaneButton { text: "Clear"; borderless: true }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: T.bgDeep
            border.color: T.borderSubtle
            ScrollView {
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                TextArea {
                    id: log
                    readOnly: true
                    wrapMode: TextArea.NoWrap
                    textFormat: TextEdit.RichText
                    selectByMouse: true
                    font.family: T.monoFamily
                    font.pixelSize: T.fontBody
                    background: null
                    leftPadding: 8; rightPadding: 8
                    topPadding: 6;  bottomPadding: 6
                    color: T.text
                    Component.onCompleted: {
                        var lines = [
                            ["Avrdude.EXE version 8.1",                 T.text],
                            ["Using port            : net:127.0.0.1:8600", T.text],
                            ["Using programmer      : arduino",          T.text],
                            ["Setting baud rate     : 115200",           T.text],
                            ["AVR device initialized and ready to accept instructions", T.text],
                            ["Device signature = 1E 95 0F (ATmega328P, ATA6614Q, LGT8F328P)", T.text],
                            ["Reading 13104 bytes for flash from input file dashboard_full.hex", T.info],
                            ["Writing 13104 bytes to flash",             T.info],
                            ["Writing | ################################################## | 100% 1.65s", T.info],
                            ["Reading | ################################################## | 100% 1.50s", T.info],
                            ["13104 bytes of flash verified",            T.success],
                            ["Avrdude.EXE done.  Thank you.",            T.success],
                        ]
                        var html = ""
                        for (var i = 0; i < lines.length; i++) {
                            html += "<span style=\"color:" + lines[i][1] + "\">"
                                  + lines[i][0]
                                        .replace(/&/g, "&amp;")
                                        .replace(/</g, "&lt;")
                                        .replace(/>/g, "&gt;")
                                  + "</span><br/>"
                        }
                        log.text = html
                    }
                }
            }
        }
    }
}
