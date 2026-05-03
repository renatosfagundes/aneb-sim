// PaneHeader.qml — themed title strip used at the top of every floating
// pane (Console / Plot / Avrdude).  Provides a consistent visual anchor
// — chip badge on the left, pane title in the middle, slot for trailing
// controls on the right — so the panes feel like they belong to the
// same app instead of three different bare windows.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "Theme.js" as T

Rectangle {
    id: root
    property string chip:    ""
    property string label:   ""
    property string title:   ""              // e.g. "Console"
    property string subtitle: ""             // e.g. "8700  •  COM11"
    property color  accent:  T.accent        // pane-specific tint dot
    default property alias trailing: trailingRow.children

    implicitHeight: 38
    color: T.bgRaised
    border.color: T.border

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: T.pad
        anchors.rightMargin: T.padSmall
        spacing: T.pad

        // Chip badge — small rounded rect with the chip's friendly name.
        Rectangle {
            implicitWidth: chipText.implicitWidth + 16
            implicitHeight: 24
            radius: T.radiusSmall
            color: T.bgHover
            border.color: T.border
            Text {
                id: chipText
                anchors.centerIn: parent
                text: root.label.length ? root.label : root.chip.toUpperCase()
                color: T.textBright
                font.family: T.sansFamily
                font.pixelSize: T.fontSmall
                font.weight: Font.DemiBold
            }
        }

        // Accent dot + pane title.
        Rectangle {
            implicitWidth: 6; implicitHeight: 6; radius: 3
            color: root.accent
            opacity: 0.85
        }
        Text {
            text: root.title
            color: T.text
            font.family: T.sansFamily
            font.pixelSize: T.fontHeader
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }
        // Optional muted subtitle (port info, byte count, etc.).
        Text {
            text: root.subtitle
            color: T.textMuted
            font.family: T.monoFamily
            font.pixelSize: T.fontSmall
            verticalAlignment: Text.AlignVCenter
            visible: root.subtitle.length > 0
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Item { Layout.fillWidth: root.subtitle.length === 0 }

        RowLayout {
            id: trailingRow
            spacing: T.padSmall
        }
    }

    // Subtle bottom edge — separates header from content.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: T.borderSubtle
    }
}
