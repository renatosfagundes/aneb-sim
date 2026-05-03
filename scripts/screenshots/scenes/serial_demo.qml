// serial_demo.qml — wraps SerialConsole with sample UART output for
// the screenshot tool.  Imports from "../../../aneb-ui/aneb_ui/qml"
// where the actual pane components live.
import QtQuick 2.15
import "../../../aneb-ui/aneb_ui/qml" as Aneb

Rectangle {
    width: 600
    height: 380
    color: "#0a1a14"

    Aneb.SerialConsole {
        id: console_
        anchors.fill: parent
        chip:  "ecu1"
        label: "ECU 1"
    }

    // Inject sample UART output so the terminal area isn't empty in
    // the screenshot.  Done after the component finishes loading;
    // we walk the visible TextArea via objectName isn't available,
    // so we re-emit through the bridge — simpler.
    Component.onCompleted: {
        var sample = [
            "[boot] dashboard_full v1.4",
            "[ecu] ADC0=812 ADC1=540 ADC2=210 ADC3=703",
            "lights:1,seatbeltUnbuckled:0,turnLeft:1,turnRight:0,cruiseActive:1,serviceDue:0,tirePressureLow:0",
            "lights:1,seatbeltUnbuckled:0,turnLeft:1,turnRight:0,cruiseActive:1,serviceDue:0,tirePressureLow:0",
            "[can] tx id=0x123 dlc=2 data=CAFE",
            "lights:1,seatbeltUnbuckled:0,turnLeft:0,turnRight:1,cruiseActive:1,serviceDue:0,tirePressureLow:0",
            "[adc] hi=703 lo=210",
            "lights:1,seatbeltUnbuckled:1,turnLeft:0,turnRight:0,cruiseActive:0,serviceDue:0,tirePressureLow:0",
            ""
        ].join("\n")
        bridge.uartAppended("ecu1", sample)
    }
}
