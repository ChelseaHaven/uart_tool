import QtQuick

// Decorative thumbnail only; independent of receive data and chart state.
Rectangle {
    id: preview
    property bool trajectory: false
    color: Theme.input; radius: 6; border.color: Theme.border
    implicitWidth: 180; implicitHeight: 92
    Canvas {
        id: drawing
        anchors.fill: parent; anchors.margins: 8
        property color gridColor: Theme.divider
        property color firstColor: Theme.accentText
        property color secondColor: Theme.warning
        Connections {
            target: preview
            function onTrajectoryChanged() { drawing.requestPaint() }
        }
        onGridColorChanged: requestPaint()
        onFirstColorChanged: requestPaint()
        onSecondColorChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx=getContext("2d")
            ctx.clearRect(0,0,width,height)
            const left=5, top=5, w=Math.max(1,width-10), h=Math.max(1,height-10)
            ctx.strokeStyle=gridColor; ctx.lineWidth=1
            ctx.beginPath()
            for(let i=0;i<=4;++i) {
                const x=left+w*i/4
                ctx.moveTo(x,top); ctx.lineTo(x,top+h)
            }
            for(let i=0;i<=3;++i) {
                const y=top+h*i/3
                ctx.moveTo(left,y); ctx.lineTo(left+w,y)
            }
            ctx.stroke()
            ctx.save(); ctx.beginPath(); ctx.rect(left,top,w,h); ctx.clip()
            ctx.lineWidth=2; ctx.lineJoin="round"; ctx.lineCap="round"
            for(let curve=0;curve<(preview.trajectory?1:2);++curve) {
                ctx.strokeStyle=curve===0?firstColor:secondColor
                ctx.beginPath()
                for(let i=0;i<=120;++i) {
                    const t=i/120
                    const value=curve===0?Math.sin(t*Math.PI*4.4)*0.29:Math.cos(t*Math.PI*3.2+0.5)*0.18
                    const angle=t*Math.PI*1.8
                    const x=preview.trajectory?left+w*(0.5+0.39*Math.sin(angle)):left+t*w
                    const y=preview.trajectory?top+h*(0.5+0.36*Math.sin(2*angle)):top+h*(0.5-value)
                    if(i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y)
                }
                ctx.stroke()
            }
            ctx.restore()
        }
    }
}
