#!/bin/bash
# Builds the Wild OS ISO into ./out using the builder container.
# --incremental reuses the last build (see build/incremental.sh).
# WILDOS_CACHE names the volume or directory kept between builds.
# WILDOS_WORK names the scratch volume: one per worktree, so a build never
# wipes another worktree's work.
#
# --packages builds only the packages this checkout changed from main
# (committed or not), never all of them by accident: a full package build
# takes every core for many minutes, and with several sessions at once it froze
# the PC. WILDOS_ONLY="a b" names them instead; WILDOS_ALL=1 builds everything
# (publishing with --full, and CI).
# Patched Debian rebuilds and the compiler cache are shared by every build on
# the PC (wildos-rebuilds, wildos-ccache), so a new worktree starts warm.
# WILDOS_REBUILDS names another volume or directory for the rebuilds (CI's).
# WILDOS_REBUILDS_FROM=https://deb.wildos.io first fills it with the rebuilds
# already published there (tools/fetch-rebuilds.sh), so they aren't compiled;
# --fetch-rebuilds does only that and reports what a build would compile.
# WILDOS_BUILD_MEMORY caps one build (10g by default): a build that needs more
# fails on its own instead of freezing every other build and VM.
#
# Builds on one PC take turns; WILDOS_BUILD_SLOTS lets that many run at once
# (one by default). WILDOS_PUBLISH=1 marks a publish, which has a slot of its
# own and so never waits behind a development build.
set -euo pipefail
cd "$(dirname "$0")"

# A worktree made from Windows records its git directory as a Windows path
# (gitdir: C:/...), which git under WSL cannot follow. Without git the patch
# counts below come out empty and the build misses the rebuild cache and
# compiles LibreOffice and the patched KDE libraries again for an hour.
if ! git rev-parse --git-dir >/dev/null 2>&1 && [ -f .git ] && command -v wslpath >/dev/null; then
    gitdir=$(sed -n 's/^gitdir: //p' .git | tr -d '\r')
    GIT_DIR=$(wslpath -u "$gitdir") GIT_WORK_TREE=$PWD
    export GIT_DIR GIT_WORK_TREE
fi
git rev-parse --git-dir >/dev/null 2>&1 || { echo "git cannot read this checkout, so the build would miss the rebuild cache." >&2; exit 1; }

if [ "${1:-}" = --packages ] && [ -z "${WILDOS_ONLY+set}" ] && [ "${WILDOS_ALL:-}" != 1 ]; then
    base=$(git merge-base HEAD origin/main 2>/dev/null || git merge-base HEAD main)
    # branding/ is copied into wildos-branding when it builds.
    WILDOS_ONLY=$({ git diff --name-only "$base" -- packages branding; git ls-files --others --exclude-standard -- packages branding; } |
        awk -F/ '$1 == "branding" { print "wildos-branding"; next } NF > 2 { print $2 }' | sort -u | tr '\n' ' ')
    WILDOS_ONLY=${WILDOS_ONLY% }
    export WILDOS_ONLY
    if [ -z "$WILDOS_ONLY" ]; then
        echo "No Wild OS package differs from main here, so there is nothing to build."
        echo "Name packages with WILDOS_ONLY=\"wildos-mail ...\", or set WILDOS_ALL=1 for every package."
        exit 0
    fi
    echo "Building only what changed from main: $WILDOS_ONLY"
fi
if [ "${WILDOS_ALL:-}" = 1 ]; then unset WILDOS_ONLY; fi

# One cache for every build on this PC: the Debian packages Wild OS patches
# (Calamares, KDE libraries) take many minutes to compile and are kept there by
# recipe, so a cache per worktree compiled them again for every agent. A path
# (CI's directory) is still honoured.
cache=${WILDOS_CACHE:-wildos-cache}
case "$cache" in
    /*|./*|[A-Za-z]:*) ;;
    *) [ "$cache" = wildos-cache ] || echo "Using the shared wildos-cache volume, not $cache"; cache=wildos-cache ;;
esac

docker build -t wildos-builder build
mkdir -p out

# How many commits changed each set of patches Wild OS applies to a Debian
# package, for the +wildosN version of the rebuild (wildos_suffix in
# build/build.sh). Counted here because the builder has no git.
patch_counts=
for dir in build/patches/*/; do
    n=$(git rev-list --count HEAD -- "$dir") || { echo "Could not count the commits in $dir" >&2; exit 1; }
    patch_counts="$patch_counts $(basename "$dir")=$n"
done

# A volume name passes as it is; a directory is mounted by its full path.
rebuilds=${WILDOS_REBUILDS:-wildos-rebuilds}
case "$rebuilds" in
    /*|./*) mkdir -p "$rebuilds" && rebuilds=$(cd "$rebuilds" && pwd) ;;
esac
if [ "${1:-}" = --fetch-rebuilds ] || [ -n "${WILDOS_REBUILDS_FROM:-}" ]; then
    docker run --rm \
        -e WILDOS_PATCH_COUNTS="$patch_counts" \
        -e WILDOS_REBUILDS_FROM \
        -e WILDOS_REBUILDS_PRUNE \
        -e GITHUB_ACTIONS \
        -v "$PWD":/src:ro \
        -v "$rebuilds":/rebuilds \
        wildos-builder bash /src/tools/fetch-rebuilds.sh
    if [ "${1:-}" = --fetch-rebuilds ]; then exit 0; fi
fi

docker run --rm --privileged \
    --memory "${WILDOS_BUILD_MEMORY:-10g}" --memory-swap "${WILDOS_BUILD_MEMORY:-10g}" \
    -e WILDOS_REVISION="${WILDOS_REVISION:-}" \
    -e WILDOS_PATCH_COUNTS="$patch_counts" \
    -e WILDOS_ONLY \
    -e WILDOS_PUBLISH \
    -e WILDOS_PACKAGES_FROM \
    -e WILDOS_BUILD_SLOTS     -e WILDOS_UPDATES_KEY \
    -e WILDOS_CHECKOUT="$PWD" \
    -v "$PWD":/src:ro \
    -v "$PWD/out":/out \
    -v "${WILDOS_WORK:-wildos-work}":/work \
    -v "$cache":/cache \
    -v "$rebuilds":/rebuilds \
    -v wildos-ccache:/ccache \
    wildos-builder bash /src/build/build.sh "$@"
