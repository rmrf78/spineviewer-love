pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window

Button {
    id: control
    required property UiMetrics metrics
    required property UiTheme theme
    property string tip: ""
    property real lineHeight: metrics.mainFont
    implicitHeight: lineHeight === metrics.mainFont ? metrics.buttonHeight : lineHeight + metrics.framePaddingY * 2
    implicitWidth: Math.max(0, contentItem.implicitWidth + metrics.framePaddingX * 2)
    padding: 0
    leftPadding: metrics.framePaddingX
    rightPadding: metrics.framePaddingX
    font.pixelSize: lineHeight * metrics.fontEmScale
    focusPolicy: Qt.NoFocus
    onPressed: {
        const item = Window.window ? Window.window.activeFocusItem : null;
        if (item && (item instanceof TextInput || item instanceof TextEdit)) item.focus = false;
    }
    opacity: enabled ? 1 : 0.6
    contentItem: Text {
        text: control.text
        textFormat: Text.PlainText
        font: control.font
        color: control.theme.text
        horizontalAlignment: implicitWidth > width ? Text.AlignLeft : Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        clip: true
    }
    background: Rectangle {
        radius: control.metrics.frameRadius
        color: control.down || control.highlighted ? control.theme.buttonActive
              : control.hovered ? control.theme.buttonHover : control.theme.button
    }
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
    ToolTip.delay: 400
}
