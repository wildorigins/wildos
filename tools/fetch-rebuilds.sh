#!/bin/bash
# Fills the rebuild cache with the patched Debian packages Wild OS already
# publishes, so a build takes them instead of compiling them. CI starts with
# an empty cache, and LibreOffice alone compiles for longer than a CI job may
# run. build-iso.sh runs this in the builder when WILDOS_REBUILDS_FROM is set
# (CI), or on its own with --fetch-rebuilds.
#
#   /src       the checkout, read-only
#   /rebuilds  the rebuild cache build.sh reads (build-iso.sh mounts it)
#
# Nothing here lists the rebuilds. Each one is found in build/build.sh by its
# cache line, cached="$REBUILDS/<dir>/$version$(wildos_suffix <name>)-$stamp",
# and the lines of its block down to that one run unchanged, so the cache
# entry filled is exactly the one build.sh then looks for. A rebuild added to
# build.sh in the same shape is fetched without changing this script.
#
# A published copy is used only when it is the same build:
#   - its version is exactly Debian's current version plus the +wildosN
#     build.sh would give it, and
#   - the stamp hashes nothing but build/patches/<name>, the folder whose
#     commits N counts. Published packages come from main, so on main a given
#     N is a given set of patches, and the same version means the same stamp.
#     A rebuild whose stamp covers anything else (evolution-data-server's
#     comes from its recipe in build.sh) can change without its version
#     changing, so it is compiled.
# Anything else is compiled by build.sh as before, and said so loudly.
#
# Every file is checked before it is used: InRelease against the update
# signing key in wildos-base, Packages against the SHA256 in the signed
# release, and each .deb against the SHA256 and size in Packages. A failed
# check stops the build rather than compile around it.
#
# WILDOS_REBUILDS_FROM   the repository (https://deb.wildos.io)
# WILDOS_REBUILDS_PRUNE  1 removes cache entries this checkout doesn't use
#                        (CI's own cache folder only, never the shared one)
set -euo pipefail

SRC=/src
REBUILDS=/rebuilds
REPO=${WILDOS_REBUILDS_FROM:-https://deb.wildos.io}
REPO=${REPO%/}
SUITES="stable beta"
KEYRING=$SRC/packages/wildos-base/files/apt/wildos-updates-keyring.asc

say() { printf '==> %s\n' "$*"; }
die() { echo "fetch-rebuilds: $*" >&2; exit 1; }
# Shown as a warning on the CI run's summary page as well as in the log.
loud() {
    printf '\n!!! %s\n\n' "$*"
    [ -n "${GITHUB_ACTIONS:-}" ] && printf '::warning title=Compiling a rebuild::%s\n' "$*"
    return 0
}
fetch() {
    python3 -I -c 'import sys, urllib.request, shutil
with urllib.request.urlopen(sys.argv[1], timeout=120) as r, open(sys.argv[2], "wb") as f:
    shutil.copyfileobj(r, f, 1 << 20)' "$1" "$2" || die "could not download $1"
}

[ -d "$REBUILDS" ] || die "no rebuild cache mounted at $REBUILDS"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# The signed indexes, into $TMP/index: suite, package, version, source,
# file, SHA256, size, one line each.
gpg --dearmor < "$KEYRING" > "$TMP/keyring.gpg"
: > "$TMP/index"
for suite in $SUITES; do
    fetch "$REPO/dists/$suite/InRelease" "$TMP/$suite.InRelease"
    # Only what gpgv verified is read: the text it writes out, not the file.
    gpgv --quiet --keyring "$TMP/keyring.gpg" --output "$TMP/$suite.Release" "$TMP/$suite.InRelease" 2>"$TMP/gpgv.log" \
        || { cat "$TMP/gpgv.log" >&2; die "$suite's InRelease is not signed by the Wild OS update key"; }
    [ "$(sed -n 's/^Suite: //p' "$TMP/$suite.Release")" = "$suite" ] || die "$suite's InRelease names another suite"
    until=$(sed -n 's/^Valid-Until: //p' "$TMP/$suite.Release")
    [ -n "$until" ] && [ "$(date -u -d "$until" +%s)" -gt "$(date -u +%s)" ] || die "$suite's InRelease has expired ($until)"
    want=$(awk '/^SHA256:/ { s = 1; next } /^[^ ]/ { s = 0 } s && $3 == "main/binary-amd64/Packages" { print $1 " " $2 }' "$TMP/$suite.Release")
    [ -n "$want" ] || die "$suite's InRelease lists no main/binary-amd64/Packages"
    fetch "$REPO/dists/$suite/main/binary-amd64/Packages" "$TMP/$suite.Packages"
    [ "$(sha256sum < "$TMP/$suite.Packages" | cut -d' ' -f1) $(stat -c %s "$TMP/$suite.Packages")" = "$want" ] \
        || die "$suite's Packages does not match its signed InRelease"
    awk -v suite="$suite" '
        function flush() {
            if (p != "") { if (src == "") src = p; print suite "\t" p "\t" v "\t" src "\t" f "\t" h "\t" sz }
            p = v = src = f = h = sz = ""
        }
        /^Package: / { p = $2 } /^Version: / { v = $2 } /^Source: / { src = $2 }
        /^Filename: / { f = $2 } /^SHA256: / { h = $2 } /^Size: / { sz = $2 }
        /^$/ { flush() } END { flush() }' "$TMP/$suite.Packages" >> "$TMP/index"
    say "$suite: signed $(sed -n 's/^Date: //p' "$TMP/$suite.Release"), $(grep -c '^Package: ' "$TMP/$suite.Packages") packages"
done

# Debian's current versions, as build.sh's own apt-cache calls see them.
apt-get update -qq

# Every rebuild's block in build.sh: from the "(" that opens it to its cache
# line, run as build.sh runs it.
BUILD=$SRC/build/build.sh
suffix_fn=$(sed -n '/^wildos_suffix() {$/,/^}$/p' "$BUILD")
# The Debian release the blocks build for, as build.sh sets it.
SUITE=$(sed -n 's/^SUITE=\([a-z]*\)$/\1/p' "$BUILD")
[ -n "$SUITE" ] || die "build.sh no longer sets SUITE"
[ -n "$suffix_fn" ] || die "build.sh no longer defines wildos_suffix"
mapfile -t ends < <(grep -n '^[[:space:]]*cached="\$REBUILDS/' "$BUILD" | cut -d: -f1)
[ ${#ends[@]} -gt 0 ] || die "found no rebuilds in build.sh"

wanted=() compile=() fetched=() kept=()
for end in "${ends[@]}"; do
    start=$(head -n "$end" "$BUILD" | grep -n '^($' | tail -1 | cut -d: -f1)
    block=$(sed -n "$((start + 1)),${end}p" "$BUILD")
    line=$(sed -n "${end}p" "$BUILD")
    dir=$(sed -n 's|.*cached="\$REBUILDS/\([^/]*\)/.*|\1|p' <<<"$line")
    name=$(sed -n 's/.*wildos_suffix \([A-Za-z0-9_.-]*\).*/\1/p' <<<"$line")
    # policy or madison (FreeRDP reads its backports version with madison).
    # No match must not end the script: with pipefail a grep that finds
    # nothing stopped it there, silently.
    anchor=$(grep -oE 'apt-cache (policy|madison) [^ |]*' <<<"$block" | head -1 | cut -d' ' -f3 || true)
    # A rebuild of something Debian doesn't carry (Lotus) has no Debian
    # version to ask apt for, so it names its package instead.
    [ -n "$anchor" ] || anchor=$(sed -n 's/^[[:space:]]*package=\([A-Za-z0-9.+-]*\)$/\1/p' <<<"$block" | head -1)
    stampline=$(grep '^[[:space:]]*stamp=' <<<"$block" | sed 's/^[[:space:]]*//')
    [ -n "$dir" ] && [ -n "$name" ] && [ -n "$anchor" ] || die "build.sh line $end: cannot read this rebuild"
    mkdir -p "$TMP/work"
    out=$(cd "$TMP/work" && SRC=$SRC REBUILDS=$REBUILDS WORK=$TMP/work SUITE=$SUITE bash -c \
        "set -euo pipefail; $suffix_fn
$block
printf '%s\t%s\t%s\n' \"\$cached\" \"\$version\$(wildos_suffix $name)\" \"\$stamp\"") \
        || die "build.sh line $end: the $dir block failed"
    IFS=$'\t' read -r cached full stamp <<<"$out"
    [ -n "$full" ] && [ -n "$stamp" ] && [[ "$cached" == "$REBUILDS/$dir/"* ]] || die "build.sh line $end: odd result: $out"
    wanted+=("$dir/${cached##*/}")

    if [ -d "$cached" ]; then
        kept+=("$dir $full")
        continue
    fi
    # The version number has to stand for the stamp: N counts commits to
    # build/patches/<name>, and the stamp must hash those patches and nothing else.
    expected="stamp=\$(cat \"\$SRC\"/build/patches/$name/*.patch | sha256sum | cut -c1-12)"
    if [ "$stampline" != "$expected" ] || [ ! -d "$SRC/build/patches/$name" ] \
        || [[ " ${WILDOS_PATCH_COUNTS:-} " != *" $name="* ]]; then
        loud "$dir $full will be COMPILED: its stamp is not tied to its version number, so a published copy can't be trusted to match"
        compile+=("$dir")
        continue
    fi
    # The source package, from the published copy of the package build.sh
    # reads Debian's version from; then all of that source's packages at
    # that version, from one suite.
    pick=
    for suite in $SUITES; do
        src=$(awk -F'\t' -v s="$suite" -v p="$anchor" -v v="$full" '$1 == s && $2 == p && $3 == v { print $4; exit }' "$TMP/index")
        [ -n "$src" ] && { pick=$suite; break; }
    done
    if [ -z "$pick" ]; then
        published=$(awk -F'\t' -v p="$anchor" '$2 == p { print $1 " " $3 }' "$TMP/index" | sort -u | paste -sd, | sed 's/,/, /g')
        loud "$dir $full will be COMPILED: the update repository has ${published:-no $anchor} (build.sh wants $full)"
        compile+=("$dir")
        continue
    fi
    part="$cached.fetch"
    rm -rf "$part" && mkdir -p "$part"
    n=0
    while IFS=$'\t' read -r _ pkg ver _ file sha size; do
        deb="$part/${file##*/}"
        fetch "$REPO/$file" "$deb"
        [ "$(sha256sum < "$deb" | cut -d' ' -f1) $(stat -c %s "$deb")" = "$sha $size" ] \
            || die "$file does not match the signed Packages index"
        [ "$(dpkg-deb -f "$deb" Package) $(dpkg-deb -f "$deb" Version)" = "$pkg $ver" ] || die "$file is not $pkg $ver"
        n=$((n + 1))
    done < <(awk -F'\t' -v s="$pick" -v src="$src" -v v="$full" '$1 == s && $4 == src && $3 == v' "$TMP/index")
    mv -T "$part" "$cached"
    fetched+=("$dir $full ($n packages from $pick)")
done

if [ "${WILDOS_REBUILDS_PRUNE:-}" = 1 ]; then
    for entry in "$REBUILDS"/*/*/; do
        [ -d "$entry" ] || continue
        entry=${entry%/}
        rel=${entry#"$REBUILDS"/}
        [[ " ${wanted[*]} " == *" $rel "* ]] || { rm -rf "$entry"; echo "pruned $rel"; }
    done
fi

for x in "${kept[@]}"; do echo "in the cache already: $x"; done
for x in "${fetched[@]}"; do echo "fetched and verified: $x"; done
for x in "${compile[@]}"; do echo "TO COMPILE:           $x"; done
say "${#wanted[@]} rebuilds: ${#kept[@]} cached, ${#fetched[@]} fetched, ${#compile[@]} to compile"
