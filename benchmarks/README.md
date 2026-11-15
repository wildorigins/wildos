# Benchmarks

The scripts and raw results behind the figures on [wildos.io/measured](https://wildos.io/measured/). Run them yourself and compare.

## What is measured

| Measurement | How |
|---|---|
| Phoronix Test Suite tests | `bench.sh pts`, each test three times, the average and the run-to-run deviation kept |
| OpenSSL AES-256-GCM | `ssl.sh`, `openssl speed` run directly, because the suite's OpenSSL test produced no result on any system |
| Start-up | `bench.sh boot`, seconds from the kernel starting to the desktop shell starting, three starts |
| Idle footprint | `bench.sh idle`, memory and processor use five minutes after sign-in with nothing open, sampled for two minutes |
| Disk | `bench.sh fio_run`, four fio jobs with direct I/O |

The Phoronix tests: build-linux-kernel (defconfig), compress-7zip (the suite's build and the system's package), compress-zstd (level 19), openssl, x265 (Bosphorus 1080p), phpbench, pybench, sqlite-speedtest, simdjson (PartialTweets), stockfish and git.

## The test machine

One KVM virtual machine, the same for every system: 4 processor cores, 8 GB of memory, a 40 GB virtio disk, UEFI, on a desktop PC with an Intel Core i9-12900K. One system at a time, with nothing else running on the host. A virtual machine has no real graphics card and no battery, so there are no graphics, gaming or battery figures here.

Each system is installed fresh from its own installer and fully updated. Wild OS and CachyOS were both measured on an encrypted Btrfs disk, unlocked by a key file so that no typing is timed, with automatic sign-in. Wild OS was measured without Windows mode set up.

## Running it

Inside the system under test, as root:

```sh
# The test suite and what its source-built tests need (Debian family shown)
apt-get install php-cli php-xml php-zip build-essential cmake nasm yasm autoconf automake \
  libtool pkg-config bc bison flex libssl-dev libelf-dev python3 python3-dev unzip 7zip zstd \
  openssl git sqlite3 fio tcl libaio-dev libnuma-dev curl wget
curl -L https://github.com/phoronix-test-suite/phoronix-test-suite/archive/refs/heads/master.tar.gz \
  -o /var/tmp/pts-master.tar.gz
cp bench.sh full3.sh ptsres.sh ssl.sh /var/tmp/

bash /var/tmp/bench.sh info         # what the system is
bash /var/tmp/bench.sh ptsinstall   # downloads and compiles the tests
bash /var/tmp/full3.sh              # the suite three times, then OpenSSL and the disk jobs
bash /var/tmp/bench.sh boot         # after each restart
bash /var/tmp/bench.sh idle         # five minutes after signing in
```

Output goes to `/var/tmp/bench-out`. `full3.sh` prints the summary that is saved here as `final.txt`.

## Reading the results

`results/<system>/`:

| File | Contents |
|---|---|
| `info.txt` | Kernel, command line, processor, filesystem, package count, compilers |
| `final.txt` | Every test's average, the deviations, OpenSSL and the disk jobs |
| `pts-final.log` | The Phoronix Test Suite's own log of the three-run pass |
| `boot-1.txt` to `boot-3.txt` | Three starts: `systemd-analyze`, when the desktop shell started, the slowest units |
| `idle.txt` | Processor use, memory, process count and the fifteen largest processes |

Things to know before comparing:

- The 4k random write job in the disk results is low on Wild OS (about 10,000 a second against about 43,000 on CachyOS). Wild OS's kernel sends direct writes to checksummed Btrfs files through the page cache, a deliberate upstream change that keeps a file's checksum matching its data. Ordinary programs do not write this way. Windows mode's disk is stored without checksums and is not affected.
- SQLite varied more between passes than any other test on both systems, so treat a single pass with care.
- Wild OS's idle memory includes the lock screen, which had started by the time of the sample.
- Wild OS pauses its secure time sync during the Phoronix pass, because a clock correction in the middle of a test changes its timing.

## Results so far

| System | Version | Tested |
|---|---|---|
| Wild OS | 0.1 Beta 2, build 612, Linux 7.2 | 15 November 2026 |
| CachyOS | Desktop (KDE Plasma), updated 14 November 2026, Linux 7.3 | 15 November 2026 |
