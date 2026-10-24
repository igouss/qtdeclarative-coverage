import QtQml

QtObject {
    property int ticks: 0
    function once() { return 1 }
    function tick() { ++ticks }
    property Timer timer: Timer {
        interval: 10
        running: true
        repeat: true
        onTriggered: tick()
    }
    Component.onCompleted: once()
}
