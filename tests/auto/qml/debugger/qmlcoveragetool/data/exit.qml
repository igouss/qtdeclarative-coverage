import QtQml
import "util.js" as Util

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
        sumTo(Util.three())
        Qt.callLater(Qt.quit)
    }
}
