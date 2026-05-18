#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/Xinerama.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <locale.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <errno.h>
#include <dirent.h>

#define INP_MAX 512
#define MAX_ITEMS 4096
#define PAD 4
#define BORDER 2
#define SOCK_PATH "/tmp/sdmened.sock"
#define ICON_SIZE 24

static char *fontstr = "9x15";
static char *prompt = "";
static char *normbg = "#222222";
static char *normfg = "#bbbbbb";
static char *selbg  = "#005577";
static char *selfg  = "#eeeeee";
static int lines = 0;
static int topbar = 1;
static int mon = -1;
static int insensitive = 0;
static int daemon_mode = 0;
static int benchmark = 0;
static long t0 = 0;

static long ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

#define MARK(msg) do { if (benchmark) \
  fprintf(stderr, "  %3ld ms  %s\n", ms() - t0, msg); } while (0)

typedef struct {
  Pixmap pixmap;
  int loaded;
} Icon;

typedef struct {
  char **items;
  int nitems;
  int *matches;
  int nmatches;
  char text[INP_MAX];
  int cursor;
  int sel;
  int top;
  int width;
  int height;
  int promptw;
  int maxvis;
  int fw, fh, BH;
  Icon *icons;
  Display *dpy;
  Window win;
  GC gc;
  XFontStruct *xfont;
  unsigned long normfg_p, normbg_p, selfg_p, selbg_p;
  Colormap cmap;
  int scr;
  int basex, basey, monh;
} DMenu;

static int textw(DMenu *dm, const char *s, int n) {
  return XTextWidth(dm->xfont, s, n);
}

static int prefixmatch(const char *item, const char *pat) {
  int n = strlen(pat);
  if (n == 0) return 1;
  if (insensitive)
    return strncasecmp(item, pat, n) == 0;
  return strncmp(item, pat, n) == 0;
}

static long matchtime = 0;

static void match(DMenu *dm) {
  long mt0 = benchmark ? ms() : 0;
  dm->nmatches = 0;
  for (int i = 0; i < dm->nitems; i++)
    if (prefixmatch(dm->items[i], dm->text))
      dm->matches[dm->nmatches++] = i;
  if (dm->nmatches == 0)
    dm->sel = 0;
  else if (dm->sel >= dm->nmatches)
    dm->sel = dm->nmatches - 1;
  matchtime += ms() - mt0;
}

static void draw(DMenu *dm) {
  int mh = dm->maxvis;
  if (mh > dm->nmatches) mh = dm->nmatches;

  if (dm->sel >= dm->top + mh) dm->top = dm->sel - mh + 1;
  if (dm->sel < dm->top) dm->top = dm->sel;

  XSetForeground(dm->dpy, dm->gc, dm->normbg_p);
  XFillRectangle(dm->dpy, dm->win, dm->gc, 0, 0, dm->width, dm->height);
  XSetFont(dm->dpy, dm->gc, dm->xfont->fid);

  int by = dm->xfont->ascent + 2;
  XSetForeground(dm->dpy, dm->gc, dm->normfg_p);
  XDrawString(dm->dpy, dm->win, dm->gc, PAD, by, prompt, strlen(prompt));
  XDrawString(dm->dpy, dm->win, dm->gc, PAD + dm->promptw, by,
    dm->text, strlen(dm->text));

  int cx = PAD + dm->promptw + textw(dm, dm->text, dm->cursor);
  XFillRectangle(dm->dpy, dm->win, dm->gc, cx, 1, 2, dm->fh - 2);

  int textoff = PAD + ICON_SIZE + 4;
  for (int i = 0; i < mh; i++) {
    int idx = dm->top + i;
    int y = dm->BH + i * dm->BH;
    int ty = y + (dm->BH + dm->xfont->ascent - dm->xfont->descent) / 2;

    if (idx == dm->sel) {
      XSetForeground(dm->dpy, dm->gc, dm->selbg_p);
      XFillRectangle(dm->dpy, dm->win, dm->gc, 0, y, dm->width, dm->BH);
      XSetForeground(dm->dpy, dm->gc, dm->selfg_p);
    } else {
      XSetForeground(dm->dpy, dm->gc, dm->normfg_p);
    }

    if (dm->icons && dm->icons[dm->matches[idx]].loaded)
      XCopyArea(dm->dpy, dm->icons[dm->matches[idx]].pixmap, dm->win, dm->gc,
        0, 0, ICON_SIZE, ICON_SIZE, PAD, y + (dm->BH - ICON_SIZE) / 2);

    XDrawString(dm->dpy, dm->win, dm->gc, textoff, ty,
      dm->items[dm->matches[idx]], strlen(dm->items[dm->matches[idx]]));
  }
}

static void matchanddraw(DMenu *dm) {
  match(dm);
  draw(dm);
}

static void killword(DMenu *dm) {
  int i = dm->cursor;
  while (i > 0 && dm->text[i - 1] == ' ') i--;
  while (i > 0 && dm->text[i - 1] != ' ') i--;
  memmove(dm->text + i, dm->text + dm->cursor, strlen(dm->text) - dm->cursor + 1);
  dm->cursor = i;
}

static void run(DMenu *dm, int out_fd) {
  XEvent ev;
  char buf[32];
  KeySym ks;

  matchanddraw(dm);
  XFlush(dm->dpy);

  while (1) {
    XNextEvent(dm->dpy, &ev);
    if (ev.type == Expose) { draw(dm); XFlush(dm->dpy); continue; }
    if (ev.type != KeyPress) continue;

    int len = XLookupString(&ev.xkey, buf, sizeof(buf), &ks, NULL);

    if (ks == XK_Escape) { dm->sel = -1; break; }
    if ((ks == XK_Return || ks == XK_KP_Enter) && dm->nmatches > 0) break;

    if (ks == XK_Tab || ks == XK_Down || ks == XK_KP_Down) {
      if (dm->nmatches > 0) { dm->sel = (dm->sel + 1) % dm->nmatches; draw(dm); XFlush(dm->dpy); }
      continue;
    }
    if ((ks == XK_Up || ks == XK_KP_Up) ||
        (ks == XK_Tab && (ev.xkey.state & ShiftMask))) {
      if (dm->nmatches > 0) { dm->sel = (dm->sel - 1 + dm->nmatches) % dm->nmatches; draw(dm); XFlush(dm->dpy); }
      continue;
    }

    if (ks == XK_Left || ks == XK_KP_Left) {
      if (dm->cursor > 0) { dm->cursor--; draw(dm); XFlush(dm->dpy); } continue;
    }
    if (ks == XK_Right || ks == XK_KP_Right) {
      int l = strlen(dm->text);
      if (dm->cursor < l) { dm->cursor++; draw(dm); XFlush(dm->dpy); } continue;
    }
    if (ks == XK_Home || ks == XK_KP_Home) { dm->cursor = 0; draw(dm); XFlush(dm->dpy); continue; }
    if (ks == XK_End || ks == XK_KP_End) { dm->cursor = strlen(dm->text); draw(dm); XFlush(dm->dpy); continue; }

    if (ks == XK_BackSpace) {
      if (dm->cursor > 0) {
        memmove(dm->text + dm->cursor - 1, dm->text + dm->cursor,
          strlen(dm->text) - dm->cursor + 1);
        dm->cursor--;
        matchanddraw(dm); XFlush(dm->dpy);
      } continue;
    }
    if (ks == XK_Delete) {
      int l = strlen(dm->text);
      if (dm->cursor < l) {
        memmove(dm->text + dm->cursor, dm->text + dm->cursor + 1, l - dm->cursor);
        matchanddraw(dm); XFlush(dm->dpy);
      } continue;
    }

    if (len > 0) {
      char c = buf[0];
      switch (c) {
      case '\x01': dm->cursor = 0; draw(dm); XFlush(dm->dpy); continue;
      case '\x02': if (dm->cursor > 0) { dm->cursor--; draw(dm); XFlush(dm->dpy); } continue;
      case '\x03': dm->sel = -1; goto done;
      case '\x04':
        if (dm->cursor < (int)strlen(dm->text)) {
          memmove(dm->text + dm->cursor, dm->text + dm->cursor + 1,
            strlen(dm->text) - dm->cursor);
          matchanddraw(dm); XFlush(dm->dpy);
        } continue;
      case '\x05': dm->cursor = strlen(dm->text); draw(dm); XFlush(dm->dpy); continue;
      case '\x06':
        if (dm->cursor < (int)strlen(dm->text)) { dm->cursor++; draw(dm); XFlush(dm->dpy); }
        continue;
      case '\x07': continue;
      case '\x08':
        if (dm->cursor > 0) {
          memmove(dm->text + dm->cursor - 1, dm->text + dm->cursor,
            strlen(dm->text) - dm->cursor + 1);
          dm->cursor--;
          matchanddraw(dm); XFlush(dm->dpy);
        } continue;
      case '\x09':
        if (dm->nmatches > 0) { dm->sel = (dm->sel + 1) % dm->nmatches; draw(dm); XFlush(dm->dpy); }
        continue;
      case '\x0a': case '\x0d': if (dm->nmatches > 0) goto done; continue;
      case '\x0b': dm->text[dm->cursor] = '\0'; matchanddraw(dm); XFlush(dm->dpy); continue;
      case '\x0e':
        if (dm->nmatches > 0) { dm->sel = (dm->sel + 1) % dm->nmatches; draw(dm); XFlush(dm->dpy); }
        continue;
      case '\x10':
        if (dm->nmatches > 0) { dm->sel = (dm->sel - 1 + dm->nmatches) % dm->nmatches; draw(dm); XFlush(dm->dpy); }
        continue;
      case '\x15': dm->text[0] = '\0'; dm->cursor = 0; matchanddraw(dm); XFlush(dm->dpy); continue;
      case '\x17': killword(dm); matchanddraw(dm); XFlush(dm->dpy); continue;
      default:
        if (isprint((unsigned char)c)) {
          int l = strlen(dm->text);
          if (l < INP_MAX - 1) {
            memmove(dm->text + dm->cursor + 1, dm->text + dm->cursor,
              l - dm->cursor + 1);
            dm->text[dm->cursor] = c;
            dm->cursor++;
            matchanddraw(dm); XFlush(dm->dpy);
          }
        }
      }
    }
  }
done:
  if (dm->sel >= 0 && dm->sel < dm->nmatches) {
    const char *s = dm->items[dm->matches[dm->sel]];
    write(out_fd, s, strlen(s) + 1);
  }
  if (benchmark)
    fprintf(stderr, "  %3ld ms  total match time\n", matchtime);
}

static int init_x11(DMenu *dm) {
  dm->dpy = XOpenDisplay(NULL);
  if (!dm->dpy) { fprintf(stderr, "sdmenu: cannot open display\n"); return -1; }
  dm->scr = DefaultScreen(dm->dpy);
  dm->xfont = XLoadQueryFont(dm->dpy, fontstr);
  if (!dm->xfont) dm->xfont = XLoadQueryFont(dm->dpy, "9x15");
  if (!dm->xfont) dm->xfont = XLoadQueryFont(dm->dpy, "8x13");
  if (!dm->xfont) dm->xfont = XLoadQueryFont(dm->dpy, "fixed");
  dm->fw = dm->xfont->max_bounds.width;
  dm->fh = dm->xfont->ascent + dm->xfont->descent;
  dm->BH = dm->fh + 4;
  if (dm->BH < ICON_SIZE + 4) dm->BH = ICON_SIZE + 4;
  dm->cmap = DefaultColormap(dm->dpy, dm->scr);
  XColor xc, unused;
  XAllocNamedColor(dm->dpy, dm->cmap, normfg, &xc, &unused);
  dm->normfg_p = xc.pixel;
  XAllocNamedColor(dm->dpy, dm->cmap, normbg, &xc, &unused);
  dm->normbg_p = xc.pixel;
  XAllocNamedColor(dm->dpy, dm->cmap, selfg, &xc, &unused);
  dm->selfg_p = xc.pixel;
  XAllocNamedColor(dm->dpy, dm->cmap, selbg, &xc, &unused);
  dm->selbg_p = xc.pixel;
  dm->promptw = textw(dm, prompt, strlen(prompt));
  return 0;
}

static Pixmap ppm_to_pixmap(DMenu *dm, const char *path) {
  char cmd[4096];
  snprintf(cmd, sizeof(cmd), "convert '%s' -resize %dx%d ppm:- 2>/dev/null", path, ICON_SIZE, ICON_SIZE);
  FILE *fp = popen(cmd, "r");
  if (!fp) return 0;
  char buf[256];
  if (!fgets(buf, sizeof(buf), fp)) { pclose(fp); return 0; }
  if (buf[0] != 'P' || buf[1] != '6') { pclose(fp); return 0; }
  do { if (!fgets(buf, sizeof(buf), fp)) { pclose(fp); return 0; } } while (buf[0] == '#');
  int w, h;
  sscanf(buf, "%d %d", &w, &h);
  if (!fgets(buf, sizeof(buf), fp)) { pclose(fp); return 0; }
  int rowbytes = w * 3;
  unsigned char *rgb = malloc(h * rowbytes);
  for (int y = 0; y < h; y++) {
    if (fread(rgb + y * rowbytes, 1, rowbytes, fp) != (size_t)rowbytes) {
      free(rgb); pclose(fp); return 0;
    }
  }
  pclose(fp);
  int depth = DefaultDepth(dm->dpy, dm->scr);
  Visual *vis = DefaultVisual(dm->dpy, dm->scr);
  XImage *img = XCreateImage(dm->dpy, vis, depth, ZPixmap, 0, NULL, w, h, 32, 0);
  if (!img) { free(rgb); return 0; }
  img->data = calloc(h, img->bytes_per_line);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      unsigned char r = rgb[(y * w + x) * 3];
      unsigned char g = rgb[(y * w + x) * 3 + 1];
      unsigned char b = rgb[(y * w + x) * 3 + 2];
      unsigned long pixel = 0;
      if (depth == 24 || depth == 32)
        pixel = (r << 16) | (g << 8) | b;
      else if (depth == 16)
        pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
      else if (depth == 8)
        pixel = ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6);
      long dst = y * img->bytes_per_line + x * (img->bits_per_pixel / 8);
      if (img->byte_order == LSBFirst) {
        img->data[dst] = pixel & 0xFF;
        if (img->bits_per_pixel >= 16) img->data[dst+1] = (pixel >> 8) & 0xFF;
        if (img->bits_per_pixel >= 24) img->data[dst+2] = (pixel >> 16) & 0xFF;
        if (img->bits_per_pixel >= 32) img->data[dst+3] = 0;
      } else {
        if (img->bits_per_pixel >= 24) img->data[dst] = (pixel >> 16) & 0xFF;
        if (img->bits_per_pixel >= 16) img->data[dst+1] = (pixel >> 8) & 0xFF;
        img->data[dst+2] = pixel & 0xFF;
        if (img->bits_per_pixel >= 32) img->data[dst+3] = 0;
      }
    }
  free(rgb);
  Pixmap pm = XCreatePixmap(dm->dpy, RootWindow(dm->dpy, dm->scr), w, h, depth);
  GC gc = XCreateGC(dm->dpy, pm, 0, NULL);
  XPutImage(dm->dpy, pm, gc, img, 0, 0, 0, 0, w, h);
  XFreeGC(dm->dpy, gc);
  free(img->data);
  img->data = NULL;
  XDestroyImage(img);
  return pm;
}

static void load_icons(DMenu *dm) {
  dm->icons = calloc(dm->nitems, sizeof(Icon));
  DIR *dir = opendir("/run/current-system/sw/share/applications");
  if (!dir) return;
  struct dirent *entry;
  int ndesk = 0;
  char **desk_names = NULL;
  char **desk_icons = NULL;
  while ((entry = readdir(dir))) {
    int l = strlen(entry->d_name);
    if (l < 9 || strcmp(entry->d_name + l - 8, ".desktop") != 0) continue;
    char path[4096];
    snprintf(path, sizeof(path), "/run/current-system/sw/share/applications/%s", entry->d_name);
    FILE *fp = fopen(path, "r");
    if (!fp) continue;
    char *icon = NULL, *name = NULL;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
      if (strncmp(line, "Icon=", 5) == 0) {
        free(icon);
        icon = strdup(line + 5);
        char *nl = strchr(icon, '\n');
        if (nl) *nl = '\0';
      }
      if (strncmp(line, "Name=", 5) == 0 && !name) {
        name = strdup(line + 5);
        char *nl = strchr(name, '\n');
        if (nl) *nl = '\0';
      }
    }
    fclose(fp);
    if (!icon) { free(name); continue; }
    char basename[256];
    strncpy(basename, entry->d_name, l - 8);
    basename[l - 8] = '\0';
    desk_names = realloc(desk_names, (ndesk + 1) * sizeof(char *));
    desk_icons = realloc(desk_icons, (ndesk + 1) * sizeof(char *));
    desk_names[ndesk] = strdup(basename);
    desk_icons[ndesk] = icon;
    ndesk++;
    free(name);
  }
  closedir(dir);

  if (ndesk == 0) return;
  for (int i = 0; i < dm->nitems; i++) {
    const char *cmd = dm->items[i];
    int found = 0;
    for (int d = 0; d < ndesk; d++) {
      if (strcmp(cmd, desk_names[d]) == 0) { found = 1; }
      else {
        int nl = strlen(desk_names[d]);
        if (nl > (int)strlen(cmd)) nl = strlen(cmd);
        if (strncmp(cmd, desk_names[d], nl) == 0 && cmd[nl] == '\0') found = 1;
      }
      if (found) {
        char sizes[][8] = {"48x48", "32x32", "24x24", "64x64", "96x96", "128x128"};
        for (int s = 0; s < 6; s++) {
          char ipath[4096];
          snprintf(ipath, sizeof(ipath),
            "/run/current-system/sw/share/icons/hicolor/%s/apps/%s.png",
            sizes[s], desk_icons[d]);
          FILE *test = fopen(ipath, "r");
          if (test) {
            fclose(test);
            Pixmap pm = ppm_to_pixmap(dm, ipath);
            if (pm) { dm->icons[i].pixmap = pm; dm->icons[i].loaded = 1; }
            break;
          }
        }
        break;
      }
    }
  }
  for (int d = 0; d < ndesk; d++) {
    free(desk_names[d]);
    free(desk_icons[d]);
  }
  free(desk_names);
  free(desk_icons);
}

static void create_window(DMenu *dm) {
  int sw = DisplayWidth(dm->dpy, dm->scr);
  int sh = DisplayHeight(dm->dpy, dm->scr);
  dm->width = sw;
  dm->maxvis = lines > 0 ? lines : (sh - dm->BH) / dm->BH;
  if (dm->maxvis > dm->nitems) dm->maxvis = dm->nitems;
  dm->height = dm->BH + dm->maxvis * dm->BH + BORDER * 2;

  int nmon;
  XineramaScreenInfo *info = XineramaQueryScreens(dm->dpy, &nmon);
  if (info) {
    int idx = (mon >= 0 && mon < nmon) ? mon : 0;
    dm->width = info[idx].width;
    dm->basex = info[idx].x_org;
    dm->basey = info[idx].y_org;
    dm->monh = info[idx].height;
    XFree(info);
  } else {
    dm->basex = 0; dm->basey = 0; dm->monh = sh;
  }
  int x = dm->basex;
  int y = topbar ? dm->basey : dm->basey + dm->monh - dm->height;

  dm->win = XCreateSimpleWindow(dm->dpy, RootWindow(dm->dpy, dm->scr),
    x, y, dm->width, dm->height, BORDER, dm->normfg_p, dm->normbg_p);
  dm->gc = XCreateGC(dm->dpy, dm->win, 0, NULL);
  XSetFont(dm->dpy, dm->gc, dm->xfont->fid);
  XSelectInput(dm->dpy, dm->win, ExposureMask | KeyPressMask);
  XMapRaised(dm->dpy, dm->win);
}

static void destroy_window(DMenu *dm) {
  XFreeGC(dm->dpy, dm->gc);
  XDestroyWindow(dm->dpy, dm->win);
}

static void read_items(DMenu *dm) {
  char line[4096];
  while (dm->nitems < MAX_ITEMS && fgets(line, sizeof(line), stdin)) {
    int l = strlen(line);
    if (l > 0 && line[l - 1] == '\n') line[l - 1] = '\0';
    if (strlen(line) > 0)
      dm->items[dm->nitems++] = strdup(line);
  }
}

static int try_daemon(void) {
  struct sockaddr_un addr;
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, SOCK_PATH);
  if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(fd); return -1;
  }
  char result[4096];
  int n = read(fd, result, sizeof(result) - 1);
  close(fd);
  if (n > 0) {
    result[n] = '\0';
    if (result[0]) {
      puts(result);
      return 0;
    }
  }
  return 1;
}

static void daemon_serve(DMenu *dm) {
  struct sockaddr_un addr;
  int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
  unlink(SOCK_PATH);
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, SOCK_PATH);
  bind(sfd, (struct sockaddr*)&addr, sizeof(addr));
  listen(sfd, 4);
  MARK("daemon listening");

  for (;;) {
    int cfd = accept(sfd, NULL, NULL);
    if (cfd < 0) continue;
    dm->text[0] = '\0'; dm->cursor = 0; dm->sel = 0; dm->top = 0;
    create_window(dm);
    run(dm, cfd);
    destroy_window(dm);
    close(cfd);
  }
}

static void usage(void) {
  fprintf(stderr, "usage: sdmenu [-d] [-b] [-i] [-l lines] [-m monitor] "
    "[-p prompt] [-fn font] [-nb color] [-nf color] [-sb color] [-sf color]\n");
  exit(1);
}

int main(int argc, char **argv) {
  setlocale(LC_CTYPE, "");
  DMenu dm = {0};
  benchmark = getenv("SDMENU_BENCH") != NULL;
  t0 = ms();

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-')
      switch (argv[i][1]) {
      case 'd': daemon_mode = 1; break;
      case 'b': topbar = 0; break;
      case 'f': if (++i < argc) fontstr = argv[i]; break;
      case 'i': insensitive = 1; break;
      case 'l': if (++i < argc) lines = atoi(argv[i]); break;
      case 'm': if (++i < argc) mon = atoi(argv[i]); break;
      case 'p': if (++i < argc) prompt = argv[i]; break;
      case 'n':
        switch (argv[i][2]) {
        case 'b': if (++i < argc) normbg = argv[i]; break;
        case 'f': if (++i < argc) normfg = argv[i]; break;
        default: usage();
        } break;
      case 's':
        switch (argv[i][2]) {
        case 'b': if (++i < argc) selbg = argv[i]; break;
        case 'f': if (++i < argc) selfg = argv[i]; break;
        default: usage();
        } break;
      default: usage();
      }
    else usage();
  }

  if (!daemon_mode && try_daemon() == 0)
    return 0;

  dm.items = malloc(MAX_ITEMS * sizeof(char *));
  dm.matches = malloc(MAX_ITEMS * sizeof(int));
  read_items(&dm);
  if (dm.nitems == 0) return 1;
  MARK("items read from stdin");

  if (benchmark) {
    const char *tests[] = {"", "a", "ab", "abc", "x", "z", "xy", "open", "fire", NULL};
    for (int t = 0; tests[t]; t++) {
      strcpy(dm.text, tests[t]);
      dm.cursor = strlen(dm.text);
      matchtime = 0;
      int nruns = 1000;
      for (int r = 0; r < nruns; r++) match(&dm);
      fprintf(stderr, "  match '%s': %ld us  (%d/%d items, %d runs)\n",
        tests[t][0] ? tests[t] : "(empty)", matchtime * 1000 / nruns,
        dm.nmatches, dm.nitems, nruns);
    }
  }

  if (init_x11(&dm) < 0) return 1;
  MARK("X11 initialized");

  if (daemon_mode) {
    load_icons(&dm);
    MARK("icons loaded");
    daemon_serve(&dm);
    return 0;
  }

  create_window(&dm);
  MARK("window mapped");
  run(&dm, STDOUT_FILENO);

  destroy_window(&dm);
  XCloseDisplay(dm.dpy);
  for (int i = 0; i < dm.nitems; i++) {
    if (dm.icons && dm.icons[i].loaded)
      XFreePixmap(dm.dpy, dm.icons[i].pixmap);
    free(dm.items[i]);
  }
  free(dm.items);
  free(dm.matches);
  free(dm.icons);
  return dm.sel >= 0 ? 0 : 1;
}
