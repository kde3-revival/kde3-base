# kde3-base

The KDE 3.5 desktop (kdebase 3.5.10), maintained by the
[kde3-revival](https://github.com/kde3-revival) project for today's Linux
systems: the window manager, panel, desktop, file manager and web browser,
terminal, Control Center, session manager and login manager.

It is built on [kde3-libs](https://github.com/kde3-revival/kde3-libs) and
[kde3-qt](https://github.com/kde3-revival/kde3-qt); the applications in
[kde3-apps](https://github.com/kde3-revival/kde3-apps) run on top of it.

The first commit of this repository is the pristine kdebase 3.5.10 release;
everything after it is this project's work, so `git log` and `git blame`
lead back to the original code. How the project works is described on the
[organisation page](https://github.com/kde3-revival).

## Contents

| Directory | What it is |
|---|---|
| `kwin` | The window manager, now also the compositor |
| `kicker` | The panel: K menu, taskbar, pager, system tray, applets |
| `kdesktop` | The desktop: icons, backgrounds, screen locker (`kdesktop_lock`) |
| `konqueror`, `libkonq` | File manager and web browser, and its shared library |
| `konsole` | The terminal emulator |
| `kcontrol` | The Control Center and its modules |
| `ksmserver` | The session manager, logout and shutdown dialog |
| `kdm`, `kdmlib` | The display (login) manager |
| `kate` | The Kate text editor application (the editor part is in kde3-libs) |
| `kioslave` | I/O slaves: media, trash, fish, sftp, smb, man, info, ldap, pop3, smtp, thumbnail, ... |
| `khotkeys`, `kxkb`, `klipper` | Input actions, keyboard layouts, clipboard tool |
| `kmenuedit`, `kfind`, `khelpcenter` | Menu editor, file search, help center |
| `ksysguard` | System monitor and its daemon |
| `kscreensaver`, `ksplashml` | Screen saver framework, splash screen |
| `kcheckpass`, `kdesu`, `kdepasswd` | Password checking, running as another user, password change |
| `drkonqi` | The crash handler |
| `kdialog`, `kstart`, `kreadconfig`, `ksystraycmd`, `kstartupx` | Small tools for scripts and the session start |
| `kdeprint`, `kdeeject`, `kcminit` | Print helpers, eject helper, module initialisation |
| `applnk`, `pics`, `l10n`, `doc` | Menu entries, icons and wallpapers, country data, handbooks |
| `startkde` | The session start script |

## Changes compared with KDE 3.5.10

### Build

- CMake replaces autotools; kdebase finds kdelibs through its CMake
  package (`find_package(KDE3Libs 3.5)`) and installs into the same
  prefix.
- Builds with current GCC, glibc and system libraries (OpenSSL 3, CUPS 2,
  current X.Org).

### KWin

- KWin composites the screen itself: OpenGL ES 3 through EGL/GBM and DRI3
  on a GPU X server, XRender otherwise. Tear-free frames with Present on
  each monitor, soft shadows that follow window shapes, translucency
  (`_NET_WM_WINDOW_OPACITY`), unredirected full screen windows. A
  Compositing page in the window behaviour module replaces kompmgr, which
  is removed.
- Window animations on the GPU: open, close, minimize (genie), maximize.
- A GNOME-like overview of windows and desktops: the Super key alone or a
  hot corner of the primary monitor opens it; windows can be closed there
  or dragged to another monitor; minimized windows can be shown.
- Virtual desktops per monitor: each monitor switches its own desktop;
  kicker's taskbars and pagers follow. `[Desktops] PerMonitor=false` and a
  checkbox in the Multiple Desktops module keep the classic behaviour.
- Applications launched on a monitor open on that monitor's desktop.
- Shortcuts: Print takes a screenshot of an area, Shift+Print of the
  desktop, Alt+Print of the window (through KSnapshot from kde3-apps),
  also while a menu is open; Meta+Up/Down maximize and restore; moving
  windows to the next or a given monitor.
- New defaults: new windows centred, double click on the title bar
  maximizes.

### Monitors and hardware

- Monitors module for XRandR 1.2 (replaces Size & Orientation): arranges
  several monitors, remembers the arrangement per set of connected
  monitors (by EDID) and applies it at login and on hotplug.
- Background per monitor; the main panel can be shown on every monitor.
- Touchpad tab in the Mouse module for the libinput X driver (tap to
  click, scrolling, speed, disable while typing, ...), applied by
  `kded_touchpad`.
- Media manager backend for UDisks2 (the HAL backend is removed).
- Panel applets: NetworkManager (made for Wi-Fi; uses libnm), volume
  control (PulseAudio or PipeWire's PulseAudio server), power (batteries,
  CPU, brightness; UPower).
- System tray: StatusNotifierItem icons over D-Bus besides the XEmbed
  ones, all icons at the tray's size.
- Keyboard keys for volume, sleep, hibernate, lock and launchers work.

### Session

- Sleep and hibernate in the logout dialog and the K menu, power off and
  reboot, through systemd-logind.
- The session is named `KDE3:GNOME` (`XDG_CURRENT_DESKTOP`), so that
  applications that do not know KDE 3 take their GNOME path (for example
  the GNOME keyring for passwords).
- GTK and libadwaita applications follow KDE's fonts, colours and cursor:
  an XSETTINGS manager (`kded_xsettings`), a dconf database for the
  session, and `xdg-desktop-portal-kde3`, a Settings portal backend
  (with `kde3-portals.conf` choosing the other portal backends).
- Font defaults: slight hinting, `Xft.dpi` and `Xft.lcdfilter` exported.

### Applications

- Konsole: bracketed paste, focus reporting, SGR and any-event mouse
  reporting, synchronized output, cursor shape (DECSCUSR), mode and colour
  queries, characters above U+FFFF and Unicode 17 widths, box drawing
  characters drawn by Konsole.
- Konqueror: location bar completion without invented `www.`, favicons
  in PNG and at 32 pixels, better behaviour with parts that fetch their
  URLs themselves.
- Background module: new wallpapers from the KDE Store and other sources,
  a picture of the day.
- kio_man and kio_filter read xz, zstd, lzma and lz4 files.
- Security fixes in kcheckpass, kdm, drkonqi, konsole, ksysguardd and the
  crypto module (which trusts the system's CA store).

### Removed

kompmgr, the HAL media backend, nsplugins and the Java settings, the cgi,
finger, floppy, mac, nfs and nntp I/O slaves, kpersonalizer, ktip,
kappfinder, knetattach, kpager, kdcop, kdebugdialog, kdeprintfax, aRts
support and obsolete Control Center modules, and the tests of the 3.5.10
tarball.

## Requirements

- kde3-libs installed (which brings kde3-qt and the CMake macros), CMake
  3.20 or later, a C and C++ compiler, pkg-config.
- Required: the X11 libraries including libXrender.
- Required by default, each can be switched off with its option:
  GIO (`KDEBASE_LOGIND_SLEEP`, `KDEBASE_UDISKS2`, `KDEBASE_SYSTRAY_SNI`,
  `KDEBASE_SETTINGS_PORTAL`), libpulse (`KDEBASE_VOLUME_APPLET`),
  libupower-glib (`KDEBASE_POWER_APPLET`), libnm 1.24
  (`KDEBASE_NETWORK_APPLET`), bdftopcf (`KDEBASE_KONSOLE_FONTS`).
- Optional, used when found: Xcomposite, Xdamage and Xfixes (compositing),
  Xpresent, EGL, GLESv2, GBM, libdrm and xcb-dri3 (GPU compositing), Xi
  1.7 and Xrandr 1.5 (overview's Super key and hot corner), XInput 2
  (Touchpad tab), Xinerama, XScreenSaver, DPMS, OpenGL, PAM, OpenSSL,
  libxkbfile (kxkb), libXau and libXdmcp (kdm), FreeType and fontconfig (kfontinst),
  cyrus-sasl, OpenLDAP, libsmbclient, OpenEXR, lm_sensors, libdns_sd,
  libusb.

At the end of configuring, CMake lists the features that are switched off
or were not found.

## Building and installing

Build and install kde3-qt and kde3-libs first, then:

    cmake -S . -B build
    cmake --build build -j"$(nproc)"
    sudo cmake --install build

kdebase installs into the prefix of the kdelibs it found, by default
`/opt/kde3`. If kdelibs is not in a standard place, pass
`-DCMAKE_PREFIX_PATH=/opt/kde3`.

Useful options:

| Option | Default | Effect |
|---|---|---|
| `KDE3_PAM` | empty | PAM: empty uses it if found, `OFF` disables it, `ON` or a service name requires it (default service `kde`) |
| `KDE3_KDM_PAM_SERVICE`, `KDE3_KCHECKPASS_PAM_SERVICE`, `KDE3_KSCREENSAVER_PAM_SERVICE` | empty | Per-program PAM service |
| `KDE3_WITH_COMPOSITE` | ON | Xcomposite support (KWin compositing) |
| `KDEBASE_*` | ON | `LOGIND_SLEEP`, `UDISKS2`, `NETWORK_APPLET`, `VOLUME_APPLET`, `POWER_APPLET`, `SYSTRAY_SNI`, `SETTINGS_PORTAL`, `KONSOLE_FONTS` (see Requirements) |
| `KDE3_WITH_SAMBA`, `KDE3_WITH_OPENEXR`, `KDE3_WITH_SENSORS`, `KDE3_WITH_LIBUSB` | check | `check` uses the library if found, `ON` requires it |
| `KDE3_WITH_LDAP`, `KDE3_WITH_SSL`, `KDE3_WITH_GL`, `KDE3_WITH_DPMS`, `KDE3_WITH_XINERAMA`, `KDE3_WITH_XSCREENSAVER`, `KDE3_WITH_XDMCP`, `KDE3_WITH_DNSSD` | ON | The corresponding feature |
| `KDE3_KDESU_SUDO_BACKEND` | OFF | kdesu uses sudo instead of su |

`kcheckpass` is installed setuid root and `kdesud` setgid; this only
works when installing as root. The PAM services of kdm are created with
`mkpamserv` only when installing without `DESTDIR`. Packages do both
themselves. See `README.pam` for PAM (its configure options are now the
`KDE3_PAM*` CMake options above).

To start the desktop, choose "KDE 3.5" in the display manager (the
packages install a session file) or run `/opt/kde3/bin/startkde` from
`~/.xinitrc`.

## Packages

[kde3-packaging](https://github.com/kde3-revival/kde3-packaging) builds
this repository for Fedora (`kde3-kdebase`, `kde3-kdebase-devel`, and
`kde3-kdm` for the login manager) and Arch Linux (`kde3-kdebase`). The
packages use the PAM service `kde3`, since `kde` belongs to KDE Plasma,
and add the X session file and a `kde3-kdm.service` unit (not enabled).

## Status

kde3-base is used every day as a desktop on X11, with several monitors,
on current Fedora and Arch Linux. Known limitations:

- X11 only; there is no Wayland session.
- Sleep, power off, the media manager and the network, volume and power
  applets rely on systemd-logind, UDisks2, NetworkManager, PulseAudio
  (or PipeWire) and UPower.
- The network applet connects to open and WPA/WPA2/WPA3-Personal
  networks; WEP and 802.1X (Enterprise) networks are handed to
  `nm-connection-editor`.
- GPU compositing needs an X server with DRI3 and Present; otherwise KWin
  composites with XRender.
- The BSD code of KDE 3.5.10 is kept but not tested.

## License

kdebase is under the GNU General Public License (GPL), see `COPYING`;
some libraries in it under the GNU Library General Public License
(LGPL); the documentation under the GNU Free Documentation License, see
`COPYING-DOCS`. When in doubt, check the header of the individual file.
Contributions are accepted under the same terms.

## Related repositories

- [kde3-qt](https://github.com/kde3-revival/kde3-qt): Qt 3.3.8b, the base
  of everything
- [kde3-libs](https://github.com/kde3-revival/kde3-libs): the KDE core
  libraries
- [kde3-apps](https://github.com/kde3-revival/kde3-apps): applications
  (KSnapshot, Ark, KCalc, KPDF, ...)
- [kde3-remote](https://github.com/kde3-revival/kde3-remote): remote
  desktop for KWin (in progress)
- [kde3-packaging](https://github.com/kde3-revival/kde3-packaging): Fedora
  and Arch Linux packages
