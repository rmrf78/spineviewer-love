pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: layers
    required property var shell
    width: shell.metrics.s(286)
    height: shell.metrics.s(66) + Math.min(8, shell.read("loadedSpines", []).length) * (shell.metrics.s(32) + shell.metrics.spacing) - shell.metrics.spacing
    x: shell.width - width - shell.metrics.s(10)
    y: shell.topInset + shell.metrics.s(10)
    color: shell.theme.popup
    radius: 10 * shell.metrics.pixel
    DragHandler { target: layers }
    Column {
        anchors.fill: parent; anchors.margins: 10 * layers.shell.metrics.pixel
        spacing: layers.shell.metrics.spacing
        SlLabel { metrics: layers.shell.metrics; theme: layers.shell.theme; text: qsTr("Spines") }
        ListView {
            width: parent.width; height: parent.height - y; clip: true
            model: layers.shell.read("loadedSpines", [])
            spacing: layers.shell.metrics.spacing
            delegate: Row {
                id: row
                required property int index
                required property var modelData
                width: ListView.view.width - layers.shell.metrics.scrollbarWidth
                spacing: layers.shell.metrics.s(6)
                Row {
                    spacing: layers.shell.metrics.s(2)
                    SlButton { width: layers.shell.metrics.s(20); height: layers.shell.metrics.s(32); metrics: layers.shell.metrics; theme: layers.shell.theme; text: "^"; enabled: row.index > 0 && layers.shell.can("layer.up"); onClicked: layers.shell.send("layer.up", row.index) }
                    SlButton { width: layers.shell.metrics.s(20); height: layers.shell.metrics.s(32); metrics: layers.shell.metrics; theme: layers.shell.theme; text: "v"; enabled: row.index + 1 < layers.shell.read("loadedSpines", []).length && layers.shell.can("layer.down"); onClicked: layers.shell.send("layer.down", row.index) }
                }
                SlButton { width: Math.max(0, row.width - layers.shell.metrics.s(90)); height: layers.shell.metrics.s(32); metrics: layers.shell.metrics; theme: layers.shell.theme; text: row.modelData.name; highlighted: row.modelData.selected; enabled: layers.shell.can("layer.select"); onClicked: layers.shell.send("layer.select", row.index) }
                SlCheckBox { width: layers.shell.metrics.s(32); metrics: layers.shell.metrics; theme: layers.shell.theme; checked: row.modelData.visible; enabled: layers.shell.can("layer.visible"); onClicked: layers.shell.send("layer.visible", row.index) }
            }
            ScrollBar.vertical: SlScrollBar { metrics: layers.shell.metrics; theme: layers.shell.theme; policy: ScrollBar.AlwaysOn }
        }
    }
}
