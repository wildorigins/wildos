#!/bin/bash
# The keyboard light comes on at the disk unlock prompt, which runs in the
# initramfs, long before the installed system's udev rules can see it.

check() {
    [ -x /usr/libexec/wildos/kbd-backlight-default ] || return 1
    return 0
}

depends() {
    echo udev-rules
    return 0
}

install() {
    inst_multiple mkdir
    inst_script /usr/libexec/wildos/kbd-backlight-default
    inst_rules 90-wildos-kbd-backlight.rules
}

# The drivers behind this PC's keyboard lights, found from the lights
# themselves so every make works the same (dell-laptop, thinkpad_acpi,
# asus-nb-wmi, hid-asus, system76 and the rest). No disk uses them, so a
# host-only initramfs would leave them out. The live image is not built
# host-only and has no unlock prompt, so it gets none.
installkernel() {
    [[ $hostonly ]] || return 0
    local led dev
    local -a mods=()
    for led in /sys/class/leds/*::kbd_backlight*; do
        [[ -e $led/device ]] || continue
        dev=$(readlink -f "$led/device")
        while [[ $dev == /sys/devices/* ]]; do
            if [[ -e $dev/driver/module ]]; then
                mods+=("$(basename "$(readlink -f "$dev/driver/module")")")
                break
            fi
            dev=${dev%/*}
        done
    done
    if ((${#mods[@]})); then
        hostonly='' instmods "${mods[@]}"
    fi
    return 0
}
