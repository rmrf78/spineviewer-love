pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window

ListView {
    id: list
    required property UiMetrics metrics
    required property UiTheme theme
    property int selectedIndex: -1
    property var entries: []
    property real textSize: metrics.mainFont
    signal activated(int index)
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    currentIndex: selectedIndex
    model: rowData
    ListModel { id: rowData }
    onEntriesChanged: {
        let same = rowData.count === entries.length;
        for (let i=0;same && i<entries.length;++i)
            same = rowData.get(i).name === (typeof entries[i] === "string" ? entries[i] : entries[i].name);
        if (!same) rowData.clear();
        for (let i=0;i<entries.length;++i) {
            const value = {name:String(typeof entries[i] === "string" ? entries[i] : entries[i].name),duration:Number(entries[i].duration || 0)};
            if (same) rowData.set(i,value); else rowData.append(value);
        }
    }
    delegate: Rectangle {
        id: row
        required property int index
        required property var model
        readonly property var modelData: model
        width: list.width - (bar.visible ? bar.width : 0)
        height: list.textSize + list.metrics.spacing
        color: mouse.containsMouse ? list.theme.headerHover
             : list.selectedIndex === index ? list.theme.header : "transparent"
        Text {
            anchors.left: parent.left
            anchors.right: duration.left
            anchors.rightMargin: list.metrics.gap
            anchors.verticalCenter: parent.verticalCenter
            text: typeof row.modelData === "string" ? row.modelData : row.modelData.name
            textFormat: Text.PlainText
            font.pixelSize: list.textSize * list.metrics.fontEmScale
            color: list.theme.text
            clip: true
        }
        Text {
            id: duration
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: row.modelData.duration > 0 ? row.modelData.duration.toFixed(1) + "s" : ""
            font.pixelSize: list.textSize * list.metrics.fontEmScale
            color: list.theme.text
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            onPressed: {
                const item = Window.window ? Window.window.activeFocusItem : null;
                if (item && (item instanceof TextInput || item instanceof TextEdit)) item.focus = false;
            }
            onClicked: list.activated(row.index)
        }
    }
    ScrollBar.vertical: SlScrollBar { id: bar; metrics: list.metrics; theme: list.theme }
}
