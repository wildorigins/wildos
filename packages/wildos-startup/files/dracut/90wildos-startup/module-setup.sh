#!/bin/bash
# The start-up manager in the start-up files (wildos-startup).
#
# It has to run before the boot screen and before the disk is unlocked, so
# everything it needs is here: the program, its pictures and words, the
# typeface, and a note of which disks are which on this PC.

check() {
    [[ -x /usr/lib/wildos/startup-manager ]] || return 1
    # The image on a USB stick is built for every PC and starts one system.
    [[ $hostonly ]] || return 1
    return 0
}

depends() {
    echo systemd
    return 0
}

installkernel() {
    # FAT to look for other systems' boot loaders, NTFS for a Windows on a
    # PC without UEFI, and the keyboards the choice is made with.
    hostonly='' instmods vfat nls_cp437 nls_ascii nls_utf8 ntfs3 evdev hid_generic usbhid xhci_pci xhci_hcd ehci_pci
}

install() {
    inst_simple /usr/lib/wildos/startup-manager
    inst_multiple -o cryptsetup
    local f
    for f in /usr/share/wildos-startup/*.png /usr/share/wildos-startup/i18n/*.tsv \
        /usr/share/fonts/truetype/albert-sans/*.ttf; do
        [[ -e $f ]] && inst_simple "$f"
    done
    # Plymouth brings a font configuration with it; without Plymouth the
    # typeface still has to be found.
    if [[ ! -e $initdir/etc/fonts/fonts.conf ]]; then
        mkdir -p "$initdir/etc/fonts"
        printf '%s\n' '<?xml version="1.0"?>' '<fontconfig><dir>/usr/share/fonts</dir></fontconfig>' \
            > "$initdir/etc/fonts/fonts.conf"
    fi

    local boot_uuid boot_rel esp_uuid luks_uuid root_uuid root_opts lang
    boot_uuid=$(findmnt -no UUID -T /boot 2>/dev/null)
    boot_rel=$(grub-mkrelpath /boot 2>/dev/null | sed 's,/*$,,')
    esp_uuid=$(findmnt -no UUID /boot/efi 2>/dev/null)
    luks_uuid=$(awk '!/^#/ && $2 ~ /^UUID=/ {sub("UUID=", "", $2); print $2; exit}' /etc/crypttab 2>/dev/null)
    root_uuid=$(findmnt -no UUID / 2>/dev/null)
    root_opts=$(findmnt -no OPTIONS / 2>/dev/null | tr ',' '\n' | grep -m1 '^subvol=')
    lang=$(sed -n 's/^LANG=\([^.]*\).*/\1/p' /etc/default/locale 2>/dev/null | head -n 1)
    printf '%s\n' "boot_uuid=$boot_uuid" "boot_rel=$boot_rel" "esp_uuid=$esp_uuid" \
        "luks_uuid=$luks_uuid" "root_uuid=$root_uuid" "root_opts=$root_opts" "lang=$lang" \
        > "$initdir/etc/wildos-startup.conf"

    inst_simple "$moddir/wildos-startup-manager.service" \
        "$systemdsystemunitdir/wildos-startup-manager.service"
    $SYSTEMCTL -q --root "$initdir" add-wants sysinit.target wildos-startup-manager.service
}
