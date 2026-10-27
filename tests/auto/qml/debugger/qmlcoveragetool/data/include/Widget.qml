import QtQml

QtObject {
    property int value: 2 * 3
    function twice(x) {
        return x * 2
    }
    Component.onCompleted: value = twice(value)
}
