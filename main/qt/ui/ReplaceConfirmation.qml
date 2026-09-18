pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Popup {
    id: dialog
    required property var shell
    readonly property var request: shell.read("replaceConfirmation", {})
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: shell.metrics.s(760)
    height: shell.metrics.s(250)
    padding: 8 * shell.metrics.pixel
    topPadding: (shell.metrics.baseFontPixels + 16) * shell.metrics.pixel
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    visible: request.open === true
    background: Rectangle {
        color: dialog.shell.theme.popup
        radius: 10 * dialog.shell.metrics.pixel
        border.color: dialog.shell.theme.separator
        Rectangle {
            width: parent.width
            height: (dialog.shell.metrics.baseFontPixels + 8) * dialog.shell.metrics.pixel
            color: dialog.shell.theme.titleActive
            Text { anchors.fill: parent; anchors.leftMargin: 4 * dialog.shell.metrics.pixel; font.pixelSize: dialog.shell.metrics.baseFontPixels * dialog.shell.metrics.pixel * dialog.shell.metrics.fontEmScale; verticalAlignment: Text.AlignVCenter; text: dialog.request.title || qsTr("Warning"); textFormat: Text.PlainText; color: dialog.shell.theme.text }
        }
    }
    contentItem: Item {
        SlLabel { y: dialog.shell.metrics.s(10); width: parent.width; metrics: dialog.shell.metrics; theme: dialog.shell.theme; text: dialog.request.message || ""; wrapMode: Text.Wrap }
        Row {
            width: parent.width
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Math.max(0, dialog.shell.metrics.s(22) - dialog.bottomPadding)
            spacing: dialog.shell.metrics.s(12)
            SlButton { objectName: "replaceCancel"; width: (parent.width - parent.spacing) * .5; height: dialog.shell.metrics.s(46); metrics: dialog.shell.metrics; theme: dialog.shell.theme; text: qsTr("Cancel"); onClicked: dialog.shell.send("replace.cancel", null) }
            SlButton { objectName: "replaceContinue"; width: (parent.width - parent.spacing) * .5; height: dialog.shell.metrics.s(46); metrics: dialog.shell.metrics; theme: dialog.shell.theme; text: qsTr("Continue"); onClicked: dialog.shell.send("replace.confirm", null) }
        }
    }
}
