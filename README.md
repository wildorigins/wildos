<div align="center">

<a href="https://wildos.io">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/banner-dark.webp">
    <source media="(prefers-color-scheme: light)" srcset="docs/assets/banner-light.webp">
    <img alt="Wild OS. Leave Windows. Keep your apps." src="docs/assets/banner-dark.webp" width="100%">
  </picture>
</a>

### A free, private Linux desktop laid out like Windows, with real Windows apps when you need them.

**[Website](https://wildos.io)** · **[Take the tour](https://wildos.io/tour/)** · **[How it works](https://wildos.io/how-it-works/)** · **[FAQ](https://wildos.io/faq/)** · **[Download](https://wildos.io/faq/)**

[![Release](https://img.shields.io/github/v/release/wildorigins/wildos?include_prereleases&label=release&style=flat-square)](https://github.com/wildorigins/wildos/releases)
[![Licence](https://img.shields.io/badge/licence-GPL--3.0--or--later-3a56c4?style=flat-square)](LICENSE)
[![Debian 13](https://img.shields.io/badge/base-Debian%2013-a80030?style=flat-square)](https://www.debian.org)
[![KDE Plasma](https://img.shields.io/badge/engine-KDE%20Plasma%206-5a72c8?style=flat-square)](https://kde.org/plasma-desktop/)

</div>

> [!IMPORTANT]
> Wild OS is young and moving fast. [Download the latest release](https://wildos.io/faq/), and back up anything that matters before you install.

## Two ways to use it

Most people need the first. The second is there for the day you can't avoid a Windows program.

| | Use it as it comes | Add Windows mode |
| --- | --- | --- |
| **Who it's for** | You browse, write, email and watch things | You need Microsoft 365, Adobe or a program that only exists for Windows |
| **What you get** | Sixteen apps already installed, and more in the Store | Real Windows programs in normal windows, next to everything else |
| **What it costs** | Nothing | Nothing from us. You need your own Windows 11 Pro licence |
| **The catch** | Files from Word and Excel open in Write and Tables, and complex layouts can shift | It wants 16 GB of memory to feel comfortable, and games with kernel anti-cheat won't run |

## Why Wild OS

<img src="docs/assets/features/windows-apps.webp" alt="Real Windows apps, set up for you" width="100%">

<img src="docs/assets/features/our-apps.webp" alt="Sixteen apps, already installed: Write, Tables, Mail, PDF, Capture, Notes, Remote, Store, Music, Videos, Voice, Backup, Archive, Calculator, Terminal and Settings" width="100%">

<table>
  <tr>
    <td width="50%"><img src="docs/assets/features/free.webp" alt="No licence, no account, no catch" width="100%"></td>
    <td width="50%"><img src="docs/assets/features/privacy.webp" alt="What Wild OS sends about you: nothing. No usage data, no advertising ID, no account" width="100%"></td>
  </tr>
  <tr>
    <td><img src="docs/assets/features/security.webp" alt="Locked down before you touch it: disk encrypted at install, firewall closed, USB protected, signed updates" width="100%"></td>
    <td><img src="docs/assets/features/light.webp" alt="Windows only runs while you need it" width="100%"></td>
  </tr>
  <tr>
    <td><img src="docs/assets/features/desktop.webp" alt="Nothing to relearn: Start, the taskbar, Snap and your shortcuts" width="100%"></td>
    <td><img src="docs/assets/features/remote.webp" alt="Remote Desktop and SSH in one app" width="100%"></td>
  </tr>
  <tr>
    <td><img src="docs/assets/features/recovery.webp" alt="A bad update is easy to undo" width="100%"></td>
    <td><img src="docs/assets/features/backup.webp" alt="Lose the laptop, keep the files: daily encrypted backups" width="100%"></td>
  </tr>
  <tr>
    <td><img src="docs/assets/features/gaming.webp" alt="Your Steam library comes with you" width="100%"></td>
    <td><img src="docs/assets/features/themes.webp" alt="Six themes, one click" width="100%"></td>
  </tr>
</table>

## How it compares

Checked in November 2026. If something here is out of date, [open an issue](https://github.com/wildorigins/wildos/issues) and we'll fix it.

| | Wild OS | Zorin OS 18 | CachyOS | Winux |
| --- | --- | --- | --- | --- |
| **Built on** | Debian 13 | Ubuntu 24.04 | Arch, rolling | Ubuntu 24.04 |
| **Windows programs** | Real Windows 11 in a hidden virtual machine, set up for you | Wine | WinBoat, offered in the Welcome app | WinBoat, preinstalled |
| **Price** | Free | Free, or Pro at $47.99 | Free | Free, or PowerTools at $19.99 |
| **Made for** | People leaving Windows | People leaving Windows or macOS | Performance and gaming | People who want the Windows look |

## How it's built

Wild OS is a free, open source operating system in three layers, and each one has a single job.

| Layer | What it does |
| --- | --- |
| **Hardened foundation** | Debian 13 with an established security hardening layer: kernel and sysctl hardening, AppArmor, USB device control and RAM wiped on shutdown. Full-disk encryption is on by default at install, and updates are signed. |
| **The Wild desktop** | KDE's proven engine underneath, with every screen you touch redesigned: Start, the taskbar, Settings, Files, the Store, the installer, first start and the system dialogs. Not a theme, a rebuilt interface. |
| **Windows mode** | A genuine copy of Windows 11 Pro running out of sight. Each Windows program opens as its own normal window on your desktop. |

On top sit our own apps: Write, Tables, Mail and Calendar, PDF, Capture, Notes, Remote, Music, Videos, Voice, Backup and Archive. The full story is at **[wildos.io](https://wildos.io)**.

## See it

Real screens from the Wild OS designs every screen is built to.

<table>
  <tr>
    <td width="50%"><img src="docs/assets/screens/main.webp" alt="The Wild OS desktop with Files open and a Windows app alongside"><br><sub><b>The desktop.</b> A Windows app running next to Files.</sub></td>
    <td width="50%"><img src="docs/assets/screens/mail.webp" alt="Mail with favourites, the inbox and an email open"><br><sub><b>Mail.</b> Gmail, iCloud and other IMAP accounts in one inbox, trackers blocked.</sub></td>
  </tr>
  <tr>
    <td><img src="docs/assets/screens/write.webp" alt="Write editing a report"><br><sub><b>Write.</b> Opens and saves Word files.</sub></td>
    <td><img src="docs/assets/screens/tables.webp" alt="Tables editing a budget"><br><sub><b>Tables.</b> Opens and saves Excel files.</sub></td>
  </tr>
  <tr>
    <td><img src="docs/assets/screens/remote-terminal.webp" alt="Remote with server tabs, suggestions and snippets"><br><sub><b>Remote.</b> Tabs, suggestions from your history and saved snippets.</sub></td>
    <td><img src="docs/assets/screens/start.webp" alt="The Start menu"><br><sub><b>Start.</b> Pinned apps, recent files and search.</sub></td>
  </tr>
  <tr>
    <td><img src="docs/assets/screens/backup.webp" alt="Backup showing files backed up to a drive"><br><sub><b>Backup.</b> Daily, encrypted, to a drive you plug in.</sub></td>
    <td><img src="docs/assets/screens/recovery.webp" alt="The recovery screen offering the earlier version"><br><sub><b>Recovery.</b> If an update goes wrong, start the version that worked.</sub></td>
  </tr>
</table>

<details>
<summary><b>More screens</b></summary>
<br>

| | |
| --- | --- |
| ![Remote's host list](docs/assets/screens/remote-hosts.webp) | ![Remote copying files to a server](docs/assets/screens/remote-files.webp) |
| ![Remote's full-screen remote desktop](docs/assets/screens/remote-session.webp) | ![Remote's keychain](docs/assets/screens/remote-keychain.webp) |
| ![Calendar week view](docs/assets/screens/calendar.webp) | ![The Store](docs/assets/screens/store.webp) |
| ![Themes](docs/assets/screens/themes.webp) | ![The Overclock theme with RGB lighting](docs/assets/screens/overclock.webp) |
| ![Installing a Windows program](docs/assets/screens/winapp.webp) | ![Windows mode status and controls](docs/assets/screens/winmodepanel.webp) |
| ![Light mode desktop](docs/assets/screens/mainlight.webp) | ![Settings](docs/assets/screens/settings.webp) |
| ![Snap layouts](docs/assets/screens/snap.webp) | ![Alt+Tab](docs/assets/screens/alttab.webp) |
| ![The installer's encryption step](docs/assets/screens/installerencryption.webp) | ![A permission prompt](docs/assets/screens/dialogpermission.webp) |
| ![Shutting down with unsaved work](docs/assets/screens/winmodeshutdown.webp) | ![Task Manager](docs/assets/screens/taskmanagerperf.webp) |
| ![First start welcome](docs/assets/screens/welcome.webp) | ![The lock screen](docs/assets/screens/lock.webp) |
| ![PDF](docs/assets/screens/pdf.webp) | ![Capture](docs/assets/screens/capture.webp) |
| ![Notes](docs/assets/screens/notes.webp) | ![Music](docs/assets/screens/music.webp) |
| ![Display settings](docs/assets/screens/display.webp) | ![Reviewing a problem report before sending it](docs/assets/screens/report.webp) |
| ![The Windows mode check before setup](docs/assets/screens/winmodecheck.webp) | ![Windows mode setup](docs/assets/screens/winmodesetup.webp) |
| ![Loading screens for the four apps](docs/assets/screens/splashes.webp) | ![Windows programs found at first start](docs/assets/screens/firstwinapps.webp) |

Or click through every screen yourself in the **[interactive tour](https://wildos.io/tour/)**.

</details>

## The little things

The details you'd miss if they weren't there, borrowed from the best of Windows and macOS.

- **Taskbar previews.** Hover a taskbar app to see its windows, and hover a preview to peek at it.
- **Snap assist.** Snap one window and Wild OS offers the rest to fill the other side.
- **Quick Look.** Press Space on a file in Files to preview it without opening anything.
- **End task from the taskbar.** Right-click a frozen app to close it for good, no Task Manager needed.
- **Switch between windows of one app** with Super and the key above Tab.
- **Title bar double-click** does what you choose: maximise, minimise or nothing.
- **A clock your way.** Seconds, the long date and battery percentage, each one switch away.
- **Keep awake.** One tile in Quick settings stops the PC sleeping during a download or a talk.
- **Lid and power button** do what you set, on battery and plugged in.
- **Do not disturb on a schedule**, so evenings and weekends stay quiet.

## Windows mode

Windows mode doesn't imitate Windows. It runs a real, licensed copy of Windows 11 Pro in a hidden virtual machine and shows each program as its own window, with its own taskbar icon, in Alt+Tab and snapping like everything else. There is no Wine: one engine is simpler and more reliable than a mixed bag.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/assets/windows-mode-flow-dark.svg">
  <source media="(prefers-color-scheme: light)" srcset="docs/assets/windows-mode-flow-light.svg">
  <img alt="How Wild OS handles a Windows program: wild-run sends most programs to Windows mode, which shows them as normal windows on your desktop; programs with a Linux version, like Zoom, Slack and Steam, install that from the Store; games with kernel anti-cheat aren't supported, and Wild OS says so." src="docs/assets/windows-mode-flow-light.svg" width="100%">
</picture>

- **Light when you're not using it.** A minute after you close your last Windows app it pauses, using no processor time. On battery or short of memory it saves itself to disk instead, and you can save it from the taskbar any time.
- **In your control.** Click the Windows mode status in the taskbar to pause, save to disk, restart or shut it down. The same controls are in Settings.
- **Never lose work.** Shut down with an unsaved document and Windows is kept exactly as it was, then reopened next time.
- **Your files stay yours.** Windows only sees the folders you share. Saving in Word puts the file straight into your Documents folder.
- **Sealed off.** Its own private network, with no view of your home network.

### What you need

> [!NOTE]
> Windows mode needs a **Windows 11 Pro** licence. Most PCs sold with Pro have the key stored in the firmware, and Wild OS finds and uses it. **Windows 11 Home isn't supported yet**, because the feature that shows programs as separate windows is only in Pro. We're working on our own way to support Home.

Everything else in Wild OS works on any PC, with or without a Windows licence.

### Compatibility

| Software | Works | Notes |
| --- | :---: | --- |
| Microsoft 365 and Office | ✅ | Word, Excel, PowerPoint, Outlook, OneNote |
| Adobe Creative Cloud | ✅ | Photoshop, Illustrator, Acrobat. Heavy 3D work is slower |
| Business and accounts software | ✅ | Accounts packages, line-of-business and industry tools |
| Everyday Windows programs | ✅ | Anything that installs from an .exe or .msi |
| Video calls | ✅ | Zoom and Slack install their Linux versions, and Teams runs as its web app |
| Windows games | ✅ | Run through Steam's Proton without Windows mode |
| Games with kernel anti-cheat | ❌ | Valorant, Fortnite and a few others refuse Linux and virtual machines |
| Windows-only hardware drivers | ❌ | Some dongles and scanners. Device passthrough is planned |

Tested something? [Open an issue](https://github.com/wildorigins/wildos/issues) so others know.

## System requirements

| | Minimum | Recommended |
| --- | --- | --- |
| Processor | 64-bit Intel or AMD | Any recent Intel Core or AMD Ryzen |
| Memory | 4 GB | 16 GB, especially for Windows mode |
| Storage | 40 GB | 128 GB SSD or more |
| Firmware | UEFI or legacy BIOS | UEFI with Secure Boot |
| Windows mode | Windows 11 Pro licence, virtualisation on | A PC sold with Windows 11 Pro |

## Try it

> [!WARNING]
> Alpha builds are for testing. Try them from the USB stick or in a virtual machine before installing over anything you care about.

1. **Download** the ISO from [wildos.io/download/iso](https://wildos.io/download/iso) and its checksums from [wildos.io/download/checksums](https://wildos.io/download/checksums). The checksums are signed with the Wild OS key in [packages/wildos-base/files/apt/wildos-updates-keyring.asc](packages/wildos-base/files/apt/wildos-updates-keyring.asc).
2. **Verify** the download.
   ```sh
   sha256sum -c SHA256SUMS
   ```
3. **Write it to a USB stick** (8 GB or larger) with [balenaEtcher](https://etcher.balena.io), [Rufus](https://rufus.ie) on Windows, or from Linux:
   ```sh
   sudo dd if=wildos-amd64.iso of=/dev/sdX bs=4M status=progress oflag=sync
   ```
4. **Start your PC from the stick.** It boots on UEFI, with Secure Boot on, and on legacy BIOS. Choose **Try Wild OS** to look around without touching your disk, or **Install Wild OS** to begin.

The installer can put Wild OS alongside Windows, so you can choose which to start each time.

## Building from source

You need Docker. On Windows that's Docker Desktop with the WSL 2 backend.

```sh
git clone https://github.com/wildorigins/wildos.git
cd wildos
./build-iso.sh        # Linux or macOS
.\build-iso.ps1       # Windows PowerShell
```

The ISO is written to `out/`. The first build downloads about 3 GB of packages; later builds reuse a cache. Released ISOs and updates are built from this source, so anyone can check what went into them.

## Repository layout

| Path | What's in it |
| --- | --- |
| `build/` | Image build: builder container, package list, chroot hooks, boot menu, patches to upstream packages |
| `packages/` | Wild OS's own Debian packages: base system, desktop and shell, Settings, Files, Store, Mail and Calendar (`wildos-mail`), Write and Tables (`wildos-office`), Remote (`wildos-remote`), PDF (`wildos-pdf`), Capture (`wildos-capture`), Notes (`wildos-notes`), Music (`wildos-music`), Videos (`wildos-videos`), Archive (`wildos-archive`), Voice (`wildos-voice`), Backup (`wildos-backup`), Report a problem (`wildos-report`), Terminal, Task Manager, lighting, welcome, installer and Windows mode (`wildos-winmode`) |
| `branding/` | Logo, glyphs and wallpaper |
| `docs/assets/` | The images on this page |

## Roadmap

- [x] Bootable live ISO and installer, with Secure Boot and encryption by default
- [x] The Wild desktop: Start, taskbar, Settings, Files, Store, welcome, light and dark
- [x] Signed update channel with stable and beta
- [x] Windows mode for PCs with Windows 11 Pro, with pause, save to disk, restart and shut down
- [x] Our own apps: Mail and Calendar, Write, Tables and Remote
- [x] More of our own apps: PDF, Capture, Notes, Music, Videos, Voice, Archive and Backup
- [x] Restore points before updates, and a recovery screen that offers the last working version
- [x] Problem reports you read before you send
- [x] Themes and RGB lighting
- [x] Taskbar previews, snap assist, Quick Look and the rest of the little things
- [x] Windows started ahead of time after you sign in, so the first Windows program opens quickly
- [ ] **Now:** Testing and fixes on real hardware
- [ ] **Later:** Windows 11 Home support, device passthrough, Strict and Maximum protection levels

## Contributing

Wild OS is built in the open and help is welcome, from code to testing Windows apps to fixing a typo.

- Ask questions, suggest ideas and show off your setup in [Discussions](https://github.com/wildorigins/wildos/discussions).
- Report bugs and compatibility results through [Issues](https://github.com/wildorigins/wildos/issues).
- Found a security problem? Please don't open a public issue. [Report it privately](https://github.com/wildorigins/wildos/security/advisories/new) instead.

## Credits

Wild OS stands on the work of [Kicksecure](https://www.kicksecure.com) and the [Whonix](https://www.whonix.org) project, whose security packages it installs unmodified from their repository. Wild OS is an independent project and is not affiliated with or endorsed by them; Kicksecure is a trademark of its owners. It is built on [Debian](https://www.debian.org), [KDE](https://kde.org), [Calamares](https://calamares.io), [LibreOffice](https://www.libreoffice.org), [QEMU and KVM](https://www.qemu.org), [libvirt](https://libvirt.org) and [FreeRDP](https://www.freerdp.com). Thank you.

The default wallpaper is a photograph by Quang Nguyen Vinh. Artwork credits and licences are in [branding/CREDITS.md](branding/CREDITS.md).

## Licence

Wild OS's own code is licensed under the [GNU General Public License v3.0 or later](LICENSE). Packages from Debian, Kicksecure and other upstreams keep their own licences.

<sub>A Wild Origins project. Wild OS is not affiliated with Microsoft. Windows mode needs your own Windows licence, and Windows itself isn't included in Wild OS. Windows and Microsoft 365 are trademarks of Microsoft Corporation.</sub>
