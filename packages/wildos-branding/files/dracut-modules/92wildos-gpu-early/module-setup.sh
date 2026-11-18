#!/bin/bash
# The graphics driver takes the screen over before the boot screen starts.
#
# Plymouth starts as soon as udev has been triggered, but udev only gets to
# the graphics driver a second or so later. Until then Plymouth draws on the
# firmware's framebuffer; when the driver takes the screen over, that
# framebuffer goes away under Plymouth, which stops, leaves the screen black
# for a moment and starts again on the new device. Loading this PC's own
# driver early and starting Plymouth once the driver's screen exists means
# the animation is drawn once, on the device it stays on.
#
# Only in a host-only initramfs, where this PC's graphics hardware is known.
# The live image is built for every PC and leaves graphics drivers out on
# purpose (build/hooks/80-live-initrd.sh), so it never gets this.

check() {
    [[ $hostonly ]] || return 255
    return 0
}

depends() {
    echo drm
    return 0
}

# Settings > Graphics driver writes this when NVIDIA's own driver is
# installed, and removes it when the open one is put back.
nvidia_own() {
    [[ -e /etc/modprobe.d/60-wildos-nvidia.conf ]]
}

# The drivers bound to this PC's displays, read from the screens themselves
# so every make works the same (i915, xe, amdgpu, nouveau, radeon and the
# rest). NVIDIA's own driver is left to load as before: its display part is
# not in the initramfs, and loading it early would not give Plymouth a
# screen.
#
# The image is also rebuilt while the driver is being switched, when the one
# running is not the one the next start-up uses: nouveau is left out once
# NVIDIA's driver is installed (it is deny-listed then), and put back in
# when NVIDIA's driver has just been removed.
gpu_modules() {
    local card mod nvidia_running=
    local -a mods=()
    for card in /sys/class/drm/card[0-9]*; do
        [[ $card == *-* ]] && continue
        [[ -e $card/device/driver/module ]] || continue
        mod=$(basename "$(readlink -f "$card/device/driver/module")")
        case $mod in
            nvidia*) nvidia_running=1; continue ;;
            nouveau) nvidia_own && continue ;;
        esac
        [[ " ${mods[*]} " == *" $mod "* ]] || mods+=("$mod")
    done
    if [[ $nvidia_running ]] && ! nvidia_own && [[ " ${mods[*]} " != *" nouveau "* ]]; then
        mods+=(nouveau)
    fi
    echo "${mods[*]}"
}

installkernel() {
    [[ $hostonly ]] || return 0
    local mods
    mods=$(gpu_modules)
    [[ $mods ]] || return 0
    # shellcheck disable=SC2086
    hostonly='' instmods $mods
    return 0
}

# systemd-modules-load starts loading them at once, and Plymouth waits for a
# graphics card on the PCI bus to be registered: the driver's own screen.
# That is a few tenths of a second into the load; the rest of it (setting
# up the text console on a large panel took over a second) goes on while
# Plymouth already draws. The wait gives up after three seconds.
install() {
    [[ $hostonly ]] || return 0
    # NVIDIA's packages ask for their module at start-up, and dracut copies
    # that request into the image without the module, so loading failed
    # twice on every start-up before the real system loaded it.
    rm -f "$initdir/etc/modules-load.d/nvidia.conf"
    # NVIDIA's own driver only loads in the real system, about ten seconds
    # into start-up, so on a PC whose monitor is on the NVIDIA card Plymouth
    # has no driver's screen to draw on until the sign-in screen is due. The
    # firmware's own screen is there all along (NVIDIA's driver leaves it in
    # place), but Plymouth only turns to it once DeviceTimeout has passed:
    # eight seconds by default, which is the whole start-up. The wait has to
    # end before Plymouth is told about the real system
    # (plymouth-switch-root, about 0.14 seconds after Plymouth starts): from
    # then on Plymouth looks for screens inside /sysroot, where /sys and
    # udev's list of devices only arrive once the switch to the real system
    # is over, about a second later, and are filled in again after that. A
    # wait that ends in between finds no screen at all, so Plymouth settles
    # on its plain text screen (three dots) for the rest of start-up; 0.3
    # seconds and one second both did. Plymouth starts after the screens
    # have been found (gpu-wait below), so it does not need to wait: 0.01
    # seconds ends before the command that starts Plymouth has returned,
    # which is before systemd can start the next step. A PC whose screen is
    # on another card is not affected: that card's driver is up before
    # Plymouth starts, and its screen is used straight away.
    local conf="$initdir/etc/plymouth/plymouthd.conf"
    if nvidia_own && [[ -e $conf ]]; then
        sed -i '/^DeviceTimeout=/d' "$conf"
        if grep -q '^\[Daemon\]' "$conf"; then
            sed -i '0,/^\[Daemon\]/s//[Daemon]\nDeviceTimeout=0.01/' "$conf"
        else
            printf '%s\n' '[Daemon]' 'DeviceTimeout=0.01' >> "$conf"
        fi
    fi
    local mods
    mods=$(gpu_modules)
    [[ $mods ]] || return 0
    inst_multiple sleep readlink
    mkdir -p "$initdir/etc/modules-load.d" "$initdir/usr/lib/wildos" \
        "$initdir$systemdsystemunitdir/plymouth-start.service.d"
    printf '%s\n' $mods > "$initdir/etc/modules-load.d/wildos-gpu-early.conf"
    cat > "$initdir/usr/lib/wildos/gpu-wait" <<'EOF'
#!/bin/sh
i=0
while [ "$i" -lt 30 ]; do
    for card in /sys/class/drm/card[0-9]*; do
        case "$card" in *-*) continue ;; esac
        case "$(readlink -f "$card/device/subsystem" 2>/dev/null)" in
            */pci) exit 0 ;;
        esac
    done
    sleep 0.1
    i=$((i + 1))
done
exit 0
EOF
    chmod 0755 "$initdir/usr/lib/wildos/gpu-wait"
    printf '%s\n' '[Unit]' 'Wants=systemd-modules-load.service' '' '[Service]' \
        'ExecStartPre=-/usr/lib/wildos/gpu-wait' \
        > "$initdir$systemdsystemunitdir/plymouth-start.service.d/wildos-gpu-early.conf"
    return 0
}
