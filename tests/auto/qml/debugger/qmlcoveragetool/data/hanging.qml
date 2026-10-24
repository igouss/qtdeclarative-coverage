import QtQml

// Blocks its thread, so it never answers a snapshot request. Without a block: in debug mode, each
// pass through a block allocates a context, and the garbage collector waits for the loop to end.
QtObject {
    property Timer timer: Timer {
        interval: 10
        running: true
        onTriggered: {
            console.log("ready")
            while (true);
        }
    }
}
