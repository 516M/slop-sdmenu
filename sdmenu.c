#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/Xinerama.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <locale.h>
#include <unistd.h>

#define INP_MAX 512
#define MAX_ITEMS 4096
#define BH 26
#define PAD 4
#define BORDER 2

static char *fontstr = "monospace:size=12";
static char *prompt = "";
static char *normbg = "#222222";
static char *normfg = "#bbbbbb";
static char *selbg  = "#005577";
static char *selfg  = "#eeeeee";
static int lines = 0;
static int topbar = 1;
static int mon = -1;
static int insensitive = 0;

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
  Display *dpy;
  Window win;
  GC gc;
  XftFont *xfont;
  XftDraw *xdraw;
  XftColor normfg_c, normbg_c, selfg_c, selbg_c;
  Colormap cmap;
  Visual *vis;
  int scr;
  int basex, basey, monh;
} DMenu;

static int textw(DMenu *dm, const char *s, int n) {
  XGlyphInfo ext;
  XftTextExtentsUtf8(dm->dpy, dm->xfont, (const FcChar8 *)s, n, &ext);
  return ext.xOff;
}

static int prefixmatch(const char *item, const char *pat) {
  int n = strlen(pat);
  if (n == 0) return 1;
  if (insensitive)
    return strncasecmp(item, pat, n) == 0;
  return strncmp(item, pat, n) == 0;
}

static void match(DMenu *dm) {
  dm->nmatches = 0;
  for (int i = 0; i < dm->nitems; i++)
    if (prefixmatch(dm->items[i], dm->text))
      dm->matches[dm->nmatches++] = i;
  if (dm->nmatches == 0)
    dm->sel = 0;
  else if (dm->sel >= dm->nmatches)
    dm->sel = dm->nmatches - 1;
}

static void draw(DMenu *dm) {
  int sh = DisplayHeight(dm->dpy, dm->scr);
  int mh = lines > 0 ? lines : (sh - BH) / BH;
  if (mh > dm->nmatches) mh = dm->nmatches;

  int newh = BH + mh * BH + BORDER * 2;
  if (newh != dm->height) {
    dm->height = newh;
    int dy = topbar ? dm->basey : dm->basey + dm->monh - dm->height;
    XMoveResizeWindow(dm->dpy, dm->win, dm->basex, dy, dm->width, dm->height);
  }

  if (dm->sel >= dm->top + mh) dm->top = dm->sel - mh + 1;
  if (dm->sel < dm->top) dm->top = dm->sel;

  XClearWindow(dm->dpy, dm->win);

  int cy = BH;
  XftDrawStringUtf8(dm->xdraw, &dm->normfg_c, dm->xfont, PAD, BH - 6,
    (const FcChar8 *)prompt, strlen(prompt));
  XftDrawStringUtf8(dm->xdraw, &dm->normfg_c, dm->xfont,
    PAD + textw(dm, prompt, strlen(prompt)), BH - 6,
    (const FcChar8 *)dm->text, strlen(dm->text));

  int cx = PAD + textw(dm, prompt, strlen(prompt)) + textw(dm, dm->text, dm->cursor);
  XSetForeground(dm->dpy, dm->gc, dm->normfg_c.pixel);
  XFillRectangle(dm->dpy, dm->win, dm->gc, cx, 3, 2, BH - 6);

  for (int i = 0; i < mh; i++) {
    int idx = dm->top + i;
    int y = cy + i * BH;
    int isel = (idx == dm->sel);
    if (isel) {
      XSetForeground(dm->dpy, dm->gc, dm->selbg_c.pixel);
      XFillRectangle(dm->dpy, dm->win, dm->gc, 0, y, dm->width, BH);
    }
    XftDrawStringUtf8(dm->xdraw, isel ? &dm->selfg_c : &dm->normfg_c,
      dm->xfont, PAD, y + BH - 6,
      (const FcChar8 *)dm->items[dm->matches[idx]],
      strlen(dm->items[dm->matches[idx]]));
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
  int n = dm->cursor - i;
  memmove(dm->text + i, dm->text + dm->cursor, strlen(dm->text) - dm->cursor + 1);
  dm->cursor = i;
}

static void run(DMenu *dm) {
  XEvent ev;
  char buf[32];
  KeySym ks;

  matchanddraw(dm);

  while (1) {
    XNextEvent(dm->dpy, &ev);
    if (ev.type == Expose) { draw(dm); continue; }
    if (ev.type != KeyPress) continue;

    int len = XLookupString(&ev.xkey, buf, sizeof(buf), &ks, NULL);

    if (ks == XK_Escape) { dm->sel = -1; break; }
    if ((ks == XK_Return || ks == XK_KP_Enter) && dm->nmatches > 0) break;

    if (ks == XK_Tab || ks == XK_Down || ks == XK_KP_Down) {
      if (dm->nmatches > 0) {
        dm->sel = (dm->sel + 1) % dm->nmatches;
        draw(dm);
      }
      continue;
    }
    if ((ks == XK_Up || ks == XK_KP_Up) ||
        (ks == XK_Tab && (ev.xkey.state & ShiftMask))) {
      if (dm->nmatches > 0) {
        dm->sel = (dm->sel - 1 + dm->nmatches) % dm->nmatches;
        draw(dm);
      }
      continue;
    }

    if (ks == XK_Left || ks == XK_KP_Left) {
      if (dm->cursor > 0) dm->cursor--;
      draw(dm); continue;
    }
    if (ks == XK_Right || ks == XK_KP_Right) {
      if (dm->cursor < (int)strlen(dm->text)) dm->cursor++;
      draw(dm); continue;
    }
    if (ks == XK_Home || ks == XK_KP_Home) {
      dm->cursor = 0; draw(dm); continue;
    }
    if (ks == XK_End || ks == XK_KP_End) {
      dm->cursor = strlen(dm->text); draw(dm); continue;
    }

    if (ks == XK_BackSpace) {
      if (dm->cursor > 0) {
        memmove(dm->text + dm->cursor - 1, dm->text + dm->cursor,
          strlen(dm->text) - dm->cursor + 1);
        dm->cursor--;
        matchanddraw(dm);
      }
      continue;
    }
    if (ks == XK_Delete) {
      int l = strlen(dm->text);
      if (dm->cursor < l) {
        memmove(dm->text + dm->cursor, dm->text + dm->cursor + 1, l - dm->cursor);
        matchanddraw(dm);
      }
      continue;
    }

    if (len > 0) {
      char c = buf[0];
      switch (c) {
      case '\x01': dm->cursor = 0; draw(dm); continue;
      case '\x02': if (dm->cursor > 0) dm->cursor--; draw(dm); continue;
      case '\x03': dm->sel = -1; goto done;
      case '\x04':
        if (dm->cursor < (int)strlen(dm->text)) {
          memmove(dm->text + dm->cursor, dm->text + dm->cursor + 1,
            strlen(dm->text) - dm->cursor);
          matchanddraw(dm);
        }
        continue;
      case '\x05': dm->cursor = strlen(dm->text); draw(dm); continue;
      case '\x06': if (dm->cursor < (int)strlen(dm->text)) dm->cursor++; draw(dm); continue;
      case '\x07': continue;
      case '\x08':
        if (dm->cursor > 0) {
          memmove(dm->text + dm->cursor - 1, dm->text + dm->cursor,
            strlen(dm->text) - dm->cursor + 1);
          dm->cursor--;
          matchanddraw(dm);
        }
        continue;
      case '\x09':
        if (dm->nmatches > 0) {
          dm->sel = (dm->sel + 1) % dm->nmatches;
          draw(dm);
        }
        continue;
      case '\x0a': case '\x0d': if (dm->nmatches > 0) goto done; continue;
      case '\x0b':
        dm->text[dm->cursor] = '\0';
        matchanddraw(dm);
        continue;
      case '\x0e':
        if (dm->nmatches > 0) {
          dm->sel = (dm->sel + 1) % dm->nmatches;
          draw(dm);
        }
        continue;
      case '\x10':
        if (dm->nmatches > 0) {
          dm->sel = (dm->sel - 1 + dm->nmatches) % dm->nmatches;
          draw(dm);
        }
        continue;
      case '\x15':
        dm->text[0] = '\0'; dm->cursor = 0;
        matchanddraw(dm);
        continue;
      case '\x17':
        killword(dm);
        matchanddraw(dm);
        continue;
      default:
        if (isprint((unsigned char)c)) {
          int l = strlen(dm->text);
          if (l < INP_MAX - 1) {
            memmove(dm->text + dm->cursor + 1, dm->text + dm->cursor,
              l - dm->cursor + 1);
            dm->text[dm->cursor] = c;
            dm->cursor++;
            matchanddraw(dm);
          }
        }
      }
    }
  }
done:
  ;
}

static XftColor initcolor(Display *dpy, Colormap cmap, const char *hex) {
  XftColor c;
  XftColorAllocName(dpy, DefaultVisual(dpy, DefaultScreen(dpy)), cmap, hex, &c);
  return c;
}

static void usage(void) {
  fprintf(stderr, "usage: sdmenu [-b] [-i] [-l lines] [-m monitor] "
    "[-p prompt] [-fn font] [-nb color] [-nf color] [-sb color] [-sf color]\n");
  exit(1);
}

int main(int argc, char **argv) {
  setlocale(LC_CTYPE, "");
  DMenu dm = {0};

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-')
      switch (argv[i][1]) {
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
        }
        break;
      case 's':
        switch (argv[i][2]) {
        case 'b': if (++i < argc) selbg = argv[i]; break;
        case 'f': if (++i < argc) selfg = argv[i]; break;
        default: usage();
        }
        break;
      default: usage();
      }
    else
      usage();
  }

  dm.items = malloc(MAX_ITEMS * sizeof(char *));
  dm.matches = malloc(MAX_ITEMS * sizeof(int));
  char line[4096];
  while (dm.nitems < MAX_ITEMS && fgets(line, sizeof(line), stdin)) {
    int l = strlen(line);
    if (l > 0 && line[l - 1] == '\n') line[l - 1] = '\0';
    if (strlen(line) > 0)
      dm.items[dm.nitems++] = strdup(line);
  }
  if (dm.nitems == 0) return 1;

  dm.dpy = XOpenDisplay(NULL);
  if (!dm.dpy) { fprintf(stderr, "sdmenu: cannot open display\n"); return 1; }
  dm.scr = DefaultScreen(dm.dpy);
  dm.vis = DefaultVisual(dm.dpy, dm.scr);
  dm.cmap = DefaultColormap(dm.dpy, dm.scr);

  dm.xfont = XftFontOpenName(dm.dpy, dm.scr, fontstr);
  if (!dm.xfont) dm.xfont = XftFontOpenName(dm.dpy, dm.scr, "fixed:size=12");
  if (!dm.xfont) { fprintf(stderr, "sdmenu: cannot load font\n"); return 1; }

  dm.normfg_c = initcolor(dm.dpy, dm.cmap, normfg);
  dm.normbg_c = initcolor(dm.dpy, dm.cmap, normbg);
  dm.selfg_c = initcolor(dm.dpy, dm.cmap, selfg);
  dm.selbg_c = initcolor(dm.dpy, dm.cmap, selbg);

  int sw = DisplayWidth(dm.dpy, dm.scr);
  int sh = DisplayHeight(dm.dpy, dm.scr);
  dm.width = sw;

  int mh = lines > 0 ? lines : (sh - BH) / BH;
  if (mh > dm.nitems) mh = dm.nitems;
  dm.height = BH + mh * BH + BORDER * 2;

  int nmon;
  XineramaScreenInfo *info = XineramaQueryScreens(dm.dpy, &nmon);
  if (info) {
    int idx = (mon >= 0 && mon < nmon) ? mon : 0;
    dm.width = info[idx].width;
    dm.basex = info[idx].x_org;
    dm.basey = info[idx].y_org;
    dm.monh = info[idx].height;
    XFree(info);
  } else {
    dm.basex = 0;
    dm.basey = 0;
    dm.monh = sh;
  }
  int x = dm.basex;
  int y = topbar ? dm.basey : dm.basey + dm.monh - dm.height;

  dm.win = XCreateSimpleWindow(dm.dpy, RootWindow(dm.dpy, dm.scr),
    x, y, dm.width, dm.height, BORDER,
    dm.normfg_c.pixel, dm.normbg_c.pixel);
  dm.gc = XCreateGC(dm.dpy, dm.win, 0, NULL);
  dm.xdraw = XftDrawCreate(dm.dpy, dm.win, dm.vis, dm.cmap);

  XSelectInput(dm.dpy, dm.win, ExposureMask | KeyPressMask);
  XMapRaised(dm.dpy, dm.win);

  run(&dm);

  if (dm.sel >= 0 && dm.sel < dm.nmatches)
    puts(dm.items[dm.matches[dm.sel]]);

  XftDrawDestroy(dm.xdraw);
  XFreeGC(dm.dpy, dm.gc);
  XDestroyWindow(dm.dpy, dm.win);
  XCloseDisplay(dm.dpy);
  for (int i = 0; i < dm.nitems; i++) free(dm.items[i]);
  free(dm.items);
  free(dm.matches);
  return dm.sel >= 0 ? 0 : 1;
}
