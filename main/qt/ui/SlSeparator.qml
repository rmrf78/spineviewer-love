pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    required property UiMetrics metrics
    required property UiTheme theme
    height: metrics.pixel
    color: theme.separator
}
