pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window

Column {
    id: skins
    required property var shell
    spacing: shell.metrics.spacing
    SlSeparator { width: parent.width; metrics: skins.shell.metrics; theme: skins.shell.theme }
    Item {
        width: parent.width; height: mix.height
        SlLabel { metrics: skins.shell.metrics; theme: skins.shell.theme; text: qsTr("Skin") }
        SlCheckBox {
            id: mix
            x: Math.max(0, skins.shell.metrics.panelWidth - skins.shell.metrics.s(80) - skins.shell.metrics.gap)
            metrics: skins.shell.metrics; theme: skins.shell.theme
            text: qsTr("Mix"); checked: skins.shell.read("skinMix", false)
            enabled: skins.shell.can("skin.mixMode")
            onClicked: skins.shell.send("skin.mixMode", checked)
        }
    }
    SlSeparator { width: parent.width; metrics: skins.shell.metrics; theme: skins.shell.theme }
    ListView {
        id: list
        objectName: "skinList"
        width: parent.width; height: Math.max(0, skins.height - y - (mix.checked ? 30 * skins.shell.metrics.pixel : 0))
        spacing: mix.checked ? skins.shell.metrics.spacing : 0
        model: skins.shell.read("skins", [])
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        delegate: Item {
            id: skinRow
            required property int index
            required property string modelData
            width: list.width - (scroll.visible ? scroll.width : 0)
            height: skins.shell.metrics.mainFont + (mix.checked ? skins.shell.metrics.framePaddingY * 2 : skins.shell.metrics.spacing)
            SlCheckBox {
                anchors.fill: parent
                visible: mix.checked
                metrics: skins.shell.metrics; theme: skins.shell.theme; lineHeight: metrics.mainFont
                text: skinRow.modelData
                checked: skins.shell.read("selectedSkins", []).indexOf(skinRow.index) >= 0
                enabled: skins.shell.can("skin.toggle")
                onClicked: skins.shell.send("skin.toggle", skinRow.index)
            }
            Rectangle {
                anchors.fill: parent; visible: !mix.checked
                color: click.containsMouse ? skins.shell.theme.headerHover : skins.shell.read("selectedSkins", []).indexOf(skinRow.index) >= 0 ? skins.shell.theme.header : "transparent"
                SlLabel { anchors.fill: parent; metrics: skins.shell.metrics; theme: skins.shell.theme; text: skinRow.modelData }
                MouseArea {
                    id: click
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: skins.shell.can("skin.select")
                    onPressed: {
                        const item = Window.window ? Window.window.activeFocusItem : null;
                        if (item && (item instanceof TextInput || item instanceof TextEdit)) item.focus = false;
                    }
                    onClicked: skins.shell.send("skin.select", skinRow.index)
                }
            }
        }
        ScrollBar.vertical: SlScrollBar { id: scroll; metrics: skins.shell.metrics; theme: skins.shell.theme }
    }
}
