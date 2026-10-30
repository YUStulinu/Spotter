import QtQuick

// A small line chart: points [{t (ms), v}], dates along the bottom.
Canvas {
    id: chart
    property var points: []
    property string unit: ""
    onPointsChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.clearRect(0, 0, width, height)
        if (!points || points.length === 0) return
        const left = 48, bottom = 28, top = 24, right = 12
        const w = width - left - right, h = height - top - bottom
        let t0 = points[0].t, t1 = points[points.length - 1].t
        if (t1 === t0) { t0 -= 86400000; t1 += 86400000 }
        let lo = Math.min(...points.map(p => p.v)), hi = Math.max(...points.map(p => p.v))
        const pad = Math.max(1, (hi - lo) * 0.15)
        lo -= pad; hi += pad
        const x = t => left + (t - t0) / (t1 - t0) * w
        const y = v => top + (1 - (v - lo) / (hi - lo)) * h
        ctx.font = "12px sans-serif"
        ctx.fillStyle = Theme.muted
        ctx.strokeStyle = Theme.line
        ctx.lineWidth = 1
        for (let i = 0; i <= 4; ++i) {
            const v = lo + (hi - lo) * i / 4
            ctx.beginPath(); ctx.moveTo(left, y(v)); ctx.lineTo(left + w, y(v)); ctx.stroke()
            ctx.fillText(v.toFixed(0), 4, y(v) + 4)
        }
        ctx.fillText(unit, left, 14)
        const fmt = t => { const d = new Date(t); return d.getDate() + "/" + (d.getMonth() + 1) }
        ctx.fillText(fmt(points[0].t), left, height - 8)
        if (points.length > 1) ctx.fillText(fmt(points[points.length - 1].t), left + w - 30, height - 8)
        ctx.strokeStyle = Theme.accent
        ctx.lineWidth = 3
        ctx.beginPath()
        points.forEach((p, i) => i ? ctx.lineTo(x(p.t), y(p.v)) : ctx.moveTo(x(p.t), y(p.v)))
        ctx.stroke()
        ctx.fillStyle = Theme.accent
        for (const p of points) { ctx.beginPath(); ctx.arc(x(p.t), y(p.v), 4, 0, 2 * Math.PI); ctx.fill() }
    }
}
