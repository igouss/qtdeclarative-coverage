import QtQml

// Blocks its thread for 2 s after "ready", so it answers a snapshot request late. Without a block:
// see hanging.qml.
QtObject {
    function once() { return 1 }
    property Timer timer: Timer {
        interval: 10
        running: true
        onTriggered: {
            once()
            console.log("ready")
            const end = Date.now() + 2000
            while (Date.now() < end);
        }
    }
}
