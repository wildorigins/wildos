#!/bin/sh
# Sourced by dracut before switching to the installed system.
#
# wildos.snapshot=<id>: the start-up menu started a Btrfs restore point.
#   The read-only snapshot is the lower layer of an overlay whose changes
#   stay in memory, so trying a restore point changes nothing on disk.
#
# A staged go-back (ext4): Settings wrote
#   /var/lib/wildos/restore-points/pending. The system is unlocked but not
#   yet started, so its files are put back now by the restore-points tool
#   of the system itself, chrooted. If the running kernel's modules went
#   with the newer system, the PC restarts into the restored kernel.

wildos_snapshot() {
    id=$(getarg wildos.snapshot=)
    [ -n "$id" ] || return 0
    base=/run/wildos-snapshot
    mkdir -p "$base/lower" "$base/rw"
    # The restore point is mounted a second time for the layer to stand on.
    # Moving /sysroot there is refused: it sits under a shared mount, and
    # every restore point then started read-only with no layer at all.
    dev=$(findmnt -no SOURCE /sysroot | sed 's/\[.*$//')
    if ! mount -t btrfs -o "ro,subvol=@snapshots/$id/snapshot" "$dev" "$base/lower"; then
        warn "wildos: could not layer restore point $id; starting it read-only"
        return 0
    fi
    mount -t tmpfs -o mode=0755 tmpfs "$base/rw"
    mkdir -p "$base/rw/upper" "$base/rw/work"
    if ! mount -t overlay overlay \
        -o "lowerdir=$base/lower,upperdir=$base/rw/upper,workdir=$base/rw/work" /sysroot; then
        warn "wildos: could not layer restore point $id; starting it read-only"
        umount "$base/rw" "$base/lower"
    fi
}

wildos_apply_pending() {
    [ -f /sysroot/var/lib/wildos/restore-points/pending ] || return 0
    [ -x /sysroot/usr/libexec/wildos/restore-points ] || return 0

    plymouth display-message --text="Going back to an earlier version. This takes a few minutes." 2>/dev/null
    mount -o remount,rw /sysroot || return 0

    boot_dev=$(sed -n 's/^[[:space:]]*\([^#[:space:]][^[:space:]]*\)[[:space:]][[:space:]]*\/boot[[:space:]].*/\1/p' /sysroot/etc/fstab | head -n 1)
    case "$boot_dev" in
        UUID=*) boot_dev="/dev/disk/by-uuid/${boot_dev#UUID=}" ;;
    esac
    if [ -n "$boot_dev" ]; then
        mount "$boot_dev" /sysroot/boot 2>/dev/null
    fi
    for d in proc sys dev run; do mount --bind "/$d" "/sysroot/$d"; done

    chroot /sysroot /usr/bin/python3 -I /usr/libexec/wildos/restore-points apply-pending
    rc=$?

    for d in run dev sys proc; do umount "/sysroot/$d"; done
    sync
    umount /sysroot/boot 2>/dev/null
    mount -o remount,ro /sysroot 2>/dev/null
    plymouth display-message --text="" 2>/dev/null

    if [ "$rc" = 3 ]; then
        info "wildos: restarting into the restored kernel"
        sync
        reboot -f
    fi
}

wildos_snapshot
wildos_apply_pending
