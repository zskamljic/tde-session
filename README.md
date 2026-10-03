# tde-session

The desktop session of TDE, tofiffe's desktop environment:

- **Atlas** (`tde-atlas`), the Wayland compositor, built on
  [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots);
- **Argus** (`tde-argus`), the overview of every open window, shown with Super, which also
  finds and starts applications;
- **Hermes** (`tde-hermes`), the bar along the top: the applications, the clock, the tray
  icons and the quick settings; it also shows the notifications;
- **Cerberus** (`tde-cerberus`), the lock screen.

The bar shows the running applications the way Windows 7 did, a button each with a dot below
for every window. A click switches to the application's window or, when it is active,
minimizes it; with several windows, clicks go through them. A middle click opens another
window, and a right click lists the windows.

The status icons on the right open the quick settings: the volume, the brightness of a laptop's
screen, the network, with a switch for Wi-Fi, and the battery, and buttons to lock the screen,
log out, restart or shut down. The volume and brightness keys show their level near the bottom
of the screen for a moment.

Notifications show in the top right corner, below the bar, for five seconds, or until they
are dealt with when urgent; the pointer over one keeps it. They sound as they arrive, from the
freedesktop sound theme. A click on one opens what it is about and brings its program's window
forward, and its buttons do what they say. Those that time out wait behind the clock, which shows
a dot meanwhile, until they are dismissed.

The screen locks with Super+L, from the quick settings, after five minutes without input (unless
a program such as a video player asks it to stay on), before the computer sleeps, and when
`loginctl lock-session` asks. The password is checked through PAM, by
`/etc/pam.d/tde-cerberus`, as at the login prompt. Should the lock screen ever quit without
unlocking, the session stays covered and a new one is started.

Hermes is also the polkit agent: when a program asks to do something that needs a password,
such as mounting a disk, it asks for it in a dialog, as GNOME does.

Tray icons are status notifier items, which most programs with one use (through Qt,
libappindicator or Electron). Programs that still use the old X11 tray can be shown with
`xembedsniproxy` from KDE's plasma-workspace.

Windows draw their own title bars, as they do on GNOME; for those that do not, like most X11
programs and Qt programs outside TDE, Atlas draws one, and a margin around them to resize them by. X11 programs run through Xwayland,
started the first time one connects.

Pick **TDE** on the login screen of GDM, LightDM or any display manager that lists Wayland sessions.

## Keys

| Keys | Action |
| --- | --- |
| Super | Argus, the overview of all windows (arrows and Enter pick one, Delete closes it, Escape goes back); typing searches applications |
| Super+A | All applications |
| Super+L | Lock the screen |
| Alt+Tab, Super+Tab | Switch windows |
| Alt+\` | Switch between windows of the same application |
| Super+Up / Super+Down | Maximize / restore |
| Super+Left / Super+Right | Fill the left / right half of the screen |
| Super+H | Minimize |
| Alt+F4, Super+Q | Close |
| Alt+F10 | Maximize or restore |
| Ctrl+Alt+T | Terminal (`$TERMINAL`, else the first one installed) |
| Super+E | Files |
| Volume and brightness keys | Change them, showing the level |
| Ctrl+Alt+Delete | Log out |
| Ctrl+Alt+F1…F12 | Switch to another virtual terminal |

Dragging a window to the top edge maximizes it, to a side edge fills that half of the screen;
Super and a drag moves a window from anywhere in it.
In Argus, a click on a window switches to it and a middle click or the × on it closes it.
Typing finds applications by name, by what they are ("terminal") and by their keywords, and
Enter starts the first one; the button at the bottom shows all of them. Each application starts in
a systemd scope of its own, rather than as part of Argus.

## Building

```sh
cmake -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr
sudo cmake --install build
```

Building needs wlroots 0.20 (with Xwayland), pango, libcanberra, libpulse, PAM, xkbcommon, Qt 6, polkit-qt6, layer-shell-qt, libtde, wayland-protocols and wlr-protocols.
The session runs swaybg for the background, a terminal, and Ariadne for files.

## Changing it

The programs started with the session are listed in `/usr/share/tde/autostart`; a copy in
`~/.config/tde/autostart` takes its place. Programs that start at login the usual way, from
`/etc/xdg/autostart` and `~/.config/autostart`, start too, as systemd services that
`tde-session.target` brings up and takes down with the session; those meant only for other
desktops do not. Keyboard layouts come from `XKB_DEFAULT_LAYOUT` and
friends, or else from the system layout that `localectl` sets.

## Testing in a virtual machine

`tools/vm` sets up an Arch Linux machine under QEMU that logs straight into the session:

```sh
tools/vm create                    # once; downloads Arch and installs GDM and the session
tools/vm install ~/.cache/pacaur/libtde/*.pkg.tar.zst ~/.cache/pacaur/tde-ariadne/*.pkg.tar.zst
tools/vm run                       # the screen follows the window size; --headless for none
tools/vm deploy                    # after a change; Ctrl+Alt+Delete restarts the session
```

The user is `tde` with the password `tde`. The window grabs the keyboard while the pointer is over
it (Ctrl+Alt+G toggles it), so Super and other shortcuts go to the machine instead of the host.
