import QtQml

QtObject {
    function once() { return 1 }
    Component.onCompleted: once()
}
