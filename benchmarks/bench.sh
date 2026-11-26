#!/bin/bash
# The same script on every system. Run as root inside the guest.
#   bench.sh info | boot | ptsinstall | pts | fio_run
OUT=/var/tmp/bench-out
mkdir -p $OUT
host=$(. /etc/os-release; echo "$ID")

info() {
  {
    echo "== os"; . /etc/os-release; echo "$PRETTY_NAME"
    echo "== kernel"; uname -a
    echo "== cmdline"; cat /proc/cmdline
    echo "== cpu"; nproc; grep -m1 'model name' /proc/cpuinfo
    /lib64/ld-linux-x86-64.so.2 --help 2>/dev/null | grep -E 'x86-64-v[0-9].*supported'
    echo "== governor"; cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo none
    echo "== thp"; cat /sys/kernel/mm/transparent_hugepage/enabled /sys/kernel/mm/transparent_hugepage/defrag
    echo "== sched_ext"; cat /sys/kernel/sched_ext/state 2>/dev/null; cat /sys/kernel/sched_ext/root/ops 2>/dev/null
    echo "== preempt"; cat /sys/kernel/debug/sched/preempt 2>/dev/null || grep -o 'preempt=[a-z]*' /proc/cmdline
    echo "== hz"; grep -E '^CONFIG_HZ=' /boot/config-$(uname -r) 2>/dev/null || zgrep -E '^CONFIG_HZ=' /proc/config.gz 2>/dev/null
    echo "== mitigations"; grep -r . /sys/devices/system/cpu/vulnerabilities/ | sed 's|.*/||'
    echo "== swap/zram"; swapon --show; cat /proc/sys/vm/swappiness
    echo "== fs"; findmnt -no SOURCE,FSTYPE,OPTIONS /
    echo "== disk used"; df -BM --output=used / | tail -1
    btrfs filesystem usage -m / 2>/dev/null | grep -E 'Used:|Device allocated' | head -2
    echo "== packages"
    if command -v pacman >/dev/null; then pacman -Q | wc -l; else dpkg-query -f '.\n' -W | wc -l; fi
    echo "== compilers"; gcc --version | head -1; php --version | head -1; python3 --version
    echo "== io scheduler"; cat /sys/block/vda/queue/scheduler
  } > $OUT/info.txt 2>&1
  cat $OUT/info.txt
}

# Desktop signed in, nothing opened, after 5 minutes settle.

boot() {
  n=$(ls $OUT/boot-*.txt 2>/dev/null | wc -l); n=$((n+1))
  {
    systemd-analyze 2>&1
    echo "== seconds since kernel start (monotonic)"
    j=$(journalctl -b -o short-monotonic --no-pager 2>/dev/null)
    t() { echo "$j" | grep -m1 -E "$1" | sed -E 's/^\[ *([0-9.]+)\].*/\1/'; }
    echo "plasmashell $(t 'Started (plasma-plasmashell.service - )?KDE Plasma Workspace\.')"
    echo "workspace_ready $(t 'Reached target (plasma-workspace.target - )?KDE Plasma Workspace\.')"
    # The splash ends when the desktop is shown. Wild OS's start-up
    # animation holds this back by design.
    echo "splash_end $(t 'plasma-ksplash.service: (Deactivated|Consumed)')"
    # Works on any desktop: when the signed-in user's shell process began,
    # in seconds since the kernel started.
    for p in plasmashell cinnamon gnome-shell; do
      pid=$(pgrep -u 1000 -o -x $p 2>/dev/null) || continue
      echo "shell_start $p $(awk '{printf "%.2f", $22/100}' /proc/$pid/stat)"
    done
    echo "graphical_target $(t 'Reached target (graphical.target - )?Graphical Interface')"
    echo "== critical chain"; systemd-analyze critical-chain 2>&1 | head -25
    echo "== blame"; systemd-analyze blame 2>&1 | head -15
  } > $OUT/boot-$n.txt
  cat $OUT/boot-$n.txt
}

TESTS="pts/build-linux-kernel pts/compress-7zip system/compress-7zip pts/compress-zstd pts/openssl system/openssl pts/x265 pts/phpbench pts/pybench pts/sqlite-speedtest pts/simdjson pts/stockfish pts/git"
# Option values follow the Phoronix CachyOS articles, sized for 4 vCPUs.
PRESET="build-linux-kernel.build=defconfig;compress-zstd.compression=19;openssl.algo=AES-256-GCM;x265.video=Bosphorus 1080p;simdjson.test=PartialTweets"

ptsenv() {
  cd /var/tmp
  [ -d phoronix-test-suite-master ] || tar xzf pts-master.tar.gz
  P=/var/tmp/phoronix-test-suite-master/phoronix-test-suite
  # A VM reports no DIMM sizes; PHP 8.4+ throws on PTS's modulo by zero.
  sed -i 's/if((\$mem_count \* \$t) != phodevi/if($t > 0 \&\& ($mem_count * $t) != phodevi/' \
    /var/tmp/phoronix-test-suite-master/pts-core/objects/phodevi/components/phodevi_memory.php
  export NO_COLOR=1 SKIP_EXTERNAL_DEPENDENCIES=1 PRESET_OPTIONS="$PRESET"
  R=${RESNAME:-bench-$host}; export TEST_RESULTS_NAME=$R TEST_RESULTS_IDENTIFIER=$host
  export TEST_RESULTS_DESCRIPTION="4 vCPU 8 GB KVM guest on i9-12900K, $host"
  # Save results; no browser, no upload, no prompts, preset options only.
  [ -f /etc/phoronix-test-suite.xml ] && grep -q '<Configured>TRUE' /etc/phoronix-test-suite.xml \
    || printf 'y\nn\nn\nn\nn\nn\nn\n' | $P batch-setup > /dev/null 2>&1
}

ptsinstall() {
  ptsenv
  $P batch-install $TESTS > $OUT/pts-install.log 2>&1
  echo "INSTALL_RC=$?" >> $OUT/pts-install.log
}

pts() {
  ptsenv
  # First pass is one run per test; later passes append more runs.
  [ -n "$TIMES" ] && export FORCE_TIMES_TO_RUN=$TIMES
  [ -n "$ONLY" ] && TESTS="$ONLY"
  $P batch-benchmark $TESTS > $OUT/pts.log 2>&1
  echo "PTS_RC=$?" >> $OUT/pts.log
  $P result-file-to-csv $R > $OUT/pts-csv.log 2>&1 || true
  cp -r /var/lib/phoronix-test-suite/test-results/bench-$host $OUT/ 2>/dev/null
  cp ~/bench-$host.csv $OUT/ 2>/dev/null
}

# fio straight, so both run the same jobs on the same root filesystem.
fio_run() {
  d=/var/tmp/fio; mkdir -p $d
  for job in "randread 4k 1" "randwrite 4k 1" "read 2m 0" "write 2m 0"; do
    set -- $job
    fio --name=$1-$2 --directory=$d --rw=$1 --bs=$2 --size=2g --ioengine=libaio \
      --direct=1 --iodepth=32 --numjobs=1 --runtime=30 --time_based --group_reporting \
      --output-format=terse --terse-version=3 2>&1 | awk -F';' -v n="$1-$2" \
      '{printf "%s read_iops=%s read_bw_kb=%s write_iops=%s write_bw_kb=%s\n", n, $8, $7, $49, $48}'
    rm -f $d/*
  done | tee $OUT/fio.txt
}

"$@"
