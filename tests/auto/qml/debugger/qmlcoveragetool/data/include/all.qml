import QtQml
import "lib.js" as Lib
import "module.mjs" as Module

QtObject {
    property Widget widget: Widget { }
    Component.onCompleted: {
        Lib.used()
        Module.square(2)
        Qt.callLater(Qt.quit)
    }
}
