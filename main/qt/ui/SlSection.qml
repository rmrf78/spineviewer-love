pragma ComponentBehavior: Bound
import QtQuick

Column {
    id: section
    required property UiMetrics metrics
    required property UiTheme theme
    property string title: ""
    property bool expanded: false
    property bool framed: true
    default property alias content: body.data
    spacing: metrics.spacing
    SlButton {
        id: header
        width: parent.width
        height: section.metrics.mainFont + (section.framed ? section.metrics.framePaddingY * 2 : 0)
        metrics: section.metrics
        theme: section.theme
        text: section.title
        contentItem: Item {
            Canvas {
                id: arrow
                width: section.metrics.mainFont
                height: width
                y: section.framed ? section.metrics.framePaddingY : section.metrics.mainFont*.15
                onWidthChanged: requestPaint()
                onPaint: {
                    const ctx=getContext("2d"),h=width,scale=section.framed?1:.7,r=h*.4*scale,cx=h*.5,cy=h*.5*scale;
                    ctx.clearRect(0,0,width,height);ctx.fillStyle=section.theme.text;ctx.beginPath();
                    if(section.expanded){ctx.moveTo(cx,cy+.75*r);ctx.lineTo(cx-.866*r,cy-.75*r);ctx.lineTo(cx+.866*r,cy-.75*r);}
                    else{ctx.moveTo(cx+.75*r,cy);ctx.lineTo(cx-.75*r,cy+.866*r);ctx.lineTo(cx-.75*r,cy-.866*r);}
                    ctx.closePath();ctx.fill();
                }
                Connections { target: section; function onExpandedChanged(){arrow.requestPaint();} }
                Connections { target: section.theme; function onTextChanged(){arrow.requestPaint();} }
            }
            Text {
                x: section.metrics.mainFont + section.metrics.framePaddingX * (section.framed ? 2 : 1)
                width: Math.max(0,parent.width-x)
                height: section.metrics.mainFont
                y: section.framed ? section.metrics.framePaddingY : 0
                text: header.text
                textFormat: Text.PlainText
                color: section.theme.text
                font.pixelSize: section.metrics.mainFont * section.metrics.fontEmScale
                verticalAlignment: Text.AlignVCenter
                clip: true
            }
        }
        background: Rectangle { color: header.down ? section.theme.headerActive : header.hovered ? section.theme.headerHover : section.framed ? section.theme.header : "transparent"; radius: section.framed ? section.metrics.frameRadius : 0 }
        onClicked: section.expanded = !section.expanded
    }
    Column {
        id: body
        x: section.framed ? 0 : 21 * section.metrics.pixel
        width: parent.width-x
        visible: section.expanded
        spacing: section.metrics.spacing
    }
}
