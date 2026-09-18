pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Popup {
    id: dialog
    objectName: "settingsDialog"
    required property var shell
    property int page: 0
    property int scrollPage: 0
    property var pageScrollOffsets: ({})
    onPageChanged: {
        if (scrollPage !== 0) pageScrollOffsets[scrollPage] = scroll.contentY;
        scrollPage = page;
        scroll.cancelFlick();
        Qt.callLater(function() {

            pages.forceLayout();
            scroll.contentY = Math.max(0, Math.min(pageScrollOffsets[page] || 0, scroll.contentHeight - scroll.height));
        });
    }
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: shell.metrics.s(800)
    height: shell.metrics.s(480)
    padding: 8 * shell.metrics.pixel
    topPadding: (8 + shell.metrics.baseFontPixels + 8) * shell.metrics.pixel
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    onOpened: shell.send("settings.modal", {source:"settings",open:true})
    onClosed: shell.send("settings.modal", {source:"settings",open:false})
    background: Rectangle {
        color: dialog.shell.theme.popup
        radius: 10 * dialog.shell.metrics.pixel
        border.color: dialog.shell.theme.separator
        Rectangle {
            width: parent.width
            height: (dialog.shell.metrics.baseFontPixels + 8) * dialog.shell.metrics.pixel
            color: dialog.shell.theme.titleActive
            Text { anchors.fill: parent; anchors.leftMargin: 4 * dialog.shell.metrics.pixel; verticalAlignment: Text.AlignVCenter; font.pixelSize: dialog.shell.metrics.baseFontPixels * dialog.shell.metrics.pixel * dialog.shell.metrics.fontEmScale; text: "SettingWindow"; color: dialog.shell.theme.text }
        }
    }
    contentItem: Item {
        Column {
            visible: dialog.page === 0
            width: parent.width
            spacing: dialog.shell.metrics.spacing
            Repeater {
                model: [{page:-1,key:"background.open",label:qsTr("Background")},{page:2,label:qsTr("Language")},{page:3,label:qsTr("Theme")},{page:5,label:qsTr("Resolution")},{page:4,label:qsTr("Render BG Color")}]
                delegate: SlButton {
                    required property var modelData
                    objectName: "settingsPage_"+modelData.page
                    width: parent.width; height: dialog.shell.metrics.s(53.3)
                    metrics: dialog.shell.metrics; theme: dialog.shell.theme; text: modelData.label
                    enabled: modelData.page >= 0 || dialog.shell.can(modelData.key)
                    onClicked: modelData.page < 0 ? dialog.shell.send(modelData.key, null) : dialog.page = modelData.page
                }
            }
        }
        Flickable {
            id: scroll
            objectName: "settingsContent"
            visible: dialog.page !== 0
            width: parent.width
            height: parent.height - dialog.shell.metrics.s(66.7)
            contentWidth: width
            contentHeight: pages.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Column {
                id: pages
                width: scroll.width
                spacing: dialog.shell.metrics.spacing
                Column {
                    width: parent.width; spacing: dialog.shell.metrics.spacing; visible: dialog.page === 2
                    SlLabel { width: parent.width; metrics: dialog.shell.metrics; theme: dialog.shell.theme; text: qsTr("Language Settings") }
                    Repeater {
                        model: dialog.shell.read("languages", [])
                        delegate: SlButton {
                            required property var modelData
                            width: parent.width; height: dialog.shell.metrics.s(53.3)
                            metrics: dialog.shell.metrics; theme: dialog.shell.theme; text: modelData.name
                            highlighted: dialog.shell.read("language", "") === modelData.id
                            enabled: dialog.shell.can("settings.language")
                            onClicked: dialog.shell.send("settings.language", modelData.id)
                        }
                    }
                }
                Column {
                    width: parent.width; spacing: dialog.shell.metrics.spacing; visible: dialog.page === 3
                    SlLabel { width: parent.width; metrics: dialog.shell.metrics; theme: dialog.shell.theme; text: qsTr("Theme") }
                    SlSeparator { width: parent.width; metrics: dialog.shell.metrics; theme: dialog.shell.theme }
                    SlButton { width: parent.width; height: dialog.shell.metrics.s(40); metrics: dialog.shell.metrics; theme: dialog.shell.theme; text: qsTr("Title BG"); enabled: dialog.shell.can("title.background"); onClicked: dialog.shell.send("title.background", null) }
                    Repeater {
                        model: [{key:"theme.hue",state:"themeHue",value:.74,label:qsTr("Hue##theme").split("##")[0],format:qsTr("Hue %.2f")},{key:"theme.saturation",state:"themeSaturation",value:.83,label:qsTr("Saturation##theme").split("##")[0],format:qsTr("Saturation %.2f")},{key:"theme.brightness",state:"themeBrightness",value:1,label:qsTr("Brightness##theme").split("##")[0],format:qsTr("Brightness %.2f")}]
                        delegate: Row {
                            id: hsvRow
                            required property var modelData
                            width: parent.width
                            spacing: dialog.shell.metrics.spacingX
                            SlSlider {
                                width: hsvRow.width*.65; metrics: dialog.shell.metrics; theme: dialog.shell.theme
                                from: 0; to: 1; value: dialog.shell.read(hsvRow.modelData.state,hsvRow.modelData.value)
                                displayText: hsvRow.modelData.format.replace("%.2f",value.toFixed(2))
                                enabled: dialog.shell.can(hsvRow.modelData.key)
                                onValueEdited: function(newValue) { dialog.shell.send(hsvRow.modelData.key,newValue); }
                            }
                            SlLabel { width: Math.max(0,hsvRow.width*.35-hsvRow.spacing); metrics: dialog.shell.metrics; theme: dialog.shell.theme; text: hsvRow.modelData.label }
                        }
                    }
                    Row {
                        width: parent.width; spacing: dialog.shell.metrics.spacingX
                        SlSlider {
                            id: fontSize
                            objectName: "fontSizeDraft"
                            property real appliedFont: dialog.shell.read("baseFontPixels",16)
                            onAppliedFontChanged: value = appliedFont
                            width: parent.width - applyFont.width - parent.spacing
                            metrics: dialog.shell.metrics; theme: dialog.shell.theme
                            from: 10; to: 50; value: appliedFont; localDraft: true
                            displayText: qsTr("Font Size")+" "+value.toFixed(1)
                            enabled: dialog.shell.can("theme.fontSize")
                        }
                        SlButton { id: applyFont; width: dialog.shell.metrics.s(100); metrics: dialog.shell.metrics; theme: dialog.shell.theme; text: qsTr("Apply"); enabled: fontSize.enabled; onClicked: dialog.shell.send("theme.fontSize", fontSize.value) }
                    }
                    SlButton { width: parent.width; height: dialog.shell.metrics.s(40); metrics: dialog.shell.metrics; theme: dialog.shell.theme; text: (dialog.shell.read("darkTheme",false)?qsTr("Dark Mode: ON##theme"):qsTr("Dark Mode: OFF##theme")).split("##")[0]; highlighted: dialog.shell.read("darkTheme", false); enabled: dialog.shell.can("theme.dark"); onClicked: dialog.shell.send("theme.dark", !dialog.shell.read("darkTheme", false)) }
                    SlButton { width: parent.width; height: dialog.shell.metrics.s(40); metrics: dialog.shell.metrics; theme: dialog.shell.theme; text: qsTr("Reset to Default##theme").split("##")[0]; enabled: dialog.shell.can("theme.reset"); onClicked: dialog.shell.send("theme.reset", null) }
                }
                Column {
                    width: parent.width; spacing: dialog.shell.metrics.spacing; visible: dialog.page === 4
                    SlLabel { width: parent.width; metrics: dialog.shell.metrics; theme: dialog.shell.theme; text: qsTr("Render BG Color") }
                    SlColorPicker {
                        width: Math.max(0,Math.min(parent.width,scroll.height-dialog.shell.metrics.s(45.3)))
                        metrics: dialog.shell.metrics; theme: dialog.shell.theme
                        sourceColor: dialog.shell.read("renderBackground", "black")
                        enabled: dialog.shell.can("background.setColor")
                        onEdited: function(value) { dialog.shell.send("background.setColor",value.toString()); }
                    }
                    SlButton { width: parent.width; height: dialog.shell.metrics.s(40); metrics: dialog.shell.metrics; theme: dialog.shell.theme; text: qsTr("Reset to Default##renderbg").split("##")[0]; enabled: dialog.shell.can("background.resetColor"); onClicked: dialog.shell.send("background.resetColor", null) }
                }
                Column {
                    width: parent.width; spacing: dialog.shell.metrics.spacing; visible: dialog.page === 5
                    SlLabel { width: parent.width; metrics: dialog.shell.metrics; theme: dialog.shell.theme; text: qsTr("Resolution Settings") }
                    SlSeparator { width: parent.width; metrics: dialog.shell.metrics; theme: dialog.shell.theme }
                    Repeater {
                        model: [qsTr("Default"),"1920x1080","1920x1200","2560x1440","2560x1600","2880x1620","2880x1800"]
                        delegate: SlButton {
                            required property string modelData
                            required property int index
                            width: parent.width; height: dialog.shell.metrics.s(53.3)
                            metrics: dialog.shell.metrics; theme: dialog.shell.theme; text: modelData
                            highlighted: dialog.shell.read("resolutionPreset", 0) === index
                            enabled: dialog.shell.can("settings.resolution")
                            onClicked: dialog.shell.send("settings.resolution", index)
                        }
                    }
                }
            }
            ScrollBar.vertical: SlScrollBar { metrics: dialog.shell.metrics; theme: dialog.shell.theme }
        }
        SlButton {
            objectName: "settingsBack"
            anchors.bottom: parent.bottom
            anchors.bottomMargin: dialog.shell.metrics.s(13.4)
            width: parent.width; height: dialog.shell.metrics.s(53.3)
            metrics: dialog.shell.metrics; theme: dialog.shell.theme
            text: dialog.page === 0 ? qsTr("Close") : qsTr("Back")
            onClicked: dialog.page === 0 ? dialog.close() : dialog.page = 0
        }
    }
}
