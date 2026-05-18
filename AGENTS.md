# Agent notes for sdmenu

## Architecture

Two binaries, single repo:

- **`sdmenu`** — thin client (15K, no X11). Connects to daemon via Unix socket, prints selection to stdout.
- **`sdmened`** — persistent daemon (27K, X11 + Xft). Holds X11 connection, font, colors, icons. Listens on `/tmp/sdmened.sock`.

### Startup flow

```
User presses $mod+d
  → sdmenu (client) starts
    → alive() checks /tmp/sdmened.pid via kill(pid, 0)
    → if no daemon: fork+exec sdmened, wait for pidfile (2s timeout)
    → connect to /tmp/sdmened.sock
    → write mode byte ('d' = normal, 'r' = rofi)
    → poll() on socket (15s timeout)
    → daemon accepts, creates X11 window, runs event loop
    → user selects/cancels → daemon writes selection to socket
    → sdmenu reads selection, puts() to stdout
    → shell executes selection
```

### Daemon lifecycle

```
1. Check alive() → exit if another instance running
2. signal(SIGPIPE, SIG_IGN)
3. prctl(PR_SET_NAME, "sdmened")  → pidof sdmened works
4. Create socket + bind + listen
5. Write PID to /tmp/sdmened.pid
6. read_items() — from stdin pipe, or auto-generate from PATH, or read ~/.cache/sdmenu_items
7. init_x11() — XOpenDisplay, load font (Xft), allocate colors
8. load_icons_cache() — scan ~/.cache/sdmenu_icons/*.rgb
9. daemon_serve() — infinite accept loop
```

### Per-invocation flow (daemon_serve)

```
accept client → read mode byte (1 char)
  → reset state (text, cursor, sel, top)
  → create_window() — full monitor width, override_redirect, 0 border
  → run() — poll(X11_fd, client_fd), XNextEvent loop, XGrabKeyboard
    → on disconnect: set sel=-1, break
    → on Escape: set sel=-1, break
    → on Enter: break
    → on keypress: match+filters, redraw
  → done: XUngrabKeyboard, XSync, XSetInputFocus(prev), write(selection) to socket
  → destroy_window() — XUnmapWindow, XSync, XftDrawDestroy, XFreeGC, XDestroyWindow
  → close(cfd)
  → if !icons_full: load_icons_convert() — convert+cache uncached icons
```

## Protocol

Client → Daemon: 1 byte mode (`d` = normal, `r` = rofi)  
Daemon → Client: null-terminated selection string (or empty for cancel)

## Key data structures

```c
typedef struct {
  Pixmap pixmap;
  int loaded;
} Icon;

typedef struct {
  char **items;           // executable names (sorted)
  int nitems;
  int *matches;           // indices into items[] matching current filter
  int nmatches;
  char text[INP_MAX];     // user input
  int cursor, sel, top;   // cursor position, selected index, scroll offset
  int width, height;
  int promptw, maxvis;    // cached prompt width, max visible items
  int fw, fh, BH;         // font width, font height, row height
  Icon *icons;            // per-item icons (NULL until load_icons_cache)
  char **paths;           // resolved binary paths (for rofi mode)
  int rofi_mode;
  Display *dpy; Window win; GC gc;
  XftFont *xfont; XftDraw *xdraw;
  XftColor normfg_c, normbg_c, selfg_c, selbg_c;
  unsigned long normfg_p, normbg_p, selfg_p, selbg_p;
  Colormap cmap; int scr;
  int basex, basey, monh; // monitor geometry
} DMenu;
```

## Cache files

| Path | Purpose |
|------|---------|
| `~/.cache/sdmenu_items` | Cached list of PATH executables (mtime-checked) |
| `~/.cache/sdmenu_icons/*.rgb` | 24×24 raw RGB icon data (4-byte dim + dim*dim*3 bytes) |
| `/tmp/sdmened.pid` | Daemon PID for `kill(pid,0)` alive check |
| `/tmp/sdmened.sock` | Unix domain socket |

## Drawing order (critical!)

The draw order in the horizontal inline mode MUST be:

```
1. XFillRectangle — selection highlight background
2. XCopyArea    — icon (on top of highlight)
3. XftDrawStringUtf8 — text (on top of icon)
```

The vertical list mode has the correct order (highlight → icon → text).  
The horizontal mode was originally wrong (icon → highlight → text, which erased the icon).

## Icon matching

In `load_icons_cache()` / `load_icons_convert()`:

1. Parse all `.desktop` files from `appdirs[]`
2. Extract `Name=`, `Icon=`, `Exec=` fields
3. For each menu item, try:
   - Exact match against `.desktop` basename
   - Exact match against extracted `Exec=` command
   - Prefix match (item is prefix of Exec command)
4. If icon cache file exists (`~/.cache/sdmenu_icons/<desk_name>.rgb`): load from disk
5. Otherwise (in convert phase only): run `convert <png> -resize 24x24 ppm:-`, parse PPM, create Pixmap, save to cache

## Cross-platform

- Makefile tries `pkg-config` first, falls back to NixOS store paths
- All paths are standard Freedesktop (`/usr/share`, `~/.local/share`)
- ImageMagick `convert` is optional
- The `appdirs[]` array includes both NixOS and standard Linux paths
