pragma ComponentBehavior: Bound
import QtQuick

QtObject {
    id: slide
    property real startValue: 0
    property real targetValue: 0
    property real value: 0
    property real rate: 8
    property bool active: true
    Component.onCompleted: value = startValue
    onActiveChanged: if (active) value = startValue
    function advance(deltaSeconds) {
        value += (targetValue - value) * Math.min(1, rate * deltaSeconds);
    }
    property FrameAnimation ticker: FrameAnimation {
        running: slide.active && Math.abs(slide.targetValue - slide.value) > .001
        onTriggered: slide.advance(frameTime)
    }
}
