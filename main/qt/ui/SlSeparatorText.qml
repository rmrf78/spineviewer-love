pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: separator
    required property UiMetrics metrics
    required property UiTheme theme
    property alias text: label.text
    property real textSize: metrics.mainFont
    height: textSize + 6 * metrics.pixel
    Rectangle { x: 0; width: 12 * separator.metrics.pixel; height: 3 * separator.metrics.pixel; anchors.verticalCenter: parent.verticalCenter; color: separator.theme.separator }
    Text {
        id: label
        x: 20 * separator.metrics.pixel
        anchors.verticalCenter: parent.verticalCenter
        color: separator.theme.text
        textFormat: Text.PlainText
        font.pixelSize: separator.textSize * separator.metrics.fontEmScale
        width: Math.min(implicitWidth, Math.max(0, separator.width - x))
        elide: Text.ElideRight
    }
    Rectangle { x: label.x + label.width + 8 * separator.metrics.pixel; width: Math.max(0, separator.width - x); height: 3 * separator.metrics.pixel; anchors.verticalCenter: parent.verticalCenter; color: separator.theme.separator }
}
