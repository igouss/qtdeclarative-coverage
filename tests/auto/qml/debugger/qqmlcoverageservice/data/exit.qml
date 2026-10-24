import QtQml

QtObject {
    function add(a, b) { return a + b }
    function neverCalled() { return 0 }
    function sumTo(n) {
        let total = 0
        for (let i = 0; i < n; ++i)
            total = add(total, i)
        return total
    }
    Component.onCompleted: {
        sumTo(3)
        Qt.callLater(Qt.quit)
    }
}
