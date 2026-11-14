#!/bin/bash
# Builds the Wild OS live ISO. Runs inside the builder container (see
# Dockerfile); use build.ps1 or build-iso.sh at the repo root to launch it.
#
#   /src    repo checkout, read-only
#   /work   scratch space on a Docker volume (Windows bind mounts cannot hold
#           device nodes or Linux permissions, so the rootfs must live here)
#   /cache  persistent apt archive cache between builds
#   /out    where the finished ISO is written
#
#   build.sh [--incremental | --packages]
#
# --incremental keeps the root filesystem and the built packages for the next
# run, which then only reinstalls what changed (see incremental.sh).
# --packages stops once the packages for the update repository are in
# /out/debs, without making an image.

set -euo pipefail

# bash reads scripts as it executes them, so editing the checkout during a
# build changes the running build. Work from a snapshot instead.
if [ -z "${WILDOS_SNAPSHOT:-}" ]; then
    rm -rf /tmp/src && mkdir -p /tmp/src
    # site/ is the update repository publish-local.sh clones beside the
    # checkout: hundreds of megabytes the build never reads.
    tar -C /src --exclude=./out --exclude=./.git --exclude=./site -cf - . | tar -C /tmp/src -xf -
    WILDOS_SNAPSHOT=1 exec bash /tmp/src/build/build.sh "$@"
fi

SRC=/tmp/src
WORK=/work
CACHE=/cache/apt
# Patched Debian rebuilds (Calamares, KIO, Kvantum and the rest) are shared by
# every build on this PC when build-iso.sh mounts /rebuilds, so a new
# worktree's build doesn't spend most of an hour making them again.
REBUILDS=/rebuilds
[ -d "$REBUILDS" ] || REBUILDS=/cache
# Compiles go through ccache, shared the same way, so a package whose code
# barely changed recompiles in seconds.
if [ -d /ccache ] && [ -d /usr/lib/ccache ]; then
    export PATH="/usr/lib/ccache:$PATH" CCACHE_DIR=/ccache CCACHE_BASEDIR=/work CCACHE_MAXSIZE=25G CCACHE_COMPILERCHECK=content
fi
# Compiler processes in all, by memory as well as cores: a Qt compile can
# take over a gigabyte, so cores alone would run out of memory first.
mem_kib=$(awk '/MemTotal/ {print $2}' /proc/meminfo)
limit=$(cat /sys/fs/cgroup/memory.max 2>/dev/null || echo max)
[ "$limit" != max ] && [ $(( limit / 1024 )) -lt "$mem_kib" ] && mem_kib=$(( limit / 1024 ))
BUILD_THREADS=$(( mem_kib / 1572864 ))
[ "$BUILD_THREADS" -gt "$(nproc)" ] && BUILD_THREADS=$(nproc)
[ "$BUILD_THREADS" -lt 2 ] && BUILD_THREADS=2
OUT=/out
SUITE=trixie
VOLID=WILDOS
VERSION=$(sed -n '1s/.*(\(.*\)).*/\1/p' "$SRC/packages/wildos-base/debian/changelog")
ISO_NAME="wildos-${VERSION}-amd64.iso"
KICKSECURE_KEY=/usr/share/keyrings/kicksecure-derivative.asc

log() { printf '\n==> [%s] %s\n' "$(date +%T)" "$*"; }

ROOTFS=$WORK/rootfs
SQUASH_LEVEL=19
INCREMENTAL=0
PACKAGES_ONLY=0
[ "${1:-}" = --packages ] && PACKAGES_ONLY=1
if [ "${1:-}" = --incremental ]; then
    INCREMENTAL=1
    source "$SRC/build/incremental.sh"
    ROOTFS=$INC/rootfs
fi

# Builds take turns, one at a time on this PC unless WILDOS_BUILD_SLOTS allows
# more. The cache is shared by every build, so locks in it make a further
# build wait instead of slowing every build and starving the test VMs of
# memory. A publish (WILDOS_PUBLISH=1) has a slot of its own, so an update
# never queues behind work in progress; publishes take turns with each other.
mkdir -p /cache
if [ "${WILDOS_PUBLISH:-}" = 1 ]; then
    slots=(.publish-lock)
else
    slots=(.build-lock)
    for ((n = 2; n <= ${WILDOS_BUILD_SLOTS:-1}; n++)); do slots+=(".build-lock-$n"); done
fi
slot=
while [ -z "$slot" ]; do
    for try in "${slots[@]}"; do
        exec 9>"/cache/$try"
        if flock -n 9; then slot=$try; break; fi
        exec 9>&-
    done
    [ -n "$slot" ] && break
    if [ -z "${waited:-}" ]; then
        log "Waiting for a build slot. Building now:"
        for try in "${slots[@]}"; do sed 's/^/    /' "/cache/$try.who" 2>/dev/null || true; done
        waited=1
    fi
    # With one slot, wait on its lock itself rather than asking again and again.
    if [ "${#slots[@]}" = 1 ]; then
        exec 9>"/cache/${slots[0]}"
        flock 9
        slot=${slots[0]}
    else
        sleep 5
    fi
done
echo "${WILDOS_ONLY:-every package} from ${WILDOS_CHECKOUT:-a checkout}, since $(date +%T)" > "/cache/$slot.who"
# Which checkout this scratch volume belongs to, so volumes left behind by
# deleted worktrees can be found and removed.
[ -n "${WILDOS_CHECKOUT:-}" ] && echo "$WILDOS_CHECKOUT" > "$WORK/.checkout"

log "Wild OS $VERSION"
rm -rf "$WORK/pkgsrc" "$WORK/repo" "$WORK/rootfs" "$WORK/iso"
mkdir -p "$WORK/pkgsrc" "$WORK/repo" "$WORK/iso" "$CACHE" "$OUT"

log "Building Wild OS packages"
cp -r "$SRC/packages/." "$WORK/pkgsrc/"
# Artwork has one master copy at the repo root; packages that ship it build
# their sizes and formats from it.
rm -rf "$WORK/pkgsrc/wildos-branding/branding"
cp -r "$SRC/branding" "$WORK/pkgsrc/wildos-branding/branding"
# Remote is built on Windows mode's Remote Desktop engine. Its own copy
# makes its build stand alone and changes the package's hash whenever the
# engine changes, so an incremental build rebuilds it too.
rm -rf "$WORK/pkgsrc/wildos-remote/rdp-engine"
mkdir -p "$WORK/pkgsrc/wildos-remote/rdp-engine"
cp -r "$SRC/packages/wildos-rdp/engine.cmake" "$SRC/packages/wildos-rdp/src" "$WORK/pkgsrc/wildos-remote/rdp-engine/"
# Windows bind mounts report every file as executable. debhelper treats an
# executable debian/install as a script and installs its output instead of its
# contents, which silently produces empty packages.
find "$WORK/pkgsrc" -type f -exec chmod 0644 {} +
chmod 0755 "$WORK"/pkgsrc/*/debian/rules
# A build that feeds the update repository carries a revision (CI passes
# the commit count), so each one is a newer version than the last and apt
# offers it as an update. Local and incremental builds keep the plain
# version, and with it their package cache.
#
# The top entry's date becomes the build time too. debhelper clamps every
# file's mtime to that date, and Qt's QML disk cache (plasmashell keeps one in
# ~/.cache/plasmashell/qmlcache) only recompiles a file whose mtime changed,
# so with a fixed date an update kept running the old compiled taskbar.
# The version suffix of a Debian package Wild OS rebuilds with its own patches
# (build/patches/<name>): +wildosN, where N counts the commits that changed
# those patches, so a rebuild with changed patches is always a newer version.
# With one version for two different builds, apt keeps the build a computer
# has while PackageKit offers the published one as an update for ever.
# build-iso.sh counts them (the builder has no git) and passes
# WILDOS_PATCH_COUNTS="calamares=1 kvantum=2 ..."; without it the count is 1.
wildos_suffix() {
    local entry n=
    for entry in ${WILDOS_PATCH_COUNTS:-}; do
        [ "${entry%%=*}" = "$1" ] && n=${entry#*=}
    done
    echo "+wildos${n:-1}"
}

# A release image carries Wild OS's packages exactly as the update repository
# has them. Rebuilding them stamped every one with the release's own +rN,
# higher than the published copies of unchanged packages, and a fresh install
# was then offered dozens of "updates" to identical packages with lower
# numbers (deb.wildos.io is pinned above everything). A source whose version
# is published, every one of its packages, is taken from there and checked
# against the repository's SHA-256 sums; anything not published is built.
if [ -n "${WILDOS_PACKAGES_FROM:-}" ]; then
    log "Taking published Wild OS packages from $WILDOS_PACKAGES_FROM"
    # The builder has Python, not curl.
    fetch() {
        python3 -I -c 'import sys, urllib.request, shutil
with urllib.request.urlopen(sys.argv[1], timeout=120) as r, open(sys.argv[2], "wb") as f:
    shutil.copyfileobj(r, f, 1 << 20)' "$1" "$2" || { echo "ERROR: could not download $1" >&2; exit 1; }
    }
    index="$WORK/published-Packages"
    fetch "$WILDOS_PACKAGES_FROM/dists/stable/main/binary-amd64/Packages" "$index"
    published_srcs=
    for pkg in "$WORK"/pkgsrc/*/; do
        [ -e "$pkg/NOT-READY" ] && continue
        ver=$(dpkg-parsechangelog -l "$pkg/debian/changelog" -S Version)
        bins=$(sed -n 's/^Package:[[:space:]]*//p' "$pkg/debian/control")
        files=()
        for bin in $bins; do
            entry=$(awk -v p="$bin" -v v="$ver" '
                /^Package: /{name=$2} /^Version: /{version=$2} /^Filename: /{file=$2} /^SHA256: /{sum=$2}
                /^$/{ if (name == p && (version == v || index(version, v "+r") == 1)) print file, sum; name="" }
                END{ if (name == p && (version == v || index(version, v "+r") == 1)) print file, sum }' "$index" | tail -n 1)
            [ -n "$entry" ] || { files=(); break; }
            files+=("$entry")
        done
        [ "${#files[@]}" -gt 0 ] || continue
        for entry in "${files[@]}"; do
            file=${entry% *}
            sum=${entry#* }
            fetch "$WILDOS_PACKAGES_FROM/$file" "$WORK/repo/$(basename "$file")"
            echo "$sum  $WORK/repo/$(basename "$file")" | sha256sum -c --quiet \
                || { echo "ERROR: $file does not match the repository's checksum" >&2; exit 1; }
        done
        published_srcs="$published_srcs $(basename "$pkg")"
    done
    log "Published, not rebuilt:${published_srcs:- none}"
    # The rest are built as usual.
    for name in $published_srcs; do touch "$WORK/pkgsrc/$name/PUBLISHED"; done
fi

if [ -n "${WILDOS_REVISION:-}" ]; then
    stamp=$(date -R)
    for changelog in "$WORK"/pkgsrc/*/debian/changelog; do
        sed -i -e "1s/(\([^)]*\))/(\1+r$WILDOS_REVISION)/" \
            -e "0,/^ -- /s/^\( -- .*>\)  .*/\1  $stamp/" "$changelog"
    done
fi
if [ "$INCREMENTAL" = 1 ]; then
    inc_build_packages
else
    # Several packages at once: much of a package build (debhelper, QML
    # caches, compressing) uses one core, so one at a time leaves most of
    # them idle. Each keeps its own log, shown if it fails.
    # Counted here: `jobs` inside $(...) sees no jobs, which once let the
    # build carry on before the packages were made.
    max_jobs=$(( $(nproc) / 4 ))
    [ "$max_jobs" -lt 2 ] && max_jobs=2
    # BUILD_THREADS (cores and memory) is shared out between the packages
    # building at once, not handed to each one in full: 6 packages each told
    # to use all 24 cores asked for 144 compilers at a time.
    todo=0
    for pkg in "$WORK"/pkgsrc/*/; do
        [ -e "$pkg/NOT-READY" ] && continue
        [ -e "$pkg/PUBLISHED" ] && continue
        if [ -n "${WILDOS_ONLY+set}" ] && [[ " $WILDOS_ONLY " != *" $(basename "$pkg") "* ]]; then continue; fi
        todo=$((todo + 1))
    done
    if [ "$todo" -gt 0 ] && [ "$todo" -lt "$max_jobs" ]; then max_jobs=$todo; fi
    threads=$(( BUILD_THREADS / max_jobs ))
    [ "$threads" -lt 1 ] && threads=1
    failed=0
    running=0
    built=()
    for pkg in "$WORK"/pkgsrc/*/; do
        # Work in progress is kept out of the image until it is ready.
        [ -e "$pkg/NOT-READY" ] && continue
        # Already in the image as published (WILDOS_PACKAGES_FROM).
        [ -e "$pkg/PUBLISHED" ] && continue
        # WILDOS_ONLY, when set, names the packages to build (an update that
        # reuses the rest as published).
        if [ -n "${WILDOS_ONLY+set}" ] && [[ " $WILDOS_ONLY " != *" $(basename "$pkg") "* ]]; then
            continue
        fi
        if [ "$running" -ge "$max_jobs" ]; then
            wait -n || failed=1
            running=$((running - 1))
        fi
        name=$(basename "$pkg")
        built+=("$name")
        (cd "$pkg" && DEB_BUILD_OPTIONS="parallel=$threads" dpkg-buildpackage -us -uc -b -d -tc >"$WORK/build-$name.log" 2>&1 \
            || { echo "ERROR: $name failed to build:" >&2; tail -40 "$WORK/build-$name.log" >&2; exit 1; }) &
        running=$((running + 1))
    done
    while [ "$running" -gt 0 ]; do
        wait -n || failed=1
        running=$((running - 1))
    done
    [ "$failed" = 0 ] || exit 1
    rm -f "$WORK"/build-*.log
fi
find "$WORK/pkgsrc" -maxdepth 1 -name '*.deb' -exec mv {} "$WORK/repo/" \;
# Every package asked for has to have come out of it.
for name in "${built[@]:-}"; do
    [ -z "$name" ] && continue
    ls "$WORK/repo/${name}_"*.deb >/dev/null 2>&1 || { echo "ERROR: no package came out of $name" >&2; exit 1; }
done

# Debian's plymouth declares "Conflicts: console-common", which Kicksecure's
# core layer depends on. The two share no files: the conflict dates from
# console-common's old init script loading keymaps while the boot splash held
# the console, and systemd systems no longer run that script. Republish
# plymouth with only that line removed and a +wildos1 version so apt prefers
# it. When Debian updates plymouth, this picks up the new version on the next
# build, and installed systems get it from the Wild OS update repository.
(
    cd "$WORK"
    # The builder image drops its package lists to stay small.
    apt-get update -qq
    apt-get download -qq plymouth
    deb=$(ls plymouth_*.deb)
    rm -rf plymouth-repack && dpkg-deb -R "$deb" plymouth-repack
    control=plymouth-repack/DEBIAN/control
    sed -i '/^Conflicts: console-common$/d' "$control"
    sed -i 's/^Version: \(.*\)$/Version: \1+wildos1/' "$control"
    if grep -q "console-common" "$control"; then
        echo "ERROR: plymouth's control file changed shape; update the repack" >&2
        exit 1
    fi
    dpkg-deb -b plymouth-repack "$WORK/repo/" >/dev/null
    rm -rf plymouth-repack "$deb"
    # plymouth-label holds the boot splash's text plugins and wants exactly
    # Debian's plymouth version, so it needs the same +wildos1. Without it
    # the start-up screen has no way to draw text and shows boxes instead.
    apt-get download -qq plymouth-label
    deb=$(ls plymouth-label_*.deb)
    rm -rf label-repack && dpkg-deb -R "$deb" label-repack
    control=label-repack/DEBIAN/control
    sed -i 's/^Version: \(.*\)$/Version: \1+wildos1/' "$control"
    sed -i 's/plymouth (= \([^)]*\))/plymouth (= \1+wildos1)/' "$control"
    if ! grep -q "plymouth (= .*+wildos1)" "$control"; then
        echo "ERROR: plymouth-label's control file changed shape; update the repack" >&2
        exit 1
    fi
    dpkg-deb -b label-repack "$WORK/repo/" >/dev/null
    rm -rf label-repack "$deb"
)
# Calamares with Wild OS's patches from build/patches/calamares, versioned
# +wildos1 so apt prefers it. Building Calamares takes several minutes, so
# the result is kept in the cache and rebuilt only when Debian's version or
# the patches change.
(
    cd "$WORK"
    version=$(apt-cache policy calamares | sed -n 's/^  Candidate: //p')
    stamp=$(cat "$SRC"/build/patches/calamares/*.patch | sha256sum | cut -c1-12)
    cached="$REBUILDS/calamares/$version$(wildos_suffix calamares)-$stamp"
    if [ ! -d "$cached" ]; then
        log "Building patched Calamares $version"
        echo "deb-src http://deb.debian.org/debian $SUITE main" > /etc/apt/sources.list.d/wildos-src.list
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get build-dep -y -qq calamares >/dev/null
        rm -rf calamares-src && mkdir calamares-src && cd calamares-src
        apt-get source -qq "calamares=$version" >/dev/null
        cd calamares-*/
        # Calamares falls back to Qt6 only when Qt5 is missing, and the
        # builder has Qt5's development files for LibreOffice, so ask for Qt6.
        sed -i 's/-DKDE_INSTALL_USE_QT_SYS_PATHS=ON/& -DWITH_QT6=ON/' debian/rules
        grep -q -- -DWITH_QT6=ON debian/rules || { echo "Could not set WITH_QT6 in Calamares' debian/rules" >&2; exit 1; }
        mkdir -p debian/patches
        for patch in "$SRC"/build/patches/calamares/*.patch; do
            cp "$patch" debian/patches/
            basename "$patch" >> debian/patches/series
        done
        {
            printf 'calamares (%s%s) %s; urgency=medium\n\n' "$version" "$(wildos_suffix calamares)" "$SUITE"
            printf '  * Wild OS patches: %s\n\n' "$(cd "$SRC/build/patches/calamares" && ls *.patch | tr '\n' ' ')"
            printf ' -- Wild OS Developers <dev@wildos.io>  %s\n\n' "$(date -R)"
            cat debian/changelog
        } > debian/changelog.new
        mv debian/changelog.new debian/changelog
        DEB_BUILD_OPTIONS="nocheck parallel=$BUILD_THREADS" dpkg-buildpackage -us -uc -b >/dev/null
        fill="$cached.part.$BASHPID"
        rm -rf "$fill" && mkdir -p "$fill"
        cp ../calamares_*+wildos*_*.deb "$fill/"
        cd "$WORK" && rm -rf calamares-src
        # Another build may have made the same one meanwhile; either copy is good.
        mv -T "$fill" "$cached" 2>/dev/null || rm -rf "$fill"
    fi
    cp "$cached"/*.deb "$WORK/repo/"
)
# Kvantum, the engine that draws Qt and KDE apps with the WildOS theme, with
# the patches from build/patches/kvantum (solid primary buttons, rounded
# menus with tight padding, the accent from Settings). Cached like Calamares.
(
    cd "$WORK"
    version=$(apt-cache policy qt-style-kvantum | sed -n 's/^  Candidate: //p')
    if [ -z "$version" ] || [ "$version" = "(none)" ]; then
        apt-get update -qq
        version=$(apt-cache policy qt-style-kvantum | sed -n 's/^  Candidate: //p')
    fi
    stamp=$(cat "$SRC"/build/patches/kvantum/*.patch | sha256sum | cut -c1-12)
    cached="$REBUILDS/kvantum/$version$(wildos_suffix kvantum)-$stamp"
    if [ ! -d "$cached" ]; then
        log "Building patched Kvantum $version"
        echo "deb-src http://deb.debian.org/debian $SUITE main" > /etc/apt/sources.list.d/wildos-src.list
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get build-dep -y -qq "qt-style-kvantum=$version" >/dev/null
        rm -rf kvantum-src && mkdir kvantum-src && cd kvantum-src
        apt-get source -qq "qt-style-kvantum=$version" >/dev/null
        cd kvantum-*/
        mkdir -p debian/patches
        for patch in "$SRC"/build/patches/kvantum/*.patch; do
            cp "$patch" debian/patches/
            basename "$patch" >> debian/patches/series
        done
        {
            printf 'kvantum (%s%s) %s; urgency=medium\n\n' "$version" "$(wildos_suffix kvantum)" "$SUITE"
            printf '  * Wild OS patches: %s\n\n' "$(cd "$SRC/build/patches/kvantum" && ls *.patch | tr '\n' ' ')"
            printf ' -- Wild OS Developers <dev@wildos.io>  %s\n\n' "$(date -R)"
            cat debian/changelog
        } > debian/changelog.new
        mv debian/changelog.new debian/changelog
        DEB_BUILD_OPTIONS="nocheck parallel=$BUILD_THREADS" dpkg-buildpackage -us -uc -b >/dev/null
        fill="$cached.part.$BASHPID"
        rm -rf "$fill" && mkdir -p "$fill"
        cp ../*+wildos[0-9]*_*.deb "$fill/"
        rm -f "$fill"/*dbgsym*
        cd "$WORK" && rm -rf kvantum-src
        # Another build may have made the same one meanwhile; either copy is good.
        mv -T "$fill" "$cached" 2>/dev/null || rm -rf "$fill"
    fi
    cp "$cached"/*.deb "$WORK/repo/"
)
# KIO, the file library every KDE app copies and moves files with, with the
# patches from build/patches/kio: a name clash asks in the Wild OS clash
# dialog (wildos-portal) instead of KIO's own. All of KIO's packages are
# republished +wildos1, since they require each other's exact version.
# Cached like Calamares.
(
    cd "$WORK"
    version=$(apt-cache policy libkf6kiocore6 | sed -n 's/^  Candidate: //p')
    if [ -z "$version" ] || [ "$version" = "(none)" ]; then
        apt-get update -qq
        version=$(apt-cache policy libkf6kiocore6 | sed -n 's/^  Candidate: //p')
    fi
    stamp=$(cat "$SRC"/build/patches/kio/*.patch | sha256sum | cut -c1-12)
    cached="$REBUILDS/kio/$version$(wildos_suffix kio)-$stamp"
    if [ ! -d "$cached" ]; then
        log "Building patched KIO $version"
        echo "deb-src http://deb.debian.org/debian $SUITE main" > /etc/apt/sources.list.d/wildos-src.list
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get build-dep -y -qq "kf6-kio=$version" >/dev/null
        rm -rf kio-src && mkdir kio-src && cd kio-src
        apt-get source -qq "kf6-kio=$version" >/dev/null
        cd kf6-kio-*/
        mkdir -p debian/patches
        for patch in "$SRC"/build/patches/kio/*.patch; do
            cp "$patch" debian/patches/
            basename "$patch" >> debian/patches/series
        done
        {
            printf 'kf6-kio (%s%s) %s; urgency=medium\n\n' "$version" "$(wildos_suffix kio)" "$SUITE"
            printf '  * Wild OS patches: %s\n\n' "$(cd "$SRC/build/patches/kio" && ls *.patch | tr '\n' ' ')"
            printf ' -- Wild OS Developers <dev@wildos.io>  %s\n\n' "$(date -R)"
            cat debian/changelog
        } > debian/changelog.new
        mv debian/changelog.new debian/changelog
        DEB_BUILD_OPTIONS="nocheck nodoc parallel=$BUILD_THREADS" dpkg-buildpackage -us -uc -b >/dev/null
        fill="$cached.part.$BASHPID"
        rm -rf "$fill" && mkdir -p "$fill"
        cp ../*+wildos[0-9]*_*.deb "$fill/"
        rm -f "$fill"/*dbgsym*
        cd "$WORK" && rm -rf kio-src
        # Another build may have made the same one meanwhile; either copy is good.
        mv -T "$fill" "$cached" 2>/dev/null || rm -rf "$fill"
    fi
    cp "$cached"/*.deb "$WORK/repo/"
)
# LibreOffice, which Write and Tables are, with the patches from
# build/patches/libreoffice: the Wild OS ribbon, style cards, Tables' formula
# bar, sheet tabs and headings, Write and Tables' status bars, and no empty
# window while a document loads. The patched code is mostly in libmergedlo.so in libreoffice-core,
# and every LibreOffice package built for the architecture requires that
# exact version of it, so all of them are rebuilt +wildosN; the build-arch
# half of Debian's build is enough for those (no help, no language packs).
# libreoffice-writer and -calc also require their uiconfig package at
# exactly the same version: those are architecture-independent, so Debian's
# own are republished at the new version with the few files the patches
# change (the status bar layouts) taken from this build.
# Cached by the patches and Debian's version like Calamares: the first
# build compiles all of LibreOffice once (about 70 minutes at six jobs in a
# 10 GB build, and 35 GB of scratch space), later builds reuse it until the
# patches or Debian's LibreOffice change, and a build after a patch change
# takes most of its objects from LibreOffice's own compiler cache.
(
    cd "$WORK"
    version=$(apt-cache policy libreoffice-core | sed -n 's/^  Candidate: //p')
    if [ -z "$version" ] || [ "$version" = "(none)" ]; then
        apt-get update -qq
        version=$(apt-cache policy libreoffice-core | sed -n 's/^  Candidate: //p')
    fi
    stamp=$(cat "$SRC"/build/patches/libreoffice/*.patch | sha256sum | cut -c1-12)
    cached="$REBUILDS/libreoffice/$version$(wildos_suffix libreoffice)-$stamp"
    if [ ! -d "$cached" ]; then
        log "Building patched LibreOffice $version"
        echo "deb-src http://deb.debian.org/debian $SUITE main" > /etc/apt/sources.list.d/wildos-src.list
        echo "deb-src http://deb.debian.org/debian-security $SUITE-security main" >> /etc/apt/sources.list.d/wildos-src.list
        apt-get update -qq
        profiles="nocheck nodoc noinsttest"
        DEBIAN_FRONTEND=noninteractive apt-get build-dep -y -qq -P "${profiles// /,}" "libreoffice=$version" >/dev/null
        rm -rf lo-src && mkdir lo-src && cd lo-src
        apt-get source -qq "libreoffice=$version" >/dev/null
        cd libreoffice-*/
        for patch in "$SRC"/build/patches/libreoffice/*.patch; do
            cp "$patch" debian/patches/
            basename "$patch" >> debian/patches/series
        done
        {
            printf 'libreoffice (%s%s) %s; urgency=medium\n\n' "$version" "$(wildos_suffix libreoffice)" "$SUITE"
            printf '  * Wild OS patches: %s\n\n' "$(cd "$SRC/build/patches/libreoffice" && ls *.patch | tr '\n' ' ')"
            printf ' -- Wild OS Developers <dev@wildos.io>  %s\n\n' "$(date -R)"
            cat debian/changelog
        } > debian/changelog.new
        mv debian/changelog.new debian/changelog
        # On amd64 Debian compiles everything twice, first without a GUI for
        # the -nogui packages; Wild OS ships none of those. LibreOffice keeps
        # a compiler cache of its own beside the shared one (its objects
        # alone would fill that and push out everything else's), so a patch
        # change recompiles only the files it touches rather than all of
        # LibreOffice.
        CCACHE_DIR=/ccache/libreoffice CCACHE_MAXSIZE=40G \
        DEB_BUILD_PROFILES="$profiles" DEB_BUILD_OPTIONS="nocheck nodoc noautodbgsym parallel=$BUILD_THREADS" \
            dpkg-buildpackage -us -uc -B -R'debian/rules BUILD_NOGUI_PACKAGES=n' >/dev/null
        # Filled under another name and renamed when complete, so a build
        # that stops half way is not taken for a finished one next time.
        part="$cached.part"
        rm -rf "$part" && mkdir -p "$part"
        cp ../*+wildos[0-9]*_*.deb "$part/"
        rm -f "$part"/*dbgsym* "$part"/*-tests_* "$part"/*-smoketest-*
        built="$PWD/instdir/share/config/soffice.cfg"
        # The uiconfig packages the rebuilt ones require, republished at the
        # new version. Only those: libreoffice-common, which is not rebuilt,
        # requires Debian's uiconfig-common at Debian's version.
        mkdir -p "$WORK/lo-uiconfig" && cd "$WORK/lo-uiconfig"
        for pkg in $(for deb in "$part"/*.deb; do dpkg-deb -f "$deb" Depends; done |
                     { grep -o 'libreoffice-uiconfig-[a-z-]*' || true; } | sort -u); do
            apt-get download -qq "$pkg=$version"
            deb=$(ls "${pkg}"_*.deb)
            dpkg-deb -R "$deb" repack
            cfg=repack/usr/lib/libreoffice/share/config/soffice.cfg
            if [ -d "$cfg" ]; then
                (cd "$cfg" && find . -type f) | while read -r f; do
                    if [ -f "$built/$f" ] && ! cmp -s "$built/$f" "$cfg/$f"; then
                        cp "$built/$f" "$cfg/$f"
                    fi
                done
            fi
            (cd repack && find usr -type f -exec md5sum {} + > DEBIAN/md5sums)
            sed -i "s/^Version: .*/Version: $version$(wildos_suffix libreoffice)/" repack/DEBIAN/control
            dpkg-deb -b repack "$part/" >/dev/null
            rm -rf repack "$deb"
        done
        cd "$WORK" && rm -rf lo-uiconfig lo-src
        mv "$part" "$cached"
    fi
    cp "$cached"/*.deb "$WORK/repo/"
)
# libfprint with the patch from build/patches/libfprint: the open driver for
# the Goodix 27c6:533c reader in the Dell XPS 13 9300 and 9310 (only its
# 533c family, which never writes a key or firmware to the sensor), with
# every other reader Debian's libfprint drives unchanged. Its own test suite
# runs once, when the rebuild is made. Republished +wildos1 like KIO, since
# its packages require each other's exact version. Cached like Calamares.
(
    cd "$WORK"
    version=$(apt-cache policy libfprint-2-2 | sed -n 's/^  Candidate: //p')
    if [ -z "$version" ] || [ "$version" = "(none)" ]; then
        apt-get update -qq
        version=$(apt-cache policy libfprint-2-2 | sed -n 's/^  Candidate: //p')
    fi
    stamp=$(cat "$SRC"/build/patches/libfprint/*.patch | sha256sum | cut -c1-12)
    cached="$REBUILDS/libfprint/$version$(wildos_suffix libfprint)-$stamp"
    if [ ! -d "$cached" ]; then
        log "Building patched libfprint $version"
        echo "deb-src http://deb.debian.org/debian $SUITE main" > /etc/apt/sources.list.d/wildos-src.list
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get build-dep -y -qq "libfprint=$version" >/dev/null
        rm -rf libfprint-src && mkdir libfprint-src && cd libfprint-src
        apt-get source -qq "libfprint=$version" >/dev/null
        cd libfprint-*/
        mkdir -p debian/patches
        for patch in "$SRC"/build/patches/libfprint/*.patch; do
            cp "$patch" debian/patches/
            basename "$patch" >> debian/patches/series
        done
        cat >> debian/copyright <<'COPYRIGHT'

Added by Wild OS: libfprint/drivers/goodix53x5 and tests/test-goodix53x5-*,
from https://github.com/seaweeduk/goodix53x5-libfprint pull request 334.
Copyright © 2024-2026 goodix-fp-linux-dev contributors, under the same
licence as the rest of libfprint (LGPL 2.1 or any later version).
COPYRIGHT
        {
            printf 'libfprint (%s%s) %s; urgency=medium\n\n' "$version" "$(wildos_suffix libfprint)" "$SUITE"
            printf '  * Wild OS patches: %s\n\n' "$(cd "$SRC/build/patches/libfprint" && ls *.patch | tr '\n' ' ')"
            printf ' -- Wild OS Developers <dev@wildos.io>  %s\n\n' "$(date -R)"
            cat debian/changelog
        } > debian/changelog.new
        mv debian/changelog.new debian/changelog
        DEB_BUILD_OPTIONS="nodoc parallel=$BUILD_THREADS" dpkg-buildpackage -us -uc -B >/dev/null
        fill="$cached.part.$BASHPID"
        rm -rf "$fill" && mkdir -p "$fill"
        cp ../*+wildos[0-9]*_*.deb "$fill/"
        rm -f "$fill"/*dbgsym* "$fill"/*-tests_*
        cd "$WORK" && rm -rf libfprint-src
        # Another build may have made the same one meanwhile; either copy is good.
        mv -T "$fill" "$cached" 2>/dev/null || rm -rf "$fill"
    fi
    cp "$cached"/*.deb "$WORK/repo/"
)
# KXmlGui, the main window framework of KDE's apps, with the patch from
# build/patches/kxmlgui: windows with a hamburger menu start without their
# menu bar, so every app has a slim header. Republished +wildos1 like KIO.
(
    cd "$WORK"
    version=$(apt-cache policy libkf6xmlgui6 | sed -n 's/^  Candidate: //p')
    if [ -z "$version" ] || [ "$version" = "(none)" ]; then
        apt-get update -qq
        version=$(apt-cache policy libkf6xmlgui6 | sed -n 's/^  Candidate: //p')
    fi
    stamp=$(cat "$SRC"/build/patches/kxmlgui/*.patch | sha256sum | cut -c1-12)
    cached="$REBUILDS/kxmlgui/$version$(wildos_suffix kxmlgui)-$stamp"
    if [ ! -d "$cached" ]; then
        log "Building patched KXmlGui $version"
        echo "deb-src http://deb.debian.org/debian $SUITE main" > /etc/apt/sources.list.d/wildos-src.list
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get build-dep -y -qq "kf6-kxmlgui=$version" >/dev/null
        rm -rf kxmlgui-src && mkdir kxmlgui-src && cd kxmlgui-src
        apt-get source -qq "kf6-kxmlgui=$version" >/dev/null
        cd kf6-kxmlgui-*/
        mkdir -p debian/patches
        for patch in "$SRC"/build/patches/kxmlgui/*.patch; do
            cp "$patch" debian/patches/
            basename "$patch" >> debian/patches/series
        done
        {
            printf 'kf6-kxmlgui (%s%s) %s; urgency=medium\n\n' "$version" "$(wildos_suffix kxmlgui)" "$SUITE"
            printf '  * Wild OS patches: %s\n\n' "$(cd "$SRC/build/patches/kxmlgui" && ls *.patch | tr '\n' ' ')"
            printf ' -- Wild OS Developers <dev@wildos.io>  %s\n\n' "$(date -R)"
            cat debian/changelog
        } > debian/changelog.new
        mv debian/changelog.new debian/changelog
        DEB_BUILD_OPTIONS="nocheck nodoc parallel=$BUILD_THREADS" dpkg-buildpackage -us -uc -b >/dev/null
        fill="$cached.part.$BASHPID"
        rm -rf "$fill" && mkdir -p "$fill"
        cp ../*+wildos[0-9]*_*.deb "$fill/"
        rm -f "$fill"/*dbgsym*
        cd "$WORK" && rm -rf kxmlgui-src
        # Another build may have made the same one meanwhile; either copy is good.
        mv -T "$fill" "$cached" 2>/dev/null || rm -rf "$fill"
    fi
    cp "$cached"/*.deb "$WORK/repo/"
)
# KWallet with the patch from build/patches/kwallet: kwalletd can change the
# key a wallet opens with when the account's password changes, without its
# own dialog, so Settings keeps the wallet opening at sign-in after a
# password change. Republished +wildos1 like KIO, since its packages require
# each other's exact version.
(
    cd "$WORK"
    version=$(apt-cache policy kwallet6 | sed -n 's/^  Candidate: //p')
    if [ -z "$version" ] || [ "$version" = "(none)" ]; then
        apt-get update -qq
        version=$(apt-cache policy kwallet6 | sed -n 's/^  Candidate: //p')
    fi
    stamp=$(cat "$SRC"/build/patches/kwallet/*.patch | sha256sum | cut -c1-12)
    cached="$REBUILDS/kwallet/$version$(wildos_suffix kwallet)-$stamp"
    if [ ! -d "$cached" ]; then
        log "Building patched KWallet $version"
        echo "deb-src http://deb.debian.org/debian $SUITE main" > /etc/apt/sources.list.d/wildos-src.list
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get build-dep -y -qq "kf6-kwallet=$version" >/dev/null
        rm -rf kwallet-src && mkdir kwallet-src && cd kwallet-src
        apt-get source -qq "kf6-kwallet=$version" >/dev/null
        cd kf6-kwallet-*/
        mkdir -p debian/patches
        for patch in "$SRC"/build/patches/kwallet/*.patch; do
            cp "$patch" debian/patches/
            basename "$patch" >> debian/patches/series
        done
        {
            printf 'kf6-kwallet (%s%s) %s; urgency=medium\n\n' "$version" "$(wildos_suffix kwallet)" "$SUITE"
            printf '  * Wild OS patches: %s\n\n' "$(cd "$SRC/build/patches/kwallet" && ls *.patch | tr '\n' ' ')"
            printf ' -- Wild OS Developers <dev@wildos.io>  %s\n\n' "$(date -R)"
            cat debian/changelog
        } > debian/changelog.new
        mv debian/changelog.new debian/changelog
        DEB_BUILD_OPTIONS="nocheck nodoc parallel=$BUILD_THREADS" dpkg-buildpackage -us -uc -b >/dev/null
        fill="$cached.part.$BASHPID"
        rm -rf "$fill" && mkdir -p "$fill"
        cp ../*+wildos[0-9]*_*.deb "$fill/"
        rm -f "$fill"/*dbgsym*
        cd "$WORK" && rm -rf kwallet-src
        # Another build may have made the same one meanwhile; either copy is good.
        mv -T "$fill" "$cached" 2>/dev/null || rm -rf "$fill"
    fi
    cp "$cached"/*.deb "$WORK/repo/"
)
# kactivitymanagerd, which keeps the recent files history, with the patch
# from build/patches/kactivitymanagerd: Forget Recent Files on one app's
# taskbar menu forgets only that app's files instead of emptying Start's
# recent list too.
(
    cd "$WORK"
    version=$(apt-cache policy kactivitymanagerd | sed -n 's/^  Candidate: //p')
    if [ -z "$version" ] || [ "$version" = "(none)" ]; then
        apt-get update -qq
        version=$(apt-cache policy kactivitymanagerd | sed -n 's/^  Candidate: //p')
    fi
    stamp=$(cat "$SRC"/build/patches/kactivitymanagerd/*.patch | sha256sum | cut -c1-12)
    cached="$REBUILDS/kactivitymanagerd/$version$(wildos_suffix kactivitymanagerd)-$stamp"
    if [ ! -d "$cached" ]; then
        log "Building patched kactivitymanagerd $version"
        echo "deb-src http://deb.debian.org/debian $SUITE main" > /etc/apt/sources.list.d/wildos-src.list
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get build-dep -y -qq "kactivitymanagerd=$version" >/dev/null
        rm -rf kactivitymanagerd-src && mkdir kactivitymanagerd-src && cd kactivitymanagerd-src
        apt-get source -qq "kactivitymanagerd=$version" >/dev/null
        cd kactivitymanagerd-*/
        mkdir -p debian/patches
        for patch in "$SRC"/build/patches/kactivitymanagerd/*.patch; do
            cp "$patch" debian/patches/
            basename "$patch" >> debian/patches/series
        done
        {
            printf 'kactivitymanagerd (%s%s) %s; urgency=medium\n\n' "$version" "$(wildos_suffix kactivitymanagerd)" "$SUITE"
            printf '  * Wild OS patches: %s\n\n' "$(cd "$SRC/build/patches/kactivitymanagerd" && ls *.patch | tr '\n' ' ')"
            printf ' -- Wild OS Developers <dev@wildos.io>  %s\n\n' "$(date -R)"
            cat debian/changelog
        } > debian/changelog.new
        mv debian/changelog.new debian/changelog
        DEB_BUILD_OPTIONS="nocheck nodoc parallel=$BUILD_THREADS" dpkg-buildpackage -us -uc -b >/dev/null
        fill="$cached.part.$BASHPID"
        rm -rf "$fill" && mkdir -p "$fill"
        cp ../*+wildos[0-9]*_*.deb "$fill/"
        rm -f "$fill"/*dbgsym*
        cd "$WORK" && rm -rf kactivitymanagerd-src
        # Another build may have made the same one meanwhile; either copy is good.
        mv -T "$fill" "$cached" 2>/dev/null || rm -rf "$fill"
    fi
    cp "$cached"/*.deb "$WORK/repo/"
)
# Plasma's platform theme for Qt apps with the patch from
# build/patches/plasma-integration: a Qt or KDE app's Open and Save dialog
# (Wild OS's, through the desktop portal) is attached to the app's window on
# Wayland, with no taskbar button of its own. Republished +wildos1 like KIO.
(
    cd "$WORK"
    version=$(apt-cache policy plasma-integration | sed -n 's/^  Candidate: //p')
    if [ -z "$version" ] || [ "$version" = "(none)" ]; then
        apt-get update -qq
        version=$(apt-cache policy plasma-integration | sed -n 's/^  Candidate: //p')
    fi
    stamp=$(cat "$SRC"/build/patches/plasma-integration/*.patch | sha256sum | cut -c1-12)
    cached="$REBUILDS/plasma-integration/$version$(wildos_suffix plasma-integration)-$stamp"
    if [ ! -d "$cached" ]; then
        log "Building patched plasma-integration $version"
        echo "deb-src http://deb.debian.org/debian $SUITE main" > /etc/apt/sources.list.d/wildos-src.list
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get build-dep -y -qq "plasma-integration=$version" >/dev/null
        rm -rf plasma-integration-src && mkdir plasma-integration-src && cd plasma-integration-src
        apt-get source -qq "plasma-integration=$version" >/dev/null
        cd plasma-integration-*/
        mkdir -p debian/patches
        for patch in "$SRC"/build/patches/plasma-integration/*.patch; do
            cp "$patch" debian/patches/
            basename "$patch" >> debian/patches/series
        done
        {
            printf 'plasma-integration (%s%s) %s; urgency=medium\n\n' "$version" "$(wildos_suffix plasma-integration)" "$SUITE"
            printf '  * Wild OS patches: %s\n\n' "$(cd "$SRC/build/patches/plasma-integration" && ls *.patch | tr '\n' ' ')"
            printf ' -- Wild OS Developers <dev@wildos.io>  %s\n\n' "$(date -R)"
            cat debian/changelog
        } > debian/changelog.new
        mv debian/changelog.new debian/changelog
        DEB_BUILD_OPTIONS="nocheck nodoc parallel=$BUILD_THREADS" dpkg-buildpackage -us -uc -b >/dev/null
        fill="$cached.part.$BASHPID"
        rm -rf "$fill" && mkdir -p "$fill"
        cp ../*+wildos[0-9]*_*.deb "$fill/"
        rm -f "$fill"/*dbgsym*
        cd "$WORK" && rm -rf plasma-integration-src
        # Another build may have made the same one meanwhile; either copy is good.
        mv -T "$fill" "$cached" 2>/dev/null || rm -rf "$fill"
    fi
    cp "$cached"/*.deb "$WORK/repo/"
)
# evolution-data-server with Camel's mail modules (IMAP, SMTP and the rest)
# split into evolution-data-server-camel-providers, which needs only
# libcamel, and its calendar and contacts services into
# evolution-data-server-core. Wild OS Mail, Calendar and People use those
# without the rest of EDS, whose package brings WebKitGTK, GTK and
# gnome-keyring (a second Secret Service beside KWallet).
# evolution-data-server itself depends on both, so Evolution works as
# before. A packaging change, not a source patch, so it
# is made here; republished +wildos1 like KIO, since its packages require
# each other's exact version.
(
    cd "$WORK"
    version=$(apt-cache policy libcamel-1.2-64t64 | sed -n 's/^  Candidate: //p')
    if [ -z "$version" ] || [ "$version" = "(none)" ]; then
        apt-get update -qq
        version=$(apt-cache policy libcamel-1.2-64t64 | sed -n 's/^  Candidate: //p')
    fi
    stamp=$(sed -n '/^# evolution-data-server with Camel/,/^)$/p' "$SRC/build/build.sh" | sha256sum | cut -c1-12)
    cached="$REBUILDS/eds/$version$(wildos_suffix eds)-$stamp"
    if [ ! -d "$cached" ]; then
        log "Building evolution-data-server $version with the Camel providers split out"
        echo "deb-src http://deb.debian.org/debian $SUITE main" > /etc/apt/sources.list.d/wildos-src.list
        echo "deb-src http://deb.debian.org/debian-security $SUITE-security main" >> /etc/apt/sources.list.d/wildos-src.list
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get build-dep -y -qq "evolution-data-server=$version" >/dev/null
        rm -rf eds-src && mkdir eds-src && cd eds-src
        apt-get source -qq "evolution-data-server=$version" >/dev/null
        cd evolution-data-server-*/
        # The lists below divide Debian's install list between three
        # packages, so they are only right for the list they were written
        # against. dh_missing catches a file left out.
        echo "addacfaf2a60e9914c1baeaa549c9d1f6674edd8dc9d1e687b926c08255f3ee7  debian/evolution-data-server.install" | sha256sum -c --quiet \
            || { echo "ERROR: evolution-data-server's install list changed; update the split" >&2; exit 1; }
        printf '%s\n' 'usr/lib/evolution-data-server/camel-providers/*.urls' 'usr/lib/evolution-data-server/camel-providers/*.so' \
            > debian/evolution-data-server-camel-providers.install
        # The core: the source registry and the calendar and contacts
        # services with their backends, none of which link GTK or WebKit.
        # Wild OS Calendar and People run on it, with KWallet's Secret
        # Service holding their passwords.
        cat > debian/evolution-data-server-core.install <<'LIST'
usr/libexec/evolution-addressbook-*
usr/libexec/evolution-calendar-*
usr/libexec/evolution-data-server/addressbook-export
usr/libexec/evolution-data-server/csv2vcard
usr/libexec/evolution-data-server/list-sources
usr/libexec/evolution-scan-gconf-tree-xml
usr/libexec/evolution-source-registry
usr/lib/evolution-data-server/addressbook-backends/*.so
usr/lib/evolution-data-server/calendar-backends/libecalbackendcaldav.so
usr/lib/evolution-data-server/calendar-backends/libecalbackendcontacts.so
usr/lib/evolution-data-server/calendar-backends/libecalbackendfile.so
usr/lib/evolution-data-server/calendar-backends/libecalbackendgtasks.so
usr/lib/evolution-data-server/calendar-backends/libecalbackendhttp.so
usr/lib/evolution-data-server/calendar-backends/libecalbackendwebdavnotes.so
usr/lib/evolution-data-server/registry-modules/module-cache-reaper.so
usr/lib/evolution-data-server/registry-modules/module-google-backend.so
usr/lib/evolution-data-server/registry-modules/module-oauth2-services.so
usr/lib/evolution-data-server/registry-modules/module-outlook-backend.so
usr/lib/evolution-data-server/registry-modules/module-secret-monitor.so
usr/lib/evolution-data-server/registry-modules/module-webdav-backend.so
usr/lib/evolution-data-server/registry-modules/module-yahoo-backend.so
usr/lib/systemd/user/evolution-addressbook-factory.service
usr/lib/systemd/user/evolution-calendar-factory.service
usr/lib/systemd/user/evolution-source-registry.service
LIST
        # What stays: the GTK and WebKit programs (reminders, prompts, the
        # OAuth window, the certificate prompt), weather, GNOME Online
        # Accounts and Camel's helpers, with gnome-keyring as before.
        cat > debian/evolution-data-server.install <<'LIST'
etc
usr/libexec/camel-*
usr/libexec/evolution-data-server/evolution-alarm-notify
usr/libexec/evolution-data-server/evolution-oauth2-handler
usr/libexec/evolution-user-prompter
usr/lib/evolution-data-server/calendar-backends/libecalbackendweather.so
usr/lib/evolution-data-server/credential-modules/*.so
usr/lib/evolution-data-server/registry-modules/module-gnome-online-accounts.so
usr/lib/evolution-data-server/registry-modules/module-trust-prompt.so
usr/lib/systemd/user/evolution-user-prompter.service
usr/share/applications/org.gnome.Evolution-alarm-notify.desktop
usr/share/applications/org.gnome.evolution-data-server.OAuth2-handler.desktop
usr/share/icons/hicolor/scalable/apps/org.gnome.Evolution-alarm-notify.svg
LIST
        sed -i 's/^         libcamel-1.2-64t64 (= ${binary:Version}),$/&\n         evolution-data-server-camel-providers (= ${binary:Version}),\n         evolution-data-server-core (= ${binary:Version}),/' debian/control
        grep -q 'evolution-data-server-core (= ' debian/control \
            || { echo "ERROR: evolution-data-server's Depends changed shape; update the split" >&2; exit 1; }
        cat >> debian/control <<'STANZA'

Package: evolution-data-server-camel-providers
Architecture: any
Depends: ${shlibs:Depends},
         ${misc:Depends},
         libcamel-1.2-64t64 (= ${binary:Version}),
Breaks: evolution-data-server (<< ${binary:Version})
Replaces: evolution-data-server (<< ${binary:Version})
Description: Camel mail providers (IMAP, SMTP, POP3 and local mail)
 The modules Camel, Evolution Data Server's mail library, uses to reach
 mail servers and mail stored on disk, split out of evolution-data-server
 for programs that use Camel without the rest of it.

Package: evolution-data-server-core
Architecture: any
Depends: ${shlibs:Depends},
         ${misc:Depends},
         evolution-data-server-common (>= ${source:Version}),
         libcamel-1.2-64t64 (= ${binary:Version}),
Breaks: evolution-data-server (<< ${binary:Version})
Replaces: evolution-data-server (<< ${binary:Version})
Description: Evolution Data Server's calendar and contacts services
 The source registry and the calendar and address book services with
 their backends (local, CalDAV, CardDAV, Google, LDAP), split out of
 evolution-data-server for desktops that do not use GTK. Passwords go to
 whichever Secret Service the desktop runs.
STANZA
        {
            printf 'evolution-data-server (%s%s) %s; urgency=medium\n\n' "$version" "$(wildos_suffix eds)" "$SUITE"
            printf '  * Wild OS: Camel providers in evolution-data-server-camel-providers,\n    the calendar and contacts services in evolution-data-server-core.\n\n'
            printf ' -- Wild OS Developers <dev@wildos.io>  %s\n\n' "$(date -R)"
            cat debian/changelog
        } > debian/changelog.new
        mv debian/changelog.new debian/changelog
        DEB_BUILD_OPTIONS="nocheck nodoc parallel=$BUILD_THREADS" dpkg-buildpackage -us -uc -b >/dev/null
        fill="$cached.part.$BASHPID"
        rm -rf "$fill" && mkdir -p "$fill"
        cp ../*+wildos[0-9]*_*.deb "$fill/"
        rm -f "$fill"/*dbgsym* "$fill"/*-tests_* "$fill"/*-doc_*
        cd "$WORK" && rm -rf eds-src
        # Another build may have made the same one meanwhile; either copy is good.
        mv -T "$fill" "$cached" 2>/dev/null || rm -rf "$fill"
    fi
    cp "$cached"/*.deb "$WORK/repo/"
)
# Six Kicksecure packages hard-depend on a compiler toolchain (gcc, make,
# binutils, the C library headers, dkms, and equivs with debhelper), about
# 260 MB of the image that nothing on a Wild OS computer uses. They are
# republished without those dependencies, +wildos1 so apt prefers them, like
# plymouth: Kicksecure's own updates to them reach computers when the next
# update build picks them up, and its other packages keep coming from its
# repository. NVIDIA's driver still installs from Settings
# (settings-graphics-driver): dkms brings make and binutils, and the kernel
# headers installed with it bring gcc.
#
# Two of them compiled small programs while they installed. Those are
# compiled here instead, by the package's own build files with its own
# hardening flags, and shipped in the package, which then depends on the
# libraries they link rather than on a compiler.
KS_APT=$WORK/ks-apt
rm -rf "$KS_APT"
mkdir -p "$KS_APT/etc/apt/apt.conf.d" "$KS_APT/etc/apt/preferences.d" "$KS_APT/var/lib/apt/lists/partial" \
    "$KS_APT/var/cache/apt/archives/partial" "$KS_APT/var/lib/dpkg"
: > "$KS_APT/var/lib/dpkg/status"
echo "deb [signed-by=$SRC/build/keys/kicksecure.asc] https://deb.kicksecure.com $SUITE main contrib non-free" \
    > "$KS_APT/etc/apt/sources.list"
cat > "$KS_APT/apt.conf" <<EOF
Dir "$KS_APT/";
Dir::State::status "$KS_APT/var/lib/dpkg/status";
APT::Architecture "amd64";
APT::Sandbox::User "root";
EOF
ks_apt() { APT_CONFIG=$KS_APT/apt.conf "$@"; }
ks_apt apt-get update -qq
REPACK=$WORK/ks-repack
repack_fail() {
    echo "ERROR: Kicksecure's $1 changed shape; update its repack in build.sh" >&2
    exit 1
}
# Unpacks Kicksecure's newest <package> into $REPACK/pkg with the named
# dependencies taken out, each of which has to be there.
repack_open() {
    local name=$1 version control deps dep kept
    shift
    version=$(ks_apt apt-cache policy "$name" | sed -n 's/^  Candidate: //p')
    [ -n "$version" ] && [ "$version" != "(none)" ] || { echo "ERROR: Kicksecure no longer publishes $name; drop its repack" >&2; exit 1; }
    rm -rf "$REPACK" && mkdir -p "$REPACK"
    (cd "$REPACK" && ks_apt apt-get download -qq "$name=$version")
    dpkg-deb -R "$REPACK/${name}_"*.deb "$REPACK/pkg"
    control=$REPACK/pkg/DEBIAN/control
    # The edit below works on a Depends written on one line.
    [ "$(grep -c '^Depends: ' "$control")" = 1 ] || repack_fail "$name"
    ! sed -n '/^Depends: /{n;p}' "$control" | grep -q '^[[:space:]]' || repack_fail "$name"
    deps=$(sed -n 's/^Depends: //p' "$control" | sed 's/, */\n/g')
    for dep in "$@"; do
        grep -qxF "$dep" <<<"$deps" || repack_fail "$name"
    done
    kept=$(grep -vxF -f <(printf '%s\n' "$@") <<<"$deps" | paste -sd, | sed 's/,/, /g')
    # REPACK_REVISION goes up when a repack changes while Kicksecure's version
    # stays the same, so machines take the new one.
    sed -i -e "s/^Depends: .*/Depends: $kept/" -e "s/^Version: \(.*\)\$/Version: \1+wildos${REPACK_REVISION:-1}/" "$control"
}
# Adds programs compiled into $REPACK/pkg to the package: it becomes amd64
# and depends on the libraries they link.
repack_ship() {
    local pkg=$REPACK/pkg control f size linked=() libs=
    control=$pkg/DEBIAN/control
    grep -qx 'Architecture: all' "$control" || repack_fail "$(sed -n 's/^Package: //p' "$control")"
    size=$(sed -n 's/^Installed-Size: //p' "$control")
    for f in "$@"; do
        chmod 0755 "$pkg/$f"
        size=$((size + ($(stat -c %s "$pkg/$f") + 1023) / 1024))
        (cd "$pkg" && md5sum "$f" >> DEBIAN/md5sums)
        # A static program (emerg-shutdown) links nothing to depend on.
        objdump -p "$pkg/$f" | grep -q NEEDED && linked+=("$pkg/$f")
    done
    if [ ${#linked[@]} -gt 0 ]; then
        # dpkg-shlibdeps reads the package name from a debian/control.
        mkdir -p "$REPACK/shlibs/debian"
        printf 'Source: shlibs\n\nPackage: shlibs\nArchitecture: any\n' > "$REPACK/shlibs/debian/control"
        libs=$(cd "$REPACK/shlibs" && dpkg-shlibdeps -O "${linked[@]}" | sed -n 's/^shlibs:Depends=//p')
    fi
    sed -i -e 's/^Architecture: all$/Architecture: amd64/' -e "s/^Installed-Size: .*/Installed-Size: $size/" "$control"
    [ -z "$libs" ] || sed -i "s/^Depends: .*/&, $libs/" "$control"
}
repack_close() {
    dpkg-deb -b "$REPACK/pkg" "$WORK/repo/" >/dev/null
    rm -rf "$REPACK"
}

# security-misc-shared compiled emerg-shutdown (powers off at once when the
# boot drive is pulled out) and fm-shim-backend (its org.freedesktop.FileManager1
# service) in its postinst. emerg-shutdown would also compile at run time,
# but only when its program is missing.
#
# Its preinst also stops an install or upgrade when no account is in the sudo
# or console groups, to keep someone from locking themselves out. Wild OS makes
# and manages the accounts (Settings keeps an administrator in sudo, and every
# account that signs in is in console), so the check can only block updates,
# as it did on a computer still at first-run setup.
REPACK_REVISION=2 repack_open security-misc-shared build-essential libdbus-1-dev libsystemd-dev pkgconf
(
    cd "$REPACK/pkg"
    [ "$(grep -cxE '   (sudo|console)_users_check' DEBIAN/preinst)" = 2 ] || repack_fail security-misc-shared
    sed -i -E 's/^   (sudo|console)_users_check$/   true/' DEBIAN/preinst
    [ "$(grep -cxE '        build_(fm_shim_backend|emerg_shutdown)' DEBIAN/postinst)" = 2 ] || repack_fail security-misc-shared
    sed -i -E '/^        build_(fm_shim_backend|emerg_shutdown)$/d' DEBIAN/postinst
    for program in emerg-shutdown fm-shim-backend; do
        bash "usr/libexec/security-misc/compile-$program" "usr/src/security-misc/$program.c" "usr/bin/$program"
    done
)
repack_ship usr/bin/emerg-shutdown usr/bin/fm-shim-backend
repack_close

# sdwdate, the time sync, built sclockadj (moves the clock gradually) with
# make and gcc in its postinst.
repack_open sdwdate gcc libc6-dev
(
    cd "$REPACK/pkg"
    start='       ## Build sclockadj program.'
    end='       popd >/dev/null'
    [ "$(grep -cxF "$start" DEBIAN/postinst)" = 1 ] && [ "$(grep -cxF "$end" DEBIAN/postinst)" = 1 ] || repack_fail sdwdate
    sed -i "\|^$start\$|,\|^$end\$|d" DEBIAN/postinst
    ! grep -qwE 'make|gcc' DEBIAN/postinst || repack_fail sdwdate
    make -s -C usr/src/sdwdate
    mv usr/src/sdwdate/sclockadj usr/libexec/sdwdate/sclockadj
)
repack_ship usr/libexec/sdwdate/sclockadj
repack_close

# usability-misc depends on dkms so modules (VirtualBox's, for Kicksecure)
# can be built; nothing in it builds one.
repack_open usability-misc dkms
! grep -qE 'dkms|gcc' "$REPACK"/pkg/DEBIAN/p* || repack_fail usability-misc
repack_close

# dist-base-files depends on dpkg-dev (which brings make and binutils) for
# dpkg-parsechangelog, which its postinst runs once to record its version
# and carries on without.
repack_open dist-base-files dpkg-dev
[ "$(grep -c 'dpkg-parsechangelog .*|| true$' "$REPACK/pkg/DEBIAN/postinst")" = 1 ] || repack_fail dist-base-files
! grep -qE 'dpkg-(parsechangelog|architecture|buildflags|vendor|source)' "$REPACK"/pkg/DEBIAN/{preinst,prerm,postrm} 2>/dev/null \
    || repack_fail dist-base-files
repack_close

# helper-scripts and dist-general-cli depend on equivs, which builds
# placeholder packages and brings debhelper, autotools, libtool and gcc. Its
# only user is helper-scripts' dummy-dependency command, which Kicksecure's
# release upgrade runs on Qubes alone; nothing on Wild OS runs it.
for name in helper-scripts dist-general-cli; do
    repack_open "$name" equivs
    ! grep -rlw 'equivs' "$REPACK/pkg" --exclude=control --exclude=md5sums --exclude-dir=doc \
        | grep -vx "$REPACK/pkg/usr/bin/dummy-dependency" | grep -q . || repack_fail "$name"
    repack_close
done
rm -rf "$KS_APT"
(cd "$WORK/repo" && apt-ftparchive packages . > Packages)
ls -1 "$WORK/repo"/*.deb
# What the update repository publishes: the Wild OS packages and the Debian
# and Kicksecure packages rebuilt or repacked with Wild OS changes, without
# debug symbols.
rm -rf "$OUT/debs" && mkdir -p "$OUT/debs"
find "$WORK/repo" -maxdepth 1 \( -name 'wildos-*.deb' -o -name '*+wildos[0-9]*.deb' \) ! -name '*-dbgsym_*' \
    -exec cp {} "$OUT/debs/" \;
if [ "$PACKAGES_ONLY" = 1 ]; then
    rm -rf "$WORK/pkgsrc" "$WORK/repo"
    log "Done: $(ls "$OUT/debs" | wc -l) packages in out/debs"
    exit 0
fi

# security-misc refuses to install unless an account in group sudo already
# exists, to stop admins locking themselves out. The image has no users yet at
# that point, so the build uses Kicksecure's documented skip marker; the cleanup
# hook removes it so later upgrades on real machines keep the safeguard.
#
# The key must sit at the same absolute path outside and inside the chroot,
# because apt resolves signed-by against whichever root it is running in.
install -Dm644 "$SRC/build/keys/kicksecure.asc" "$KICKSECURE_KEY"

# The browsers come from their makers' own repositories. Paths match what the
# installed system uses: brave-keyring owns Brave's key and Brave's package
# writes its own apt source; wildos-base ships Mullvad's source and key.
BRAVE_KEY=/usr/share/keyrings/brave-browser-archive-keyring.gpg
MULLVAD_KEY=/usr/share/keyrings/mullvad-keyring.asc
install -Dm644 "$SRC/build/keys/brave-browser.gpg" "$BRAVE_KEY"
install -Dm644 "$SRC/build/keys/mullvad.asc" "$MULLVAD_KEY"

DEBIAN_COMPONENTS="main contrib non-free non-free-firmware"
PACKAGES=$(grep -vE '^\s*(#|$)' "$SRC/build/packages.list" | tr '\n' ',' | sed 's/,$//')
# The image installs no Recommends, but those of Wild OS's own packages are
# the default apps and the helpers each package was written to use. Taken
# from their control files so a new Recommends reaches the image without a
# second list to keep in step. Windows mode's virtual PC is the exception:
# setting Windows mode up installs it. It stays a Recommends so PCs that had
# it before the split keep it on upgrade, since apt adds a Recommends only
# when it is new to the package.
OWN_RECOMMENDS=$(awk '/^Recommends:/ { on = 1; sub(/^Recommends:/, ""); print; next }
                      on && /^[ \t]/ { print; next } { on = 0 }' "$SRC"/packages/*/debian/control |
    tr ',' '\n' | sed 's/\$[{][^}]*[}]//g; s/[[:space:]]//g; s/(.*//; s/|.*//' | grep -v '^$' |
    grep -vx wildos-winmode-runtime | sort -u | paste -sd,)
PACKAGES="$PACKAGES,$OWN_RECOMMENDS"

REUSE_BLOCKER=""
if [ "$INCREMENTAL" = 1 ]; then
    REUSE_BLOCKER=$(inc_reuse_blocker)
fi
if [ "$INCREMENTAL" = 1 ] && [ -z "$REUSE_BLOCKER" ]; then
    log "Updating the kept root filesystem"
    inc_update_rootfs
else
if [ "$INCREMENTAL" = 1 ]; then
    log "Full rebuild of the root filesystem: $REUSE_BLOCKER"
    rm -f "$INC/stamp"
fi
rm -rf "$ROOTFS" && mkdir -p "$(dirname "$ROOTFS")"
log "Bootstrapping root filesystem"
# adduser goes in straight after the essential packages: security-misc
# creates groups in its preinst, which runs before apt has configured
# adduser unless something else pulled adduser in earlier.
# wildos-base's apt pins go in before anything is chosen, so the image gets
# the same kernel, firmware, Mesa and FreeRDP versions as an updated PC
# rather than trixie's own.
mmdebstrap \
    --mode=root \
    --variant=apt \
    --architectures=amd64 \
    --include="$PACKAGES" \
    --aptopt='APT::Install-Recommends "false"' \
    --skip=download/empty \
    --skip=essential/unlink \
    --setup-hook='mkdir -p "$1/var/cache/apt/archives" "$1/usr/share/keyrings" "$1/var/lib/security-misc" "$1/etc/dpkg/dpkg.cfg.d" "$1/etc/apt/preferences.d"' \
    --setup-hook="copy-in $SRC/packages/wildos-base/files/apt/wildos-freerdp.pref /etc/apt/preferences.d/" \
    --setup-hook="copy-in $SRC/packages/wildos-base/files/apt/wildos-newer.pref /etc/apt/preferences.d/" \
    --setup-hook="copy-in $SRC/build/90-wildos-slim /etc/dpkg/dpkg.cfg.d/" \
    --essential-hook='APT_CONFIG="$MMDEBSTRAP_APT_CONFIG" apt-get install --yes -qq adduser >/dev/null' \
    --setup-hook='touch "$1/var/lib/security-misc/skip_install_check"' \
    --setup-hook="sync-in $CACHE /var/cache/apt/archives/" \
    --setup-hook="copy-in $KICKSECURE_KEY $BRAVE_KEY $MULLVAD_KEY /usr/share/keyrings/" \
    --customize-hook="sync-out /var/cache/apt/archives $CACHE" \
    --customize-hook="bash $SRC/build/run-hooks.sh \"\$1\" $SRC/build/hooks" \
    "$SUITE" "$ROOTFS" \
    "deb http://deb.debian.org/debian $SUITE $DEBIAN_COMPONENTS" \
    "deb http://deb.debian.org/debian $SUITE-updates $DEBIAN_COMPONENTS" \
    "deb http://deb.debian.org/debian $SUITE-backports $DEBIAN_COMPONENTS" \
    "deb http://security.debian.org/debian-security $SUITE-security $DEBIAN_COMPONENTS" \
    "deb [signed-by=$KICKSECURE_KEY] https://deb.kicksecure.com $SUITE main contrib non-free" \
    "deb [signed-by=$BRAVE_KEY arch=amd64] https://brave-browser-apt-release.s3.brave.com/ stable main" \
    "deb [signed-by=$MULLVAD_KEY arch=amd64] https://repository.mullvad.net/deb/stable stable main" \
    "deb [trusted=yes] copy://$WORK/repo ./"
fi

# systemd refuses to switch root into a tree without os-release, which surfaces
# only at boot as an unhelpful emergency shell. Catch it here instead.
if [ ! -s "$ROOTFS/etc/os-release" ]; then
    echo "ERROR: rootfs has no os-release; wildos-base did not install correctly" >&2
    exit 1
fi

# The installer adds exactly one GRUB integration package to the target. If the
# image already has one, the pool's version would conflict with it.
for pkg in grub-pc grub-efi-amd64; do
    if chroot "$ROOTFS" dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "ok installed"; then
        echo "ERROR: $pkg is installed in the image; it must only come from the installer pool" >&2
        exit 1
    fi
done

log "Assembling ISO tree"
KVER=$(basename "$(ls -1d "$ROOTFS"/lib/modules/* | sort -V | tail -1)")
mkdir -p "$WORK/iso/live" "$WORK/iso/LiveOS" "$WORK/iso/boot/grub"
cp "$ROOTFS/boot/vmlinuz-$KVER" "$WORK/iso/live/vmlinuz"
# The live initramfs is generated by a hook outside /boot so the installed
# system never inherits it.
mv "$ROOTFS/live-initrd.img" "$WORK/iso/live/initrd.img"
# GRUB packages for the installer; must sit outside the squashfs or the
# installed system would carry a copy of both.
mv "$ROOTFS/live-pool" "$WORK/iso/pool"
LANGUAGES="$SRC/packages/wildos-live/files/languages"
# One entry per language: picking it shows the main menu again with that
# language passed to the live system.
grep -v '^#' "$LANGUAGES" | awk -F '\t' '{
    printf "    menuentry \"%s\" --class wildos-lang-%s {\n        set wildos_lang=%s\n        export wildos_lang\n        configfile /boot/grub/grub.cfg\n    }\n", $3, $1, $1
}' > "$WORK/grub-languages.cfg"
sed "s/@VOLID@/$VOLID/g; s/@VERSION@/$VERSION/g" "$SRC/build/iso/grub.cfg" \
    | sed -e "/^@LANGUAGES@\$/{r $WORK/grub-languages.cfg" -e 'd}' > "$WORK/iso/boot/grub/grub.cfg"
bash "$SRC/packages/wildos-branding/tools/grub-theme.sh" usb "$WORK/iso/boot/grub/themes/wildos" \
    "$SRC/branding/logo/wild-os-icon.svg" "$VERSION" "$LANGUAGES"

log "Compressing root filesystem"
mksquashfs "$ROOTFS" "$WORK/iso/LiveOS/squashfs.img" \
    -noappend -comp zstd -Xcompression-level "$SQUASH_LEVEL" -b 1M \
    -wildcards -e 'proc/*' 'sys/*' 'dev/*' 'run/*' 'tmp/*'

log "Writing $ISO_NAME"
bash "$SRC/build/iso/make-iso.sh" "$WORK/iso" "$OUT/$ISO_NAME" "$VOLID"
(cd "$OUT" && sha256sum "$ISO_NAME" > "$ISO_NAME.sha256")

[ "$INCREMENTAL" = 1 ] && inc_save

# The rootfs and ISO tree are about 10 GB. A normal build makes them from
# scratch every time and keeps only the apt cache; an incremental one keeps
# its rootfs in $WORK/inc instead.
rm -rf "$WORK/rootfs" "$WORK/iso" "$WORK/pkgsrc" "$WORK/repo"

log "Done: out/$ISO_NAME ($(du -h "$OUT/$ISO_NAME" | cut -f1))"
