# sdmenu — super-fast dmenu clone

A drop-in dmenu replacement with an always-on daemon, app icons, rofi-style browser, and sub-100ms startup.

## Quick start

```bash
make
# Start the daemon (or let it auto-start on first run)
./sdmened &
# Show the menu
./sdmenu
```

Pipe items to filter:
```bash
printf "firefox\nst\nnvim" | ./sdmenu
```

## How it works

Two binaries:

| Binary | Size | Deps | Role |
|--------|------|------|------|
| `sdmened` | 27K | X11 + Xft | Background daemon — holds X11 connection, font, icons |
| `sdmenu` | 15K | libc only | Thin client — connects to daemon via Unix socket, prints selection |

`sdmened` runs once (auto-started or via i3 `exec_always`). Every `$mod+d` hits the running daemon → instant menu with zero X11 init overhead.

## Usage

### Normal mode — dmenu replacement

```
echo -e "firefox\nst\nnvim" | sdmenu
```

- Type to filter — matching items appear horizontally to the right
- `Tab` / arrows — cycle through matches
- `Enter` — select and execute
- `Escape` — cancel

### Rofi mode — full app browser with icons and paths

```
sdmenu -R
```

Shows a full-screen vertical list with:
- 24×24 app icon (left)
- App name
- Resolved binary path (right-aligned)

Bind to `$mod+Shift+d` in i3:
```
bindsym $mod+Shift+d exec /path/to/sdmenu_run -R
```

### Options

| Flag | Default | Description |
|------|---------|-------------|
| `-l N` | — | Show N items vertically (default: single-line horizontal) |
| `-b` | top | Show bar at bottom of screen |
| `-i` | — | Case-insensitive matching |
| `-p prompt` | `>` | Prompt string |
| `-fn font` | `Inconsolata LGC Markup:bold:size=14` | Font (Xft/fontconfig pattern) |
| `-nb color` | `#0d0d0d` | Normal background |
| `-nf color` | `#c8c8c8` | Normal foreground |
| `-sb color` | `#2a2a2a` | Selection background |
| `-sf color` | `#ffffff` | Selection foreground |

## i3 integration

```i3
# Start daemon on login
exec_always --no-startup-id sh -c 'pidof sdmened >/dev/null 2>&1 || /path/to/sdmened'

# Normal launcher
bindsym $mod+d exec /path/to/sdmenu_run

# Rofi browser
bindsym $mod+Shift+d exec /path/to/sdmenu_run -R
```

`sdmenu_run` is a thin wrapper:
```bash
#!/usr/bin/env bash
/path/to/sdmenu "$@" | ${SHELL:-"/bin/sh"} &
```

## Icons

Icons load lazily after the first menu closes. Press `$mod+d` twice on first run:
1. **1st press** — menu appears instantly, daemon converts+caches icons in background
2. **2nd press** — icons appear next to every app with a matching `.desktop` entry

Cached to `~/.cache/sdmenu_icons/<app>.rgb`. Subsequent starts load from cache.

**Source directories scanned:**
- `/usr/share/applications/` (standard Linux)
- `/run/current-system/sw/share/applications/` (NixOS)
- `/usr/local/share/applications/`
- `/var/lib/flatpak/exports/share/applications/`
- `~/.local/share/applications/` (AppImage integration)
- `~/Applications/` (AppImage files)
- `~/AppImages/` (AppImage files)

**Icon matching strategies:**
1. Exact basename match (`firefox` ↔ `firefox.desktop`)
2. `Exec=` command match (`chromium` ↔ `Exec=chromium %U`)
3. Prefix match (`brave` ↔ `brave-browser`)
4. AppImage files without `.desktop` → uses filename as icon name

**Requires:** `convert` (ImageMagick) for PNG→Pixmap conversion. Optional — icons gracefully degrade.

## Performance

| Phase | First run | Subsequent |
|-------|-----------|------------|
| Daemon startup (no cache) | ~130ms | — |
| Daemon startup (cached) | — | ~25ms |
| First menu invocation | instant (no icons) | instant (with icons) |
| Match + draw (1781 items) | ~50µs | ~50µs |

## Building from source

```bash
# Dependencies: X11, Xft, Xinerama development headers + ImageMagick (optional)
make
```

The Makefile auto-detects build flags via `pkg-config`, falling back to NixOS store paths.

### Distro-specific deps

```bash
# Debian/Ubuntu
apt install libx11-dev libxft-dev libxinerama-dev libfreetype-dev libfontconfig-dev

# Fedora
dnf install libX11-devel libXft-devel libXinerama-devel freetype-devel fontconfig-devel

# Arch
pacman -S libx11 libxft libxinerama freetype2 fontconfig
```

## Testing

```bash
./test.sh     # functional tests (daemon, client, IPC, icons)
./bench.sh    # startup timing benchmark
```
