import QtQuick

// The skeleton over the camera picture: Coach.keypoints are in the picture's pixels, `area` is
// where the picture was drawn.
Canvas {
    id: canvas
    property rect area
    property size source: Coach.frameSize
    // COCO bones: face, arms, torso, legs.
    readonly property var bones: [[0, 1], [0, 2], [1, 3], [2, 4], [5, 7], [7, 9], [6, 8], [8, 10], [5, 6],
                                  [5, 11], [6, 12], [11, 12], [11, 13], [13, 15], [12, 14], [14, 16]]

    Connections {
        target: Coach
        function onStateChanged() { canvas.requestPaint() }
    }

    onPaint: {
        const ctx = getContext("2d")
        ctx.clearRect(0, 0, width, height)
        const k = Coach.keypoints
        if (k.length !== 17 || source.width <= 0) return
        const sx = area.width / source.width, sy = area.height / source.height
        const p = i => [area.x + k[i][0] * sx, area.y + k[i][1] * sy]
        ctx.lineWidth = 4
        ctx.lineCap = "round"
        ctx.strokeStyle = Theme.bone
        for (const [a, b] of bones) {
            if (k[a][2] < 0.3 || k[b][2] < 0.3) continue
            const [x1, y1] = p(a), [x2, y2] = p(b)
            ctx.beginPath()
            ctx.moveTo(x1, y1)
            ctx.lineTo(x2, y2)
            ctx.stroke()
        }
        ctx.fillStyle = Theme.joint
        for (let i = 0; i < 17; ++i) {
            if (k[i][2] < 0.3) continue
            const [x, y] = p(i)
            ctx.beginPath()
            ctx.arc(x, y, i < 5 ? 3 : 5, 0, 2 * Math.PI)
            ctx.fill()
        }
    }
}
