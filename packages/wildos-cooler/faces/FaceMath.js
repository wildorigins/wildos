.pragma library

function wrap(value, period) {
    const r = value % period
    return r < 0 ? r + period : r
}

// Where a CSS animation of this duration and delay is at a given time, 0 to 1.
// A delay still to come counts as a cycle already running, so a face looks
// the same whenever it is started.
function phase(time, duration, delay) {
    return wrap(time - delay, duration) / duration
}

function bezier(x1, y1, x2, y2, p) {
    if (p <= 0)
        return 0
    if (p >= 1)
        return 1
    let s = p
    for (let i = 0; i < 6; ++i) {
        const u = 1 - s
        const x = 3 * u * u * s * x1 + 3 * u * s * s * x2 + s * s * s - p
        const dx = 3 * u * u * x1 + 6 * u * s * (x2 - x1) + 3 * s * s * (1 - x2)
        if (Math.abs(dx) < 1e-6)
            break
        s = Math.min(1, Math.max(0, s - x / dx))
    }
    const u = 1 - s
    return 3 * u * u * s * y1 + 3 * u * s * s * y2 + s * s * s
}

function linear(p) { return p }
function easeIn(p) { return bezier(0.42, 0, 1, 1, p) }
function easeOut(p) { return bezier(0, 0, 0.58, 1, p) }
function easeInOut(p) { return bezier(0.42, 0, 0.58, 1, p) }

// CSS keyframes: stops are [position, value] pairs in rising order, and the
// timing function runs afresh between each pair, as CSS does.
function keyframes(p, stops, ease) {
    for (let i = 1; i < stops.length; ++i) {
        if (p <= stops[i][0]) {
            const a = stops[i - 1]
            const b = stops[i]
            const local = b[0] > a[0] ? (p - a[0]) / (b[0] - a[0]) : 1
            return a[1] + (b[1] - a[1]) * ease(local)
        }
    }
    return stops[stops.length - 1][1]
}

function clamp(v, lo, hi) {
    return Math.min(hi, Math.max(lo, v))
}

// How far a temperature is along the 30 to 100 degree scale the board's arcs
// and tick rings are drawn on (54 degrees is 0.343 of the way).
function heat(celsius) {
    if (isNaN(celsius))
        return 0
    return clamp((celsius - 30) / 70, 0, 1)
}

function mix(a, b, f) {
    return Qt.rgba(a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, a.a + (b.a - a.a) * f)
}

function alpha(c, a) {
    return Qt.rgba(c.r, c.g, c.b, a)
}

// A three stop gradient read at any point, for bars and segments that each
// show their own slice of one shared gradient.
function gradientAt(c0, c1, c2, middle, f) {
    if (f <= middle)
        return mix(c0, c1, middle > 0 ? clamp(f / middle, 0, 1) : 1)
    return mix(c1, c2, clamp((f - middle) / (1 - middle), 0, 1))
}

// The three grid lines of a history graph for temperatures between lowest
// and highest: the value of the bottom line and the degrees between lines.
// The board's example (49 to 63) gives lines at 50, 60 and 70; a wider swing
// widens the step rather than letting the line run off the graph.
function gridFor(lowest, highest) {
    let step = 10
    let base = 50
    for (; step <= 80; step *= 2) {
        base = step * Math.round(lowest / step)
        if (highest - base <= 2.7 * step)
            break
    }
    return { base: base, step: step }
}

function lowest(values, fallback) {
    return values && values.length ? values.reduce((a, b) => Math.min(a, b)) : fallback
}

function highest(values, fallback) {
    return values && values.length ? values.reduce((a, b) => Math.max(a, b)) : fallback
}

// A reading that is not known (NaN) is written as nothing.
function digits(value) {
    return isNaN(value) ? "" : Math.round(value).toString()
}
