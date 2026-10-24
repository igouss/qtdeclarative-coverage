import QtQml

QtObject {
    function once() { return 1 }
    property Timer timer: Timer { interval: 10; running: true; repeat: true }
    Component.onCompleted: {
        once()
        console.log("ready")
    }
}
