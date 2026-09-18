pragma ComponentBehavior: Bound
import QtQuick

Text {
    required property UiMetrics metrics
    required property UiTheme theme
    property real lineHeight: metrics.mainFont
    color: theme.text
    font.pixelSize: lineHeight * metrics.fontEmScale
    textFormat: Text.PlainText
    height: wrapMode === Text.NoWrap ? lineHeight : implicitHeight
    verticalAlignment: Text.AlignVCenter
    clip: true
}
