# The local package repo used during the build does not exist on the user's
# machine; leaving it in sources would make every apt update fail.
sed -i '\|/work/repo|d' /etc/apt/sources.list
if [ -f /etc/apt/sources.list.d/debian.sources ]; then
    # Kicksecure ships its own Debian sources file, so ours would duplicate it.
    sed -i -e '/deb\.debian\.org/d' -e '/security\.debian\.org/d' /etc/apt/sources.list
fi
# The browsers' build-time sources are replaced by their permanent ones,
# brave.sources and mullvad.sources from wildos-base.
sed -i -e '/brave-browser-apt-release/d' -e '/repository\.mullvad\.net/d' /etc/apt/sources.list
sed -i '/^\s*$/d' /etc/apt/sources.list

rm -f /var/lib/security-misc/skip_install_check

# The essential packages are unpacked before dpkg reads 90-wildos-slim, so
# their documentation is removed here by the same rule.
find /usr/share/doc -type f ! -name copyright -delete
find /usr/share/doc -type d -empty -delete

apt-get clean
rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*
find /var/log -type f -exec truncate --size=0 {} +
