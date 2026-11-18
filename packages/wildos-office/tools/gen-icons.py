#!/usr/bin/env python3
"""Builds the Wild OS icon themes for Write, Tables and Slides.

    tools/gen-icons.py OUTDIR      write images_wildos_svg.zip and
                                   images_wildos_dark_svg.zip into OUTDIR
    tools/gen-icons.py --coverage CONFIG_TREE STOCK_ZIP [UI ...]
                                   list the commands on our screens that
                                   still fall back to LibreOffice's icons,
                                   given its soffice.cfg tree and a stock
                                   theme such as images_colibre_svg.zip

The sources are line icons on a 24 pixel grid in lo/icons: svg/ holds the
ones drawn on the Wild OS design boards and our own, lucide/ the Lucide
icons we adapted (see lo/icons/CREDITS). lo/icons/icons.txt says which
icon each command or LibreOffice image uses, and in which tone.

Each command becomes three files, the 16 pixel cmd/sc_*.svg LibreOffice
asks for on toolbars and menus, the 24 pixel cmd/lc_*.svg for large
buttons and cmd/32/*.svg for large buttons on HiDPI screens. The same
source is written twice: light strokes for the dark theme and dark ones for
the light theme, because LibreOffice draws an SVG icon exactly as coloured.
"""

import os
import re
import sys
import zipfile
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ICONS = os.path.join(HERE, "..", "lo", "icons")
SVG_NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", SVG_NS)

STROKE_WIDTH = 1.8

# Colours from the WildOS and WildOSLight colour schemes. The boards draw
# action buttons (Paste, Picture, Find) in the accent colour, formatting in
# the text colour and the sidebar strip and status bar in a quieter tone.
TONES = {
    "dark": {
        "fg": ("#eef1f8", None),
        "accent": ("#8fb4ff", None),
        "muted": ("#eef1f8", "0.62"),
        "red": ("#ff8a8a", None),
        "yellow": ("#ffd27a", None),
        "green": ("#8fd6a8", None),
        # The presenter view is dark whatever the theme: its buttons' icons.
        "presenter": ("#ffffff", None),
        "presenter-end": ("#ff9a9a", None),
    },
    "light": {
        "fg": ("#1d1f23", None),
        "accent": ("#3563d8", None),
        "muted": ("#1d1f23", "0.66"),
        "red": ("#c5372c", None),
        "yellow": ("#9a6400", None),
        "green": ("#2f7a1f", None),
        # The presenter view is dark whatever the theme: its buttons' icons.
        "presenter": ("#ffffff", None),
        "presenter-end": ("#ff9a9a", None),
    },
}

THEMES = {
    "images_wildos_svg.zip": "light",
    "images_wildos_dark_svg.zip": "dark",
}

COMMAND_SIZES = (("cmd/sc_%s.svg", (16, 16)), ("cmd/lc_%s.svg", (24, 24)), ("cmd/32/%s.svg", (32, 32)))


def read_mapping():
    """Returns [(target, icon, tone)] from icons.txt.

    A target is a UNO command name, or a LibreOffice image path with the
    size it is drawn at, such as svx/res/doc_modified_no.png@16 or, for
    images that are not square, sw/res/twopages.png@25x16.
    """
    entries = []
    seen = set()
    with open(os.path.join(ICONS, "icons.txt"), encoding="utf-8") as f:
        for number, line in enumerate(f, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) not in (2, 3):
                sys.exit("icons.txt:%d: expected 'target icon [tone]'" % number)
            target, icon = parts[0], parts[1]
            tone = parts[2] if len(parts) == 3 else "fg"
            if tone not in TONES["dark"]:
                sys.exit("icons.txt:%d: unknown tone %s" % (number, tone))
            if target.lower() in seen:
                sys.exit("icons.txt:%d: %s is mapped twice" % (number, target))
            seen.add(target.lower())
            entries.append((target, icon, tone))
    return entries


def outputs(target):
    """Yields (path in the zip, (width, height)) for one mapping target."""
    if "/" not in target:
        name = target.lower()
        for pattern, size in COMMAND_SIZES:
            yield pattern % name, size
        return
    path, _, size = target.partition("@")
    if not re.fullmatch(r"\d+(x\d+)?", size):
        sys.exit("icons.txt: %s needs a size, as in %s@16" % (target, path))
    width, _, height = size.partition("x")
    yield os.path.splitext(path)[0] + ".svg", (int(width), int(height or width))


# Path data is rewritten with every number and arc flag spelled out, as
# Lucide's compact arcs ("a2 2 0 012 2") are valid SVG that not every
# renderer reads the same way.
PATH_TOKEN = re.compile(r"[MmLlHhVvCcSsQqTtAaZz]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")
ARGS = {"m": 2, "l": 2, "h": 1, "v": 1, "c": 6, "s": 4, "q": 4, "t": 2, "a": 7, "z": 0}


def normalise_path(d):
    out = []
    pos = 0
    command = None
    while True:
        while pos < len(d) and d[pos] in " ,\t\n\r":
            pos += 1
        if pos >= len(d):
            break
        if d[pos].isalpha():
            command = d[pos]
            out.append(command)
            pos += 1
            if command in "Zz":
                continue
        if command is None:
            raise ValueError("path data does not start with a command: %r" % d)
        count = ARGS[command.lower()]
        values = []
        for index in range(count):
            while pos < len(d) and d[pos] in " ,\t\n\r":
                pos += 1
            if command in "Aa" and index in (3, 4):
                if d[pos] not in "01":
                    raise ValueError("bad arc flag in %r" % d)
                values.append(d[pos])
                pos += 1
                continue
            match = PATH_TOKEN.match(d, pos)
            if not match or match.group(0).isalpha():
                raise ValueError("expected a number at %d in %r" % (pos, d))
            values.append(match.group(0))
            pos = match.end()
        out.append(" ".join(values))
        # Further coordinate pairs after a move are line segments.
        if command == "M":
            command = "L"
        elif command == "m":
            command = "l"
    return " ".join(out)


def load_source(icon):
    path = os.path.join(ICONS, icon + ".svg") if "/" in icon else os.path.join(ICONS, "svg", icon + ".svg")
    if not os.path.exists(path):
        sys.exit("icons.txt: no source for icon %s (%s)" % (icon, path))
    root = ET.parse(path).getroot()
    if root.get("viewBox") != "0 0 24 24":
        sys.exit("%s: sources are drawn on a 0 0 24 24 viewBox" % path)
    return root


def strip_ns(tag):
    return tag.split("}", 1)[-1]


def render(root, size, tone, palette):
    """Returns one source as SVG text at one size in one theme's colours."""

    def colour(element, inherited):
        own = element.get("data-tone", inherited)
        return own, palette[own]

    def convert(element, inherited):
        tone_here, (value, opacity) = colour(element, inherited)
        attrs = {}
        for key, val in element.attrib.items():
            key = strip_ns(key)
            if key in ("class", "data-tone"):
                continue
            if key == "d":
                val = normalise_path(val)
            if val == "currentColor":
                val = value
                if opacity:
                    attrs[key + "-opacity"] = opacity
            attrs[key] = val
        node = ET.Element(strip_ns(element.tag), attrs)
        for child in element:
            node.append(convert(child, tone_here))
        return node

    value, opacity = palette[tone]
    width, height = size
    # A source fills the shorter side of an image that is not square and
    # sits centred along the longer one.
    unit = 24 / min(width, height)
    box_w, box_h = width * unit, height * unit
    view_box = "%g %g %g %g" % ((24 - box_w) / 2, (24 - box_h) / 2, box_w, box_h)
    # Below 16 pixels the board's stroke would drop under a pixel and blur
    # away, so the smallest images keep a stroke of about one pixel.
    stroke = max(STROKE_WIDTH, round(1.1 * unit, 2))
    attrs = {
        "xmlns": SVG_NS,
        "width": str(width),
        "height": str(height),
        "viewBox": view_box,
        "fill": "none",
        "stroke": value,
        "stroke-width": "%g" % stroke,
        "stroke-linecap": "round",
        "stroke-linejoin": "round",
    }
    if opacity:
        attrs["stroke-opacity"] = opacity
    parts = []
    for child in root:
        if strip_ns(child.tag) in ("title", "desc", "metadata"):
            continue
        parts.append(ET.tostring(convert(child, tone), encoding="unicode"))
    head = " ".join('%s="%s"' % (k, v) for k, v in attrs.items())
    body = "".join(parts).replace(' xmlns="%s"' % SVG_NS, "")
    return "<svg %s>%s</svg>\n" % (head, body)


def app_icon(app, size):
    """An app's tile icon, as drawn, at one size."""
    path = os.path.join(HERE, "..", "data", "org.wildos.%s.svg" % app)
    if not os.path.exists(path):
        sys.exit("icons.txt: no app icon %s (%s)" % (app, path))
    with open(path, encoding="utf-8") as f:
        text = f.read()
    width, height = size
    head = re.sub(r'\s(width|height)="[^"]*"', "", text[:text.index(">") + 1])
    head = head.replace("<svg ", '<svg width="%d" height="%d" ' % (width, height), 1)
    return head + text[text.index(">") + 1:]


def build(outdir):
    entries = read_mapping()
    sources = {}
    os.makedirs(outdir, exist_ok=True)
    for zipname, theme in THEMES.items():
        palette = TONES[theme]
        files = {}
        for target, icon, tone in entries:
            if icon.startswith("app:"):
                # A document's own app icon, in its colours (the Recent
                # list and file type images): data/org.wildos.<app>.svg.
                for path, size in outputs(target):
                    files[path] = app_icon(icon[4:], size)
                continue
            if icon not in sources:
                sources[icon] = load_source(icon)
            for path, size in outputs(target):
                files[path] = render(sources[icon], size, tone, palette)
        tmp = os.path.join(outdir, zipname + ".tmp")
        with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as z:
            for path in sorted(files):
                # Fixed dates keep the zips byte for byte reproducible.
                info = zipfile.ZipInfo(path, date_time=(2026, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                z.writestr(info, files[path])
        os.replace(tmp, os.path.join(outdir, zipname))
        print("%s: %d icons" % (zipname, len(files)))


def coverage(config_tree, stock_zip, ui_files):
    """Lists the commands on our screens that LibreOffice would still draw
    from its own theme: those the stock theme has an icon for and we have
    not mapped. Commands without a stock icon show no icon either way."""
    import glob
    mapped = {t.lower() for t, _, _ in read_mapping() if "/" not in t}
    with zipfile.ZipFile(stock_zip) as z:
        stock = set(z.namelist())
        if "links.txt" in stock:
            stock |= {line.split()[0] for line in z.read("links.txt").decode().splitlines() if line.strip()}
    stock = {os.path.splitext(n)[0] for n in stock}
    places = {}

    def scan(label, path):
        with open(path, encoding="utf-8") as f:
            for command in re.findall(r"\.uno:([A-Za-z0-9_]+)", f.read()):
                places.setdefault(command, set()).add(label)

    for path in ui_files:
        scan("ribbon", path)
    for module in ("swriter", "scalc", "simpress"):
        for path in glob.glob(os.path.join(config_tree, "modules", module, "popupmenu", "*.xml")):
            scan("menus", path)
        for bar in ("tableobjectbar", "graphicobjectbar", "drawingobjectbar", "drawobjectbar",
                    "frameobjectbar", "drawtextobjectbar", "textobjectbar", "formatobjectbar",
                    "standardbar", "findbar"):
            path = os.path.join(config_tree, "modules", module, "toolbar", bar + ".xml")
            if os.path.exists(path):
                scan("toolbars", path)
        scan("status bar", os.path.join(config_tree, "modules", module, "statusbar", "statusbar.xml"))
    for pattern in ("svx/ui/sidebar*.ui", "modules/swriter/ui/sidebar*.ui", "modules/swriter/ui/page*panel.ui",
                    "modules/swriter/ui/navigatorpanel.ui", "modules/scalc/ui/sidebar*.ui",
                    "modules/scalc/ui/navigatorpanel.ui", "sfx/ui/templatepanel.ui"):
        for path in glob.glob(os.path.join(config_tree, pattern)):
            scan("sidebar", path)
    with_icon = {c for c in places if "cmd/sc_" + c.lower() in stock or "cmd/lc_" + c.lower() in stock}
    missing = sorted(c for c in with_icon if c.lower() not in mapped)
    print("%d commands with a stock icon, %d drawn for Wild OS, %d left to LibreOffice"
          % (len(with_icon), len(with_icon) - len(missing), len(missing)))
    for command in missing:
        print("  %s (%s)" % (command, ", ".join(sorted(places[command]))))


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == "--coverage":
        coverage(sys.argv[2], sys.argv[3], sys.argv[4:])
    elif len(sys.argv) == 2 and not sys.argv[1].startswith("-"):
        build(sys.argv[1])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
