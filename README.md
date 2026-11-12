# tde-session

The desktop session of TDE, tofiffe's desktop environment:

- **Atlas** (`tde-atlas`), the Wayland compositor, built on
  [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots);
- **Argus** (`tde-argus`), the overview of every open window, shown with Super, which also
  finds and starts applications; the window switchers of Alt+Tab and Super+Tab; screenshots;
  and the desktop's background;
- **Hermes** (`tde-hermes`), the bar along the top: the applications, the clock, the tray
  icons and the quick settings; it also shows the notifications;
- **Cerberus** (`tde-cerberus`), the lock screen, and **Charon** (`tde-charon`), the login
  screen, for greetd;
- **tde-askpass**, which asks for the passphrases of SSH keys and for passwords for ssh, git
  and `sudo -A`;
- **Daedalus** (`tde-daedalus`), the settings: the network, Bluetooth, the theme and the
  background, displays, sound, power, windows, the keyboard, the mouse and touchpad, the lock
  screen and fingerprints, default apps, and the clock.

The bar shows the running applications the way Windows 7 did, a button each with a dot below
for every window. A click switches to the application's window or, when it is active,
minimizes it; with several windows, clicks go through them. A middle click opens another
window, and a right click lists the windows. Minimized windows stay out of the overview and come
back from their button in the bar, unless Settings shows them in the overview too.

The status icons on the right, with the battery's charge and whether it charges, open the
quick settings, much as GNOME's: at the top the battery and buttons to open the settings, lock
the screen, and a power menu to suspend, restart, shut down or log out; the volume and the
brightness of a laptop's screen; and buttons for Wi-Fi, Bluetooth, the dark style, do not
disturb, the power mode and a screenshot. Wi-Fi and Bluetooth switch on and off with a click,
and the arrow beside each opens a menu below it, with the networks in range to connect to or
the devices paired to connect or let go, and their settings. A network never used asks for its
password; one that does not take it asks again. The volume and
brightness keys show their level near the bottom of the screen for a moment. Bluetooth
headphones left in their headset mode after a call go back to music quality once nothing
records from their microphone.

Notifications show in the top right corner, below the bar, for five seconds, or until they are
dealt with when urgent; the pointer over one keeps it. They sound as they arrive, from the
freedesktop sound theme, unless their program plays a sound of its own along with them, as Slack
does. A click on one opens what it is about and brings its program's window forward, and its
buttons do what they say. Those that time out wait behind the clock, which shows a dot meanwhile,
until they are dismissed. With do not disturb, which stays on across logins, only urgent ones show
and sound; the rest go straight behind the clock. Settings lists the applications that sent
notifications, each to be shown with sound, shown silently or not shown at all. Behind the clock
are the calendar too and, while a program plays music or video, its controls, which the media keys
work as well.

The lock screen shows the clock until a key is pressed or the screen clicked; then the password
field, ringed in the accent colour with a blinking caret while it takes keys, and the key pressed
already typed. Escape, or half a minute without typing, brings the clock back. With several
screens, the clock and the password show on the one the pointer is on, the others stay plain; a
minute without input turns them all off, whatever programs ask, until the next key or movement.
logind knows the session is locked (`loginctl show-session -p LockedHint`).
The screen locks with Super+L, from the quick settings, after five minutes without input (unless
a program such as a video player asks it to stay on while its window shows), before the computer
sleeps, and when `loginctl lock-session` asks. The password is checked through PAM, by
`/etc/pam.d/tde-cerberus`, as at the login prompt; with a fingerprint reader and fingers added
in the settings, a finger on it unlocks too, through fprintd. Should the lock screen ever quit without
unlocking, the session stays covered and a new one is started.

Hermes is also the polkit agent: when a program asks to do something that needs a password,
such as mounting a disk, it asks for it in a dialog, as GNOME does.

Tray icons are status notifier items, which most programs with one use (through Qt,
libappindicator or Electron). Programs that still use the old X11 tray can be shown with
`xembedsniproxy` from KDE's plasma-workspace.

Windows draw their own title bars, as they do on GNOME; for those that do not, like most X11
programs and Qt programs outside TDE, Atlas draws one, and a margin around them to resize them by. Its title bars and
the header bars of TDE's programs are normal or compact, as Settings has them. X11 programs run through Xwayland,
started the first time one connects.

Pick **TDE** on the login screen of GDM, LightDM or any display manager that lists Wayland sessions,
or log in with TDE's own.

## Logging in

**Charon** (`tde-charon`) is TDE's login screen, for [greetd](https://sr.ht/~kennylevinsen/greetd/),
in place of GDM; it looks like the lock screen. It lists the people who can log in, by name and
picture as AccountsService knows them, with "Not listed?" to type a name; the one who logged in
last is asked for their password at once, and the arrow beside it goes back to the list. Under the
password is the session to start, TDE or any other Wayland session, and X11 ones when
`xorg-xinit` is installed; each person's choice is remembered, and until they make one, the one
GDM remembered for them. What PAM asks and says shows there as it would with GDM; a password
that ran out, though, greetd cannot change, so the login screen says to change it on another
terminal (Ctrl+Alt+F2) first. The button at the top right suspends, restarts or turns the
computer off. Left alone, the screens go dark after five minutes, and a computer on its battery
sleeps after fifteen. Sessions start through the person's login shell, which reads their profile
as GDM's sessions do.

Logging in with the password unlocks the keyring with it, as GDM does, and with the keyring the
SSH keys whose passphrases it keeps (gcr's SSH agent stays the session's). For that, greetd's
PAM service needs the keyring's lines; the login screen says so at the bottom while it has
none. To switch from GDM:

```sh
sudo pacman -S greetd
sudo cp /usr/share/tde/charon/greetd.toml /etc/greetd/config.toml
sudo cp /usr/share/tde/charon/greetd.pam /etc/pam.d/greetd
sudo cp /usr/share/tde/charon/greetd-greeter.pam /etc/pam.d/greetd-greeter
sudo systemctl disable gdm && sudo systemctl enable greetd
```

Charon runs in Atlas (`tde-atlas --greeter tde-charon`) as greetd's `greeter` user, which cannot
read anyone's settings. As with GDM, where `monitors.xml` is copied by hand, the displays are
laid out as they are in a session once their settings are copied to it, the pointer and
touchpad with them: `sudo tde-charon --take-settings` (from a session, after changing them in
Settings). With several displays, the login shows on the primary one. At the login screen no key
starts anything; Ctrl+Alt+F1 to F12 switch terminals, and the volume and brightness keys work.
Switching users while one is logged in is not possible with greetd.

## Keys

| Keys | Action |
| --- | --- |
| Super | Argus, the overview of all windows (arrows and Enter pick one, Delete closes it, Escape goes back); typing searches applications |
| Super+A | All applications |
| Super+L | Lock the screen |
| Alt+Tab | Switch windows, with a picture of each |
| Super+Tab | Flip through the windows in 3D, as Windows 7 did; letting go of Super picks the one in front |
| Alt+\` | Switch between windows of the same application |
| Super+Up / Super+Down | Maximize / restore |
| Super+Left / Super+Right | Fill the left / right half of the screen |
| Super+Up+Left, Super+Down+Right, … | Fill that quarter of the screen the window is on (the two arrows held together, in either order) |
| Super+H | Minimize |
| Alt+F4, Super+Q | Close |
| Alt+F10 | Maximize or restore |
| Ctrl+Alt+T | Terminal (`$TERMINAL`, else the first one installed) |
| Super+E | Files |
| Print | Screenshot of a selection, a screen or a window, picked on the frozen screen |
| Shift+Print / Alt+Print | Screenshot of the screens / of the window in use, at once |
| Volume and brightness keys | Change them while held, showing the level, on the lock screen too |
| Microphone mute key | Mutes the microphone, or lets it be heard again |
| Media keys | Play, pause, skip and seek in the program that played last, on the lock screen too |
| Browser, mail, music, files and calculator keys | Open the default application for them |
| Search, tools and sleep keys | Find applications, open Settings, sleep |
| Ctrl+Alt+Delete | Log out |
| Ctrl+Alt+F1…F12 | Switch to another virtual terminal |

Dragging a window to the top edge maximizes it, to a side edge fills that half of the screen,
and into a corner, or against an edge within an eighth of the screen of one, fills that quarter;
a preview shows the place, which the window grows into; Super and a drag moves a window from
anywhere in it. Pushing the pointer into the top left corner, or clicking the narrow button
there at the start of the bar, opens the overview as Super does; it can show the desktop
instead, as the button at the end of Windows 7's taskbar did, and doing it again before using
another window brings the windows back.
New windows open where the last window of their application was, on the same display, as large
and maximized or in half or a quarter of the screen as it was, when that display is there and no other window of
the application is open; otherwise in the middle of the screen, where splash screens open too, and
windows that cannot be resized, such as an updater's progress. X11 programs that put a window where
they want it, as Java's `setLocationRelativeTo` does, have it there. Atlas keeps these places in
`~/.local/state/tde/windows`. Resizing a window by its edges gives it one size at
a time, each once it has drawn the last, so slow ones such as browsers do not stretch what they
show meanwhile.
In Argus, the windows go on showing what they do; a click on one switches to it, a middle click
or the × on it (in the corner where the window buttons are) closes it, and with several displays, dragging it to another display's part of
the overview moves it there.
Typing finds applications by name, by what they are ("terminal") and by their keywords, and
Enter starts the first one; the button at the bottom shows all of them. Each application starts in
a systemd scope of its own, rather than as part of Argus.

Print freezes the screens for a screenshot: drawing on them selects a part, which can be moved
and resized by its edges, and the buttons at the bottom switch to taking a whole screen or a
window, picked with a click (S, C and W switch too). Enter, Ctrl+C or the round button takes it,
Escape gives up. Screenshots are saved in `~/Pictures/Screenshots` and copied to the clipboard;
the notification that says so shows the file in the file manager. Windows are taken alone, with
their rounded corners see-through.

The desktop can be reached from another computer or a phone with
[wayvnc](https://github.com/any1/wayvnc), a VNC server that shows the screens and takes the
pointer and keys through what Atlas offers already. Run in the session, `wayvnc 127.0.0.1` serves
this computer alone, to be reached through an SSH tunnel (`ssh -L 5900:localhost:5900 …`); to
listen on the network, give it a password and a key first, as its manual says
(`enable_auth=true`, `username`, `password` and `rsa_private_key_file` in
`~/.config/wayvnc/config`), and run `wayvnc 0.0.0.0`.

## Applets

Applets are small programs in the bar, left of the tray, written in Lua. Every `.lua` file in
`~/.config/tde/applets` is one, shown in the order of the files' names; the bar reads them when
it starts (`systemctl --user restart tde-hermes` after adding one). What goes wrong in one is told
in the bar's log (`journalctl --user -u tde-hermes`) and leaves the rest of the bar as it was; a
call that takes longer than 200 ms is stopped.

The file returns a table, which is the applet: each hook gets it as `self`, to keep what it likes
in.

```lua
local applet = {
    width = 150,      -- pixels in the bar; 100 if left out
    interval = 1000,  -- milliseconds between calls to update; at least 100
    popup = { width = 520, height = 470 },  -- a panel that opens below when clicked; optional
}

function applet:update() end           -- every interval, and once at the start
function applet:paint(canvas) end      -- draws it in the bar
function applet.popup.paint(self, canvas) end  -- draws the panel

return applet
```

The global `tde` table tells what the computer is doing, measured since it was last asked, and
the desktop's colours:

- `tde.cpu()`: `{ total = 12.5, cores = { 30.1, 2.0, ... } }`, how busy each core is, in percent;
- `tde.memory()`: `{ total, used, swap_total, swap_used }`, in bytes;
- `tde.network()`: `{ down, up }`, bytes per second through every interface but the loopback;
- `tde.theme`: `window`, `base`, `header`, `text`, `dim_text`, `accent`, `accent_text`, `border`
  and `error`, as `"#aarrggbb"`;
- `tde.api`: 1, the version of all this.

The canvas, `canvas.width` by `canvas.height` pixels, takes colours as `"#rrggbb"` or
`"#aarrggbb"` and can be drawn on only within `paint`:

- `canvas:rect(x, y, width, height, color [, radius])`, filled, its corners rounded by `radius`;
- `canvas:line({ x1, y1, x2, y2, ... }, color [, width])`, through the points in turn;
- `canvas:area({ x1, y1, x2, y2, x3, y3, ... }, color)`, the shape they make, filled;
- `canvas:text(x, y, width, height, text, color [, { size = 9, bold = false, align = "left" }])`,
  in the middle of the box from top to bottom, `align` being `"left"`, `"center"` or `"right"`.

Lua's `string`, `table`, `math` and `utf8` are there; files, programs and modules are not.

## Building

```sh
cmake -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr
sudo cmake --install build
```

Building needs wlroots 0.20 (with Xwayland), libinput, Lua, pango, libcanberra, libpulse, libsecret, PAM, libsystemd, xkbcommon, Qt 6, polkit-qt6, layer-shell-qt, libtde, wayland-protocols and wlr-protocols.
The session runs a terminal, Ariadne for files, and xdg-desktop-portal with its GTK and wlroots back ends.
Networking, Bluetooth, the battery, the power mode and fingerprints come from NetworkManager, BlueZ, UPower, power-profiles-daemon and fprintd when they run; what is not there is left out.

## Changing it

Daedalus, shown as Settings, connects to Wi-Fi networks, shows their passwords and forgets
them, pairs Bluetooth devices (it is visible to others, and looks for them, while its page
shows), and sets the theme, light or dark, the icons, the pointer and its size, the theme of GTK 3
programs, and the desktop's background (a picture and how it covers the screen, or a colour;
pictures added there are kept in `~/.local/share/backgrounds`),
how the displays are arranged (a display dragged next to another snaps to it) and which one has
the bar, which output and input sound uses and how loud, the power mode and when the screens
turn off and the computer sleeps, plugged in and on battery, what Alt+Tab shows of the windows,
how long the animations take, the keyboard layouts, the keys that switch them and the compose
key, Num Lock (turned on and off there for keyboards without the key) and how it is at login,
as it was left or always on or off, how fast the mouse and the touchpad move and scroll, and which way, and tapping to click,
whether and when the screen locks by itself, and before sleeping, and the fingers that unlock it,
which applications open web pages, mail, music and the rest, the terminal, which programs start
at login, which end of title bars has the window buttons (GTK programs are told too), what the
hot corner does, and whether the clocks show seconds. Its
Power page shows the battery's charge, health and the power it gives. `tde-daedalus --page
bluetooth` opens it on a page, or shows the one open there. The session's own settings are in
`~/.config/tde/session/config.lua`, which Daedalus writes; the theme, the icons, the pointer,
the lock screen's settings and the terminal are in the desktop config, `~/.config/tde/config.lua`,
of which Daedalus changes only those values, and GTK programs are told the theme, icons and
pointer through GNOME's settings. Title bars that Atlas draws put their buttons where the desktop
config says, as TDE's programs do. Both can be edited by hand too, and every part of the session
follows them once saved.

Display changes are tried out first, and go back after 15 seconds unless they are kept, in
case a display shows nothing with them. Arrangements are kept for every set of displays
plugged in together, and come back whenever that set is plugged in again; windows move along
with their display, and those on one that goes move to another. The bar, the notifications
and the window switchers are on the primary display; every display has its background and
its part of the overview. X11 programs know it as the primary display too, as Wine does when it
puts a fullscreen game there; moved to another display, such a game still aims the pointer
right, as X11 goes on believing it is where it began.

Files are opened and saved through xdg-desktop-portal, which `tde-portals.conf` has pick them
in [Ariadne](https://github.com/zskamljic/tde-ariadne) when it is installed. Programs that ask
the portal for a screenshot or a colour on the screen get them from Argus, the way Print takes
them; the screen is shared by `xdg-desktop-portal-wlr`, for which Argus asks whether a whole
screen or one window is shared, and the rest is left to GTK's portals. The session sets
`QT_QPA_PLATFORMTHEME=xdgdesktopportal` and `GTK_USE_PORTAL=1`, unless they are set already, so
Qt and GTK 3 programs use the portal too. Atlas places the picker over the window it was opened
for, which the program shares with it through xdg-foreign.

Programs that want a password or a passphrase without a terminal to ask in, such as ssh, git
and `sudo -A`, ask in a window of tde-askpass, which the session sets as `SSH_ASKPASS` and
`SUDO_ASKPASS`; ssh asks there from terminals too. A key unlocked there stays unlocked until
the session ends, in openssh's agent, which the session starts unless it has an agent already,
such as gnome-keyring's. Its passphrase can be remembered in the keyring (gnome-keyring,
KeePassXC or another Secret Service), to unlock the key without asking; one that no longer
works is forgotten and asked for again. Administrator passwords that polkit asks for are asked
for by the bar.

Hermes and Argus run as the systemd user services `tde-hermes.service` and `tde-argus.service`,
which `tde-session.target` starts; should either crash, systemd starts it again at once (a lock
screen it started stays). They run in `session.slice`, weighted ahead of applications for the
processor, so the overview and the bar keep up while programs keep the computer busy; in the
overview, the windows go on showing what they do once it is open, not while it opens. Other programs started with the session are listed in
`/usr/share/tde/autostart`; a copy in `~/.config/tde/autostart` takes its place, and should no
longer start the bar and the overview, as copies made before did. Programs that start at login
the usual way, from `/etc/xdg/autostart` and `~/.config/autostart`, start too, as systemd
services that `tde-session.target` brings up and takes down with the session; those meant only
for other desktops do not. Daedalus lists them under Startup Applications, to turn off (by a copy
in `~/.config/autostart` that says so) or to add applications to, from the next login on.
Keyboard layouts come from `XKB_DEFAULT_LAYOUT` and friends, or else from the system layout
that `localectl`, and Daedalus, set; Atlas follows changes to it at once. Num Lock goes for every keyboard at
once; Atlas keeps whether it is on in `~/.local/state/tde/numlock`, and turns it on or off when
that file is written.

## Testing in a virtual machine

`tools/vm` sets up an Arch Linux machine under QEMU that logs straight into the session:

```sh
tools/vm create                    # once; downloads Arch and installs GDM and the session
tools/vm install ~/.cache/pacaur/libtde/*.pkg.tar.zst ~/.cache/pacaur/tde-ariadne/*.pkg.tar.zst
tools/vm run                       # the screen follows the window size; --headless for none
tools/vm deploy                    # after a change; Ctrl+Alt+Delete restarts the session
```

Atlas makes as many displays as `ATLAS_VIRTUAL_OUTPUTS` says that show on no screen, which
`grim` still captures; with `ATLAS_VIRTUAL_OUTPUTS=1` in the machine's `/etc/environment`,
the session has a second display to try things on.

The user is `tde` with the password `tde`. The window grabs the keyboard while the pointer is over
it (Ctrl+Alt+G toggles it), so Super and other shortcuts go to the machine instead of the host.
