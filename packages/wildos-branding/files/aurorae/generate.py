#!/usr/bin/env python3
"""Builds the WildOS Aurorae window decoration from the design tokens.

    python3 generate.py

Writes decoration.svg, the button SVGs, WildOSrc and metadata.desktop
for the dark theme into WildOS/ and the light one into WildOSLight/, each in
its colour scheme's window colour so the title bar and the app read as one
sheet. wildos-auto-theme puts on the one that matches the colour scheme. The
drop shadow is shadow-window (0 30px 80px rgba(10,12,16,0.45)); Aurorae turns
everything drawn in the padding into the KWin window shadow. It is computed
here as a Gaussian and embedded as PNG, because the Qt SVG renderer that KWin
uses has no reliable blur filter. The title bar and edges stay vector so they
remain crisp at any scale. Standard library only.
"""

import base64
import math
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))

# Each variant's window colour and text colours come from its colour scheme
# (WildOS.colors, WildOSLight.colors, [Colors:Window]). The edge is Windows
# 11's 1px border, Style.windowEdge in Wild OS apps: faint white on dark, a
# grey hairline on light that still shows against a white window behind.
VARIANTS = {
    "WildOS": {
        "name": "Wild OS",
        "title": "#181a22",
        "ink": "#ffffff",
        "text": "255,255,255",
        "text_inactive": "205,206,208",
        "edge": "#ffffff",
        "edge_opacity": 0.093,
        "hover": "#ffffff",
        "shadow_alpha": 0.45,
    },
    "WildOSLight": {
        "name": "Wild OS Light",
        "title": "#f3f3f3",
        "ink": "#1a1a1a",
        "text": "26,26,26",
        "text_inactive": "96,96,96",
        "edge": "#000000",
        "edge_opacity": 0.18,
        "hover": "#000000",
        "shadow_alpha": 0.22,
    },
}

CLOSE_HOVER = "#c4463f"
CLOSE_PRESSED = "#a63a34"
SHADOW_RGB = (10, 12, 16)
SHADOW_Y = 30
SIGMA = 40.0  # CSS blur radius 80 is a Gaussian with sigma 40

TITLE_H = 42
# The edge is 1px, but a larger border size picked in System Settings widens
# the decoration; this much window colour inside the edge keeps it seamless.
BAND = 8
RADIUS = 14
BUTTON_W = 46

# Padding holds the shadow; two sigma either side is where it fades to nothing.
PAD_L = PAD_R = 72
PAD_T = 44
PAD_B = 100
REACH = 80
REF = 400  # reference window used to cut the nine pieces

TW = PAD_L + REF + PAD_R
TH = PAD_T + REF + PAD_B
CW = PAD_L + REACH
CTH = PAD_T + SHADOW_Y + REACH
CBH = PAD_B + REACH - SHADOW_Y


def n(v):
    s = ("%.3f" % v).rstrip("0").rstrip(".")
    return s if s not in ("", "-0") else "0"


def profile(t, a, b):
    k = SIGMA * math.sqrt(2)
    return 0.5 * (math.erf((t - a) / k) - math.erf((t - b) / k))


def png(w, h, pixels):
    raw = bytearray()
    for row in pixels:
        raw.append(0)
        raw.extend(row)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )


def shadow_image(x0, y0, w, h):
    """Shadow pixels for a piece.

    The shadow continues under the window: KWin leaves the window rectangle
    out of the shadow, so without it the wallpaper would show undarkened
    outside the rounded top corners.
    """
    wx0, wx1 = PAD_L, PAD_L + REF
    wy0, wy1 = PAD_T, PAD_T + REF
    rows = []
    for j in range(h):
        y = y0 + j + 0.5
        gy = profile(y, wy0 + SHADOW_Y, wy1 + SHADOW_Y)
        row = bytearray()
        for i in range(w):
            x = x0 + i + 0.5
            a = int(round(255 * V["shadow_alpha"] * profile(x, wx0, wx1) * gy))
            row.extend((SHADOW_RGB[0], SHADOW_RGB[1], SHADOW_RGB[2], a))
        rows.append(row)
    data = base64.b64encode(png(w, h, rows)).decode("ascii")
    return '<image x="%s" y="%s" width="%s" height="%s" xlink:href="data:image/png;base64,%s"/>' % (
        n(x0), n(y0), n(w), n(h), data)


def mirror_x(d):
    """Mirror an absolute path horizontally about the sheet centre."""
    out = []
    toks = d.split(" ")
    i = 0
    while i < len(toks):
        t = toks[i]
        cmd = t[0]
        if cmd in "ML":
            x, y = t[1:].split(",")
            out.append("%s%s,%s" % (cmd, n(TW - float(x)), y))
        elif cmd == "H":
            out.append("H%s" % n(TW - float(t[1:])))
        elif cmd == "V" or cmd == "Z":
            out.append(t)
        elif cmd == "A":
            # A<r>,<r> 0 0 <sweep> <x>,<y>
            sweep = toks[i + 3]
            x, y = toks[i + 4].split(",")
            out.extend([t, toks[i + 1], toks[i + 2], str(1 - int(sweep)), "%s,%s" % (n(TW - float(x)), y)])
            i += 4
        i += 1
    return " ".join(out)


def path(d, fill, opacity=1.0):
    op = "" if opacity >= 1 else ' fill-opacity="%s"' % n(opacity)
    return '<path d="%s" fill="%s"%s/>' % (d, fill, op)


def rect(x, y, w, h, fill, opacity=1.0):
    op = "" if opacity >= 1 else ' fill-opacity="%s"' % n(opacity)
    return '<rect x="%s" y="%s" width="%s" height="%s" fill="%s"%s/>' % (n(x), n(y), n(w), n(h), fill, op)


def decoration_svg():
    L, T, R = PAD_L, PAD_T, PAD_L + REF
    bottom = PAD_T + REF
    r = RADIUS
    # Title bar plus the 1px side column, rounded at the top only: the client
    # covers the lower corners, so they stay square.
    tl_fill = "M%s,%s V%s A%s,%s 0 0 1 %s,%s H%s V%s H%s V%s Z" % (
        n(L), n(CTH), n(T + r), n(r), n(r), n(L + r), n(T), n(CW), n(T + TITLE_H), n(L + BAND), n(CTH))
    tl_edge = "M%s,%s V%s A%s,%s 0 0 1 %s,%s H%s V%s H%s A%s,%s 0 0 0 %s,%s V%s Z" % (
        n(L), n(CTH), n(T + r), n(r), n(r), n(L + r), n(T), n(CW), n(T + 1),
        n(L + r), n(r - 1), n(r - 1), n(L + 1), n(T + r), n(CTH))

    pieces = {}
    pieces["topleft"] = (0, 0, CW, CTH, path(tl_fill, V["title"]) + path(tl_edge, V["edge"], V["edge_opacity"]))
    pieces["topright"] = (TW - CW, 0, CW, CTH, path(mirror_x(tl_fill), V["title"]) + path(mirror_x(tl_edge), V["edge"], V["edge_opacity"]))
    mid_w = TW - 2 * CW
    pieces["top"] = (CW, 0, mid_w, CTH, rect(CW, T, mid_w, TITLE_H, V["title"]) + rect(CW, T, mid_w, 1, V["edge"], V["edge_opacity"]))
    mid_h = TH - CTH - CBH

    def column(edge_x, band_x, y, h):
        return rect(band_x, y, BAND, h, V["title"]) + rect(edge_x, y, 1, h, V["edge"], V["edge_opacity"])

    pieces["left"] = (0, CTH, CW, mid_h, column(L, L, CTH, mid_h))
    pieces["right"] = (TW - CW, CTH, CW, mid_h, column(R - 1, R - BAND, CTH, mid_h))
    by = TH - CBH

    def foot(edge_x, band_x, x_row, w_row):
        h = bottom - by
        return (rect(band_x, by, BAND, h, V["title"]) + rect(x_row, bottom - BAND, w_row, BAND, V["title"])
                + rect(edge_x, by, 1, h - 1, V["edge"], V["edge_opacity"])
                + rect(x_row, bottom - 1, w_row, 1, V["edge"], V["edge_opacity"]))

    pieces["bottomleft"] = (0, by, CW, CBH, foot(L, L, L, CW - L))
    pieces["bottomright"] = (TW - CW, by, CW, CBH, foot(R - 1, R - BAND, TW - CW, R - (TW - CW)))
    pieces["bottom"] = (CW, by, mid_w, CBH, rect(CW, bottom - BAND, mid_w, BAND, V["title"]) + rect(CW, bottom - 1, mid_w, 1, V["edge"], V["edge_opacity"]))

    groups = []
    for name, (x, y, w, h, vec) in pieces.items():
        groups.append('<g id="decoration-%s">%s%s</g>' % (name, shadow_image(x, y, w, h), vec))
    groups.append('<g id="decoration-center">%s</g>' % rect(CW, CTH, mid_w, mid_h, "#000000", 0))

    # Maximised windows: the bare title bar, no edge, corners or shadow.
    my = TH + 20
    for name, (x, y, w, h) in {
        "topleft": (0, my, 1, 1), "top": (1, my, 40, 1), "topright": (41, my, 1, 1),
        "left": (0, my + 1, 1, 40), "right": (41, my + 1, 1, 40),
        "bottomleft": (0, my + 41, 1, 1), "bottom": (1, my + 41, 40, 1), "bottomright": (41, my + 41, 1, 1),
    }.items():
        groups.append('<g id="decoration-maximized-%s">%s</g>' % (name, rect(x, y, w, h, V["title"])))
    groups.append('<g id="decoration-maximized-center">%s</g>' % rect(1, my + 1, 40, TITLE_H, V["title"]))

    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
        'width="%d" height="%d" viewBox="0 0 %d %d">\n%s\n</svg>\n'
        % (TW, TH + 20 + TITLE_H + 2, TW, TH + 20 + TITLE_H + 2, "\n".join(groups))
    )


# Glyphs from the design, drawn in a 12 unit box shown at 11px.
GLYPHS = {
    "minimize": '<path d="M1.5,6 H10.5" fill="none" stroke="{c}" stroke-opacity="{a}" stroke-width="1.3"/>',
    "maximize": '<rect x="1.5" y="1.5" width="9" height="9" rx="1.8" fill="none" stroke="{c}" stroke-opacity="{a}" stroke-width="1.2"/>',
    "restore": (
        '<rect x="1.5" y="3.5" width="7" height="7" rx="1.5" fill="none" stroke="{c}" stroke-opacity="{a}" stroke-width="1.2"/>'
        '<path d="M3.5,3.5 V3 A1.5,1.5 0 0 1 5,1.5 H9 A1.5,1.5 0 0 1 10.5,3 V7 A1.5,1.5 0 0 1 9,8.5 H8.5" '
        'fill="none" stroke="{c}" stroke-opacity="{a}" stroke-width="1.2" stroke-linecap="round"/>'
    ),
    "close": '<path d="M2,2 L10,10 M10,2 L2,10" fill="none" stroke="{c}" stroke-opacity="{a}" stroke-width="1.3" stroke-linecap="round"/>',
}


def button_svg(kind):
    w, h = BUTTON_W, TITLE_H
    scale = 11 / 12.0
    states = {
        "active": (None, V["ink"], 1.0),
        "inactive": (None, V["ink"], 0.55),
        "hover": ((V["hover"], 0.09), V["ink"], 1.0),
        "pressed": ((V["hover"], 0.05), V["ink"], 0.8),
        "deactivated": (None, V["ink"], 0.3),
    }
    if kind == "close":
        states["hover"] = ((CLOSE_HOVER, 1.0), "#ffffff", 1.0)
        states["pressed"] = ((CLOSE_PRESSED, 1.0), "#ffffff", 0.9)
    groups = []
    for i, (state, (bg, c, a)) in enumerate(states.items()):
        y = i * (h + 4)
        body = rect(0, y, w, h, "#000000", 0)
        if bg:
            if kind == "close":
                # Close sits in the window's top right corner, so its hover
                # follows the 14px window radius.
                d = "M0,%s H%s A%s,%s 0 0 1 %s,%s V%s H0 Z" % (
                    n(y), n(w - RADIUS), n(RADIUS), n(RADIUS), n(w), n(y + RADIUS), n(y + h))
                body += path(d, bg[0], bg[1])
            else:
                body += rect(0, y, w, h, bg[0], bg[1])
        glyph = GLYPHS[kind].format(c=c, a=n(a))
        body += '<g transform="translate(%s,%s) scale(%s)">%s</g>' % (
            n((w - 11) / 2.0), n(y + (h - 11) / 2.0), n(scale), glyph)
        groups.append('<g id="%s-center">%s</g>' % (state, body))
    return sheet(w, h, groups)


def close_maximized_svg():
    """Square close fills for a maximised window.

    A maximised window has square corners, so as on Windows 11 its close
    hover fills the corner. Aurorae buttons have no maximised state, so Wild
    OS's aurorae.qml (wildos-shell) puts these under the close button of a
    maximised window, where they show through outside the rounded hover.
    They live in a file of their own because KSvg caches which elements a
    file has, and a cache made from an older close.svg would hide new ones.
    """
    groups = []
    for state, colour in (("hover", CLOSE_HOVER), ("pressed", CLOSE_PRESSED)):
        y = len(groups) * (TITLE_H + 4)
        groups.append('<g id="%s-center">%s</g>' % (state, rect(0, y, BUTTON_W, TITLE_H, colour)))
    return sheet(BUTTON_W, TITLE_H, groups)


def sheet(w, h, groups):
    total = len(groups) * (h + 4)
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n%s\n</svg>\n'
        % (w, total, w, total, "\n".join(groups))
    )


METADATA = """[Desktop Entry]
Name={name}
Comment=42px glass title bar with Windows-order controls
X-KDE-PluginInfo-Author=Wild OS
X-KDE-PluginInfo-Name={id}
X-KDE-PluginInfo-Version=1.0
X-KDE-PluginInfo-License=GPL-2.0-or-later
X-KDE-PluginInfo-Category=
X-KDE-PluginInfo-Depends=
X-KDE-PluginInfo-EnabledByDefault=true
"""

RC = """[General]
ActiveTextColor={text}
InactiveTextColor={text_inactive}
TitleAlignment=Left
TitleVerticalAlignment=Center
Animation=150

[Layout]
BorderLeft=1
BorderRight=1
BorderBottom=1
TitleEdgeTop=0
TitleEdgeBottom=0
TitleEdgeLeft=0
TitleEdgeRight=0
TitleEdgeTopMaximized=0
TitleEdgeBottomMaximized=0
TitleEdgeLeftMaximized=0
TitleEdgeRightMaximized=0
TitleBorderLeft=16
TitleBorderRight=12
TitleHeight={th}
ButtonWidth={bw}
ButtonWidthMinimize={bw}
ButtonWidthMaximizeRestore={bw}
ButtonWidthClose={bw}
ButtonWidthAlldesktops={bw}
ButtonWidthKeepabove={bw}
ButtonWidthKeepbelow={bw}
ButtonWidthShade={bw}
ButtonWidthHelp={bw}
ButtonWidthMenu=36
ButtonHeight={th}
ButtonSpacing=0
ButtonMarginTop=0
ButtonMarginTopMaximized=0
ExplicitButtonSpacer=10
PaddingTop={pt}
PaddingBottom={pb}
PaddingLeft={pl}
PaddingRight={pr}
"""


def main():
    global V
    for theme, V in VARIANTS.items():
        out = os.path.join(HERE, theme)
        os.makedirs(out, exist_ok=True)

        def write(name, text):
            with open(os.path.join(out, name), "w", encoding="utf-8", newline="\n") as fh:
                fh.write(text)

        write("decoration.svg", decoration_svg())
        for kind in GLYPHS:
            write(kind + ".svg", button_svg(kind))
        write("close-maximized.svg", close_maximized_svg())
        write(theme + "rc", RC.format(th=TITLE_H, bw=BUTTON_W, pt=PAD_T, pb=PAD_B, pl=PAD_L, pr=PAD_R,
                                      text=V["text"], text_inactive=V["text_inactive"]))
        write("metadata.desktop", METADATA.format(name=V["name"], id=theme))
        print("wrote", out)


main()
