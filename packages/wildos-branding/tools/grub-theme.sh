#!/bin/bash
# Renders the Wild OS GRUB themes: the start menu of the USB stick and the
# start-up menu of an installed system, which share one look.
#
#   grub-theme.sh usb <theme dir> <logo svg> <version>
#   grub-theme.sh installed <theme dir> <logo svg>
#
# The ISO build runs the first, the wildos-branding package build the second.
# The sources are the repo's logo and Albert Sans (files/fonts).
#
# Every word on the menu is a picture. With Secure Boot on, Debian's signed
# GRUB only loads fonts that are inside its own signed image (GNU Unifont and
# nothing else): an Albert Sans .pf2 on the disk is "prohibited by secure boot
# policy". Pictures render the same with Secure Boot on, off, and on legacy
# BIOS, so the menu looks identical everywhere.
#
# The measurements are the UsbMenu artboard's (1440x900). Everything is placed
# from the middle of the screen, as the artboard's centred column is, and the
# footer from the bottom edge.
set -euo pipefail
# bash 5.2 expands & in ${var//pattern/replacement}; the XML escaping needs it
# literal.
shopt -u patsub_replacement 2>/dev/null || true

usage() {
    echo "usage: grub-theme.sh usb <theme dir> <logo svg> <version> <languages file>" >&2
    echo "       grub-theme.sh installed <theme dir> <logo svg>" >&2
    exit 2
}
[ $# -ge 3 ] || usage
mode=$1
out=$2
logo=$3
case $mode in
    usb) [ $# -eq 5 ] || usage; version=$4; languages=$5 ;;
    installed) [ $# -eq 3 ] || usage ;;
    *) usage ;;
esac
bg='#05070d'
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Albert Sans is not in Debian; the copy wildos-branding ships is the one
# drawn with, so the build needs no font installed.
fonts=$(cd "$(dirname "$0")/../files/fonts/albert-sans" && pwd)
FONTCONFIG_FILE=$work/fonts.conf
printf '<?xml version="1.0"?>\n<fontconfig><include ignore_missing="yes">/etc/fonts/fonts.conf</include><dir>%s</dir></fontconfig>\n' \
    "$fonts" > "$FONTCONFIG_FILE"
export FONTCONFIG_FILE

rm -rf "$out"
mkdir -p "$out/icons"

# GRUB only reads 8- or 16-bit PNGs, and ImageMagick shrinks flat images to
# palettes unless told otherwise, so every image is written as PNG32.
png() { magick "$1" "PNG32:$2"; }

# svg OUT WIDTH HEIGHT BODY [BACKGROUND]
svg() {
    local fill=""
    [ -n "${5:-}" ] && fill="<rect width='$2' height='$3' fill='$5'/>"
    printf '<svg xmlns="http://www.w3.org/2000/svg" width="%s" height="%s">%s%s</svg>\n' \
        "$2" "$3" "$fill" "$4" > "$work/img.svg"
    rsvg-convert "$work/img.svg" -o "$work/img.png"
    png "$work/img.png" "$1"
}

# text X BASELINE SIZE WEIGHT OPACITY ANCHOR STRING
text() {
    local s=$7
    s=${s//&/&amp;}; s=${s//</&lt;}; s=${s//\"/&quot;}
    printf '<text xml:space="preserve" x="%s" y="%s" font-family="Albert Sans" font-size="%s" font-weight="%s" fill="#e8ecf8" fill-opacity="%s" text-anchor="%s">%s</text>' \
        "$1" "$2" "$3" "$4" "$5" "$6" "$s"
}

# The artboard's dawn glow along the bottom edge is left out: GRUB cannot
# move things by screen height, and on screens 800px tall or less the menu
# and the countdown's solid frames sit inside it and cut it off.

# The logo tile and the wordmark, 16px apart, as one centred row.
rsvg-convert -w 56 -h 56 "$logo" -o "$work/logo.png"
svg "$work/word.png" 114 56 "<text x='0' y='39' font-family='Albert Sans' font-size='30' font-weight='550' letter-spacing='-0.3' fill='#eef1f8'>Wild OS</text>"
magick -size 186x56 xc:none "$work/logo.png" -geometry +0+0 -composite \
    "$work/word.png" -geometry +72+0 -composite "PNG32:$out/header.png"

# An installed system updates without a new menu being drawn, so its footer
# leaves the version out rather than show one that has gone stale.
if [ "$mode" = usb ]; then
    footer="Wild OS $version · Free and open source · wildos.io"
else
    footer="Wild OS · Free and open source · wildos.io"
fi
svg "$out/footer.png" 480 16 "$(text 240 12 12 400 0.55 middle "$footer")"

# Menu entries. GRUB draws a title in one line of one font, so each entry's
# title and description are its icon (selected by --class), 420x66, starting
# where the artboard's text starts. The selection box is drawn beneath it.
item() {
    svg "$out/icons/$1.png" 420 66 \
        "<text x='0' y='30' font-family='Albert Sans' font-size='17' font-weight='$2' fill='#eef1f8'>$3</text>$(text 0 50 13 400 0.72 start "$4")"
}
# The artboard shows the default entry selected, with its title in semibold.
# GRUB cannot swap an icon when the selection moves, so the weights are the
# artboard's: semibold for the default action, medium for the rest.
if [ "$mode" = usb ]; then
    item wildos-install 550 "Install Wild OS" "Set up Wild OS on this computer"
    item wildos-try 480 "Try Wild OS" "Use it from the USB stick. Nothing on your computer changes."
    item wildos-check 480 "Check this USB stick" "Looks for download or copying errors first"
    # The Language entry names the language in use, so there is one picture
    # per language; each language in the list is named in itself.
    while IFS=$'	' read -r code _ native english; do
        case $code in '#'* | '') continue ;; esac
        item "wildos-language-$code" 480 "Language" "$native"
        item "wildos-lang-$code" 480 "$native" "$english"
    done < "$languages"
    item wildos-advanced 480 "Advanced" "Safe graphics, other options"
    item wildos-install-safe 480 "Install with safe graphics" "Use this if the screen stays black or garbled"
    item wildos-try-safe 480 "Try with safe graphics" "The same fix, without installing anything"
    item wildos-messages 480 "Show start-up messages" "Starts Wild OS with text instead of the logo"
    item wildos-firmware 480 "Firmware settings" "Restart into this computer's UEFI settings"
else
    # The classes are the ones wildos-base's /etc/grub.d scripts give their
    # entries. "windows" is also the class of the installer's Windows entry
    # and of os-prober's, so every copy of Windows gets this picture.
    item wildos 550 "Wild OS" "Start as usual"
    item wildos-safe-graphics 480 "Wild OS (safe graphics)" "Use this if the screen stays black or garbled"
    item wildos-previous 480 "Wild OS (previous version)" "Use this if Wild OS stopped starting after an update"
    item windows 480 "Windows" "Start Windows on this computer"
    # wildos-startup's entries: its picture menu, and the other system it
    # or Settings chose to start.
    item wildos-manager 480 "Start-up manager" "Choose what this computer starts"
    item wildos-other 480 "Another system" "Start the other system on this computer"
    item wildos-firmware 480 "UEFI firmware settings" "Restart into this computer's UEFI settings"
fi

# The selection: a 560x66 rounded box (12px radius, 1px border) with the
# accent dot, cut into GRUB's 9-slice parts. Only the left end, a 1px middle
# column and the right end are used, all at full height, so GRUB never
# scales the dot or the corners.
svg "$work/select.png" 560 66 "<rect width='560' height='66' rx='12' fill='#8fb4ff' fill-opacity='0.16'/><rect x='0.5' y='0.5' width='559' height='65' rx='11.5' fill='none' stroke='#8fb4ff' stroke-opacity='0.5'/><circle cx='27' cy='33' r='4' fill='#8fb4ff'/>"
magick "$work/select.png" -crop 47x66+0+0 +repage "PNG32:$out/select_w.png"
magick "$work/select.png" -crop 1x66+47+0 +repage "PNG32:$out/select_c.png"
magick "$work/select.png" -crop 13x66+547+0 +repage "PNG32:$out/select_e.png"
# Unselected entries get the same (empty) ends, so the icons do not move.
magick -size 47x66 xc:none "PNG32:$out/item_w.png"
magick -size 13x66 xc:none "PNG32:$out/item_e.png"

# GRUB still draws each entry's plain-text title (the text menu's, kept for
# when graphics fail) in at least 80px to the right of the icon, in Unifont,
# and ignores a transparent colour. It is drawn in exactly the colour beneath
# it instead: the background, or the selection's tint as GRUB blends it
# (fbblit.c alpha_dilute), so it cannot be seen.
blend() {
    local s=$(($2 * $3 + $1 * (255 ^ $3)))
    local h=$((s >> 8)) l=$((s & 255))
    [ $((h + l)) -ge 255 ] && h=$((h + 1))
    echo $h
}
read -r tr tg tb ta < <(magick "$out/select_c.png" -format '%[fx:round(p{0,33}.r*255)] %[fx:round(p{0,33}.g*255)] %[fx:round(p{0,33}.b*255)] %[fx:round(p{0,33}.a*255)]\n' info:)
selected_text=$(printf '#%02x%02x%02x' "$(blend 5 "$tr" "$ta")" "$(blend 7 "$tg" "$ta")" "$(blend 13 "$tb" "$ta")")

hint_w=600
svg "$out/hint.png" $hint_w 16 "$(text 300 13 13 400 0.7 middle "Use ↑ and ↓ to choose, then press Enter.")"

# The console GRUB falls back to (the editor, errors) and the screen behind
# GRUB's text while it loads Wild OS.
for part in n s e w ne nw se sw c; do
    magick -size 1x1 xc:"$bg" "PNG32:$out/terminal_$part.png"
done
magick -size 64x64 xc:"$bg" "PNG32:$out/background.png"

# menu HEIGHT: the boot menu, with the header above it. GRUB paints a
# theme's components from the last one listed to the first, so a theme file
# lists them from the top layer down and these come last.
menu() {
    cat <<EOF

+ boot_menu {
    left = 50%-280
    top = 50%-204
    width = 560
    height = $1
    item_height = 66
    item_spacing = 6
    item_padding = 0
    icon_width = 420
    icon_height = 66
    item_icon_space = 0
    item_font = "Unifont Regular 16"
    selected_item_font = "Unifont Regular 16"
    item_color = "$bg"
    selected_item_color = "$selected_text"
    item_pixmap_style = "item_*.png"
    selected_item_pixmap_style = "select_*.png"
    scrollbar = false
}

+ image {
    left = 50%-93
    top = 50%-300
    file = "header.png"
}
EOF
}

# header GENERATOR
header() {
    cat <<EOF
# Generated by packages/wildos-branding/tools/grub-theme.sh ($1).
desktop-color: "$bg"
title-text: ""
terminal-font: "Unifont Regular 16"
terminal-box: "terminal_*.png"
EOF
}

if [ "$mode" = installed ]; then
    # The hint sits above the footer rather than under the menu: how many
    # entries there are (Windows, firmware settings) is only known once GRUB
    # runs, and one fixed place keeps it clear of a menu of any length. The
    # menu has room for six entries, more than Wild OS ever lists itself.
    {
        header "installed system"
        cat <<EOF

+ image {
    left = 50%-$((hint_w / 2))
    top = 100%-91
    file = "hint.png"
}

+ image {
    left = 50%-240
    top = 100%-47
    file = "footer.png"
}
EOF
        menu $((6 * 66 + 5 * 6 + 6))
    } > "$out/theme.txt"
    exit 0
fi

# The hint line under the menu, counting down to "Install Wild OS".
#
# GRUB's own countdown label needs a font, so the number is drawn by progress
# bars, which GRUB updates every second (theme id "__timeout__"). The bottom
# bar shows the whole line with 8 seconds. Above it, bar j's highlight has the
# number for j seconds gone as its left end and a transparent spacer as its
# right end, sized so GRUB, which skips a highlight narrower than its two ends,
# only draws it once j seconds have passed. The newest second's bar is on top.
# A keypress stops the countdown and hides every bar, which leaves the plain
# hint underneath.
#
# GRUB clips a component at the screen's right edge, so these bars must be
# narrow: each number gets a fixed cell (the widest digit), placed where the
# centred line puts it. GRUB makes a progress bar at least 28px tall, so the
# frames are 28px with the 16px line in their middle.
countdown=8
prefix='Use ↑ and ↓ to choose, then press Enter. Starting "Install Wild OS" in '

# advance STRING: how far 13px Albert Sans moves the pen, measured from where a
# marker drawn after the string lands.
marker_x() {
    svg "$work/m.png" 700 28 "$(text 10 19 13 400 1 start "$1")"
    magick "$work/m.png" -format '%@' info: | sed 's/.*+\([0-9]*\)+.*/\1/'
}
advance() {
    local s=$1
    s=${s//&/&amp;}; s=${s//</&lt;}; s=${s//\"/&quot;}
    svg "$work/m.png" 700 28 "<text xml:space='preserve' x='10' y='19' font-family='Albert Sans' font-size='13' fill='#fff'><tspan fill-opacity='0'>$s</tspan>H</text>"
    echo $(($(magick "$work/m.png" -format '%@' info: | sed 's/.*+\([0-9]*\)+.*/\1/') - $(marker_x H)))
}
cell=0
for d in 0 1 2 3 4 5 6 7 8 9; do
    w=$(advance "$d")
    [ "$w" -gt $cell ] && cell=$w
done
prefix_w=$(advance "$prefix")
suffix_w=$(advance " seconds.")
x0=$(( (hint_w - prefix_w - cell - suffix_w) / 2 ))
xd=$((x0 + prefix_w))

digit_x=$((2 * xd + cell))
digit_x=$((digit_x / 2)).$((digit_x % 2 * 5))

line() {
    local unit=" seconds."
    [ "$1" -eq 1 ] && unit=" second."
    svg "$2" $hint_w 28 "$(text $x0 19 13 400 0.7 start "$prefix")$(text $digit_x 19 13 400 0.7 middle "$1")$(text $((xd + cell)) 19 13 400 0.7 start "$unit")" "$bg"
}
svg "$out/hint-advanced.png" $hint_w 16 "$(text 300 13 13 400 0.7 middle "Use ↑ and ↓ to choose, then press Enter. Press Esc to go back.")"

magick -size 1x1 xc:none "PNG32:$out/blank_c.png"
line $countdown "$out/count${countdown}_c.png"
# The last second also drops the "s", so its frame covers the whole word.
last_w=$((cell + suffix_w + 1))
# Bar j's highlight needs j steps of the bar's width, which GRUB makes at
# least 200px: step must leave room for every frame plus a 1px spacer.
step=$(( (200 + countdown - 1) / countdown ))
[ $step -le $cell ] && step=$((cell + 1))
[ $(((countdown - 1) * step)) -le $last_w ] && step=$(( last_w / (countdown - 1) + 1 ))
for ((gone = 1; gone < countdown; gone++)); do
    left=$((countdown - gone))
    w=$cell; [ $left -eq 1 ] && w=$last_w
    line $left "$work/line.png"
    magick "$work/line.png" -crop ${w}x28+$xd+0 +repage "PNG32:$out/count${left}_w.png"
    magick -size $((gone * step - w))x28 xc:none "PNG32:$out/count${left}_e.png"
done

# theme FILE HINT_IMAGE ENTRIES WITH_COUNTDOWN
theme() {
    local menu_h=$(($3 * 66 + ($3 - 1) * 6))
    local hint_top=$((-204 + menu_h + 40))
    {
        header "USB stick"
        if [ "$4" = yes ]; then
            for ((gone = countdown - 1; gone >= 1; gone--)); do
                cat <<EOF

+ progress_bar {
    id = "__timeout__"
    left = 50%$(printf %+d $((xd - hint_w / 2)))
    top = 50%+$((hint_top - 6))
    width = $((countdown * step))
    height = 28
    bar_style = "blank_*.png"
    highlight_style = "count$((countdown - gone))_*.png"
}
EOF
            done
            cat <<EOF

+ progress_bar {
    id = "__timeout__"
    left = 50%-$((hint_w / 2))
    top = 50%+$((hint_top - 6))
    width = $hint_w
    height = 28
    bar_style = "count${countdown}_*.png"
    highlight_style = "blank_*.png"
}
EOF
        fi
        cat <<EOF

+ image {
    left = 50%-$((hint_w / 2))
    top = 50%+$hint_top
    file = "$2"
}

+ image {
    left = 50%-240
    top = 100%-47
    file = "footer.png"
}
EOF
        menu $((menu_h + 6))
    } > "$out/$1"
}
theme theme.txt hint.png 5 yes
theme advanced-efi.txt hint-advanced.png 4 no
theme advanced-pc.txt hint-advanced.png 3 no
# The language list scrolls past six.
theme language.txt hint-advanced.png 6 no
