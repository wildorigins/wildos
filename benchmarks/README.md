# Benchmarks

The scripts and raw results behind the figures on [wildos.io/measured](https://wildos.io/measured/). Run them yourself and compare.

## What is measured

| Measurement | How |
|---|---|
| Phoronix Test Suite tests | `bench.sh pts`, each test three times, the average and the run-to-run deviation kept |
| OpenSSL AES-256-GCM | `ssl.sh`, `openssl speed` run directly, because the suite's OpenSSL test produced no result on any system |
| Start-up | `bench.sh boot`, seconds from the kernel starting to the desktop shell starting, three starts |
| Disk | `bench.sh fio_run`, four fio jobs with direct I/O |

The Phoronix tests: build-linux-kernel (defconfig), compress-7zip (the suite's build and the system's package), compress-zstd (level 19), openssl, x265 (Bosphorus 1080p), phpbench, pybench, sqlite-speedtest, simdjson (PartialTweets), stockfish and git.

## The test machine

One KVM virtual machine, the same for every system: 4 processor cores, 8 GB of memory, a 40 GB virtio disk, UEFI, on a desktop PC with an Intel Core i9-12900K. One system at a time, with nothing else running on the host. A virtual machine has no real graphics card and no battery, so there are no graphics, gaming or battery figures here.

Each system is installed fresh from its own installer and fully updated, at the installer's defaults apart from disk encryption, which is off on every system, Wild OS included. Sign-in is automatic, so no typing is timed. Wild OS was measured without Windows mode set up.

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
bash /var/tmp/bench.sh boot         # a minute after each restart
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

Things to know before comparing:

- Small random writes are slower on Wild OS than on the ext4 systems. Wild OS uses Btrfs, which checksums every file and makes restore points possible, and its kernel does more work for each small write. This shows in the 4k random write job and in SQLite. Windows mode's disk is stored without checksums and is not affected.
- SQLite varied more between passes than any other test on both systems, so treat a single pass with care.
- Wild OS pauses its secure time sync during the Phoronix pass, because a clock correction in the middle of a test changes its timing.

## Results so far

| System | Version | Tested |
|---|---|---|
| Wild OS | 0.1 Beta 2, build 775, Linux 7.2, Btrfs | 15 November 2026 |
| Ubuntu | 26.04.1 LTS, GNOME, Linux 7.0, ext4 | 15 and 16 November 2026 |
| CachyOS | Desktop (KDE Plasma), August 2026 ISO, Linux 7.3, Btrfs | 15 and 16 November 2026 |
| Linux Mint | 22.3 Cinnamon, Linux 7.0, ext4 | 16 November 2026 |
| Zorin OS | 18.1 Core, Linux 7.0, ext4 | 16 November 2026 |
