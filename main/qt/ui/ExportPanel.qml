pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: exporter
    required property var shell
    anchors.fill: parent
    SlSlide {
        id: drawerSlide
        startValue: exporter.width
        targetValue: exporter.width - panel.width
        active: exporter.shell.exportOpen
        rate: 10
    }
    Rectangle {
        id: panel
        objectName: "exportPanel"
        width: exporter.shell.metrics.s(230)
        height: content.height + exporter.shell.metrics.s(20)
        x: drawerSlide.value
        y: exporter.shell.topInset + (exporter.height - exporter.shell.topInset - height) * .5
        visible: exporter.shell.loaded && exporter.shell.exportOpen
        color: exporter.shell.theme.popup
        radius: 10 * exporter.shell.metrics.pixel
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: function(wheel) { wheel.accepted = true; } }
        Column {
            id: content
            x: exporter.shell.metrics.s(10); y: x
            width: panel.width - x * 2
            spacing: exporter.shell.metrics.spacing
            SlButton { width: parent.width; metrics: exporter.shell.metrics; theme: exporter.shell.theme; text: ">>"; onClicked: exporter.shell.exportOpen = false }
            SlLabel { width: parent.width; metrics: exporter.shell.metrics; theme: exporter.shell.theme; lineHeight: metrics.detailFont; visible: text.length > 0; text: exporter.shell.read("exportStatus", ""); wrapMode: Text.Wrap }
            ProgressBar {
                width: parent.width; height: exporter.shell.metrics.s(18)
                visible: exporter.shell.read("exportRunning", false) || exporter.shell.read("exportStatus", "").length > 0
                value: exporter.shell.read("exportTotal", 0) > 0 ? exporter.shell.read("exportDone", 0) / exporter.shell.read("exportTotal", 1) : 0
            }
            SlSeparatorText { width: parent.width; metrics: exporter.shell.metrics; theme: exporter.shell.theme; text: qsTr("Snapshot") }
            Row {
                id: snapshots
                width: parent.width; spacing: exporter.shell.metrics.s(4)
                Repeater {
                    model: [{key:"export.png",label:"PNG",alpha:true},{key:"export.jpg",label:"JPG",alpha:false}]
                    delegate: SlButton {
                        required property var modelData
                        width: (snapshots.width - snapshots.spacing) * .5
                        metrics: exporter.shell.metrics; theme: exporter.shell.theme
                        text: modelData.label; enabled: !exporter.shell.read("exportRunning", false) && exporter.shell.can(modelData.key)
                        onClicked: exporter.shell.send(modelData.key, {alpha:modelData.alpha && exporter.shell.read("exportAlpha", true)})
                    }
                }
            }
            SlButton {
                width: parent.width; metrics: exporter.shell.metrics; theme: exporter.shell.theme
                text: exporter.shell.read("exportAlpha", true) ? qsTr("Alpha ON") : qsTr("Alpha OFF")
                highlighted: exporter.shell.read("exportAlpha", true); enabled: exporter.shell.can("export.alpha")
                onClicked: exporter.shell.send("export.alpha", !exporter.shell.read("exportAlpha", true))
            }
            SlButton {
                width: parent.width; metrics: exporter.shell.metrics; theme: exporter.shell.theme
                text: exporter.shell.read("exportQueue", false) ? qsTr("Queue ON") : qsTr("Queue OFF")
                highlighted: exporter.shell.read("exportQueue", false); enabled: exporter.shell.can("export.queue")
                onClicked: exporter.shell.send("export.queue", !exporter.shell.read("exportQueue", false))
            }
            SlSeparatorText { width: parent.width; metrics: exporter.shell.metrics; theme: exporter.shell.theme; text: qsTr("Export FPS") }
            Repeater {
                model: [{key:"export.imageFps",state:"exportImageFps",value:30,label:qsTr("Image")},{key:"export.videoFps",state:"exportVideoFps",value:60,label:qsTr("Video")}]
                delegate: Row {
                    id: fpsRow
                    required property var modelData
                    width: parent.width; spacing: exporter.shell.metrics.s(4)
                    function setFps(value) { exporter.shell.send(modelData.key, Math.max(1, Math.min(120, Math.round(value)))); }
                    SlTextField {
                        id: fps
                        width: exporter.shell.metrics.s(78); height: exporter.shell.metrics.buttonHeight
                        externalText: exporter.shell.read(fpsRow.modelData.state, fpsRow.modelData.value).toString()
                        metrics: exporter.shell.metrics; theme: exporter.shell.theme; textSize: metrics.mainFont
                        padding: 0
                        validator: IntValidator { bottom: 1; top: 120 }
                        enabled: exporter.shell.can(fpsRow.modelData.key)
                        onEditingFinished: { if (text.length) fpsRow.setFps(Number(text)); }
                    }
                    SlButton { width: exporter.shell.metrics.s(34); metrics: exporter.shell.metrics; theme: exporter.shell.theme; text: "-"; enabled: Number(fps.text) > 1 && fps.enabled; onClicked: fpsRow.setFps(Number(fps.text) - 1) }
                    SlButton { width: exporter.shell.metrics.s(34); metrics: exporter.shell.metrics; theme: exporter.shell.theme; text: "+"; enabled: Number(fps.text) < 120 && fps.enabled; onClicked: fpsRow.setFps(Number(fps.text) + 1) }
                    SlLabel { metrics: exporter.shell.metrics; theme: exporter.shell.theme; text: fpsRow.modelData.label }
                }
            }
            SlSeparatorText { width: parent.width; metrics: exporter.shell.metrics; theme: exporter.shell.theme; text: qsTr("Frames") }
            Row {
                id: frames
                width: parent.width; spacing: exporter.shell.metrics.s(4)
                Repeater {
                    model: [{key:"export.pngFrames",label:"PNG",alpha:true},{key:"export.jpgFrames",label:"JPG",alpha:false}]
                    delegate: SlButton {
                        required property var modelData
                        width: (frames.width - frames.spacing) * .5
                        metrics: exporter.shell.metrics; theme: exporter.shell.theme; text: modelData.label
                        enabled: !exporter.shell.read("exportRunning", false) && exporter.shell.can(modelData.key)
                        onClicked: exporter.shell.send(modelData.key, {alpha:modelData.alpha && exporter.shell.read("exportAlpha", true)})
                    }
                }
            }
            SlSeparatorText { width: parent.width; metrics: exporter.shell.metrics; theme: exporter.shell.theme; text: qsTr("Video") }
            Row {
                id: videos
                width: parent.width; spacing: exporter.shell.metrics.s(4)
                Repeater {
                    model: [{key:"export.mp4",label:"MP4",alpha:false},{key:"export.webm",label:"WebM",alpha:true},{key:"export.gif",label:"GIF",alpha:false}]
                    delegate: SlButton {
                        required property var modelData
                        width: (videos.width - videos.spacing * 2) / 3
                        metrics: exporter.shell.metrics; theme: exporter.shell.theme; text: modelData.label
                        enabled: !exporter.shell.read("exportRunning", false) && exporter.shell.can(modelData.key)
                        onClicked: exporter.shell.send(modelData.key, {alpha:modelData.alpha && exporter.shell.read("exportAlpha", true)})
                    }
                }
            }
        }
    }
    MouseArea {
        id: edge
        x: exporter.width - width; y: exporter.shell.topInset
        width: exporter.shell.metrics.s(133.3); height: exporter.height - y
        visible: exporter.shell.loaded && exporter.shell.exportEverOpened && !exporter.shell.exportOpen
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
    }
    SlSlide {
        id: returnSlide
        startValue: exporter.width
        targetValue: edge.containsMouse || returnButton.hovered ? exporter.width - returnButton.width - exporter.shell.metrics.s(4) : exporter.width
        active: edge.visible
        rate: 8
    }
    SlButton {
        id: returnButton
        objectName: "exportReturn"
        width: exporter.shell.metrics.s(133.3); height: exporter.shell.metrics.s(33.3)
        x: returnSlide.value
        y: exporter.shell.topInset + (exporter.height - exporter.shell.topInset - height) * .5
        visible: edge.visible
        metrics: exporter.shell.metrics; theme: exporter.shell.theme; text: "<<"
        onClicked: exporter.shell.toggleExport()
    }
}
