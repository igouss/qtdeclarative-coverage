import QtQuick
import QtTest

TestCase {
    name: "Calc"
    function twice(x) { return 2 * x }
    function test_twice() { compare(twice(2), 4) }
}
