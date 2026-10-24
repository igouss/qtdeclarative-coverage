import QtQml
import "first.js" as First

QtObject {
    Component.onCompleted: {
        First.first(1)
        Qt.callLater(Qt.quit)
    }
}
