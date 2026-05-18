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

#define MAX_ITEMS 4096
#define INP_MAX 512
#define HEIGHT 24
#define PAD 4
#define BORDER 2
#define FG_COLOR "#cccccc"
#define BG_COLOR "#222222"
#define SEL_COLOR "#444444"
#define PROMPT "> "

typedef struct {
  char **items;
  int nitems;
  int *matches;
  int nmatches;
  char input[INP_MAX];
  int cursor;
  int sel;
  int top;
  int width;
  int height;
  Display *dpy;
  Window win;
  GC gc;
  XftFont *font;
  XftDraw *draw;
  XftColor fgc, bgc, selc;
  Colormap cmap;
  Visual *vis;
} DMenu;

static int strmatch(const char *s, const char *pat) {
  while (*pat) {
    if (tolower((unsigned char)*s) != tolower((unsigned char)*pat))
      return 0;
    s++; pat++;
  }
  return 1;
}

static void filter(DMenu *dm) {
  dm->nmatches = 0;
  int len = strlen(dm->input);
  for (int i = 0; i < dm->nitems; i++) {
    int match = 0;
    const char *p = dm->items[i];
    while (*p) {
      if (strmatch(p, dm->input)) { match = 1; break; }
      p++;
    }
    if (match || len == 0)
      dm->matches[dm->nmatches++] = i;
  }
  if (dm->sel >= dm->nmatches) dm->sel = dm->nmatches > 0 ? dm->nmatches - 1 : 0;
  int mh = (dm->height - HEIGHT) / (HEIGHT - 1);
  if (dm->sel >= dm->top + mh) dm->top = dm->sel - mh + 1;
  if (dm->sel < dm->top) dm->top = dm->sel;
}

static int textw(DMenu *dm, const char *s) {
  XGlyphInfo ext;
  XftTextExtentsUtf8(dm->dpy, dm->font, (const FcChar8 *)s, strlen(s), &ext);
  return ext.xOff;
}

static void draw(DMenu *dm) {
  XClearWindow(dm->dpy, dm->win);
  int mh = (dm->height - HEIGHT) / (HEIGHT - 1);
  int visible = dm->nmatches < mh ? dm->nmatches : mh;

  // Prompt + input
  char buf[INP_MAX + 8];
  snprintf(buf, sizeof(buf), "%s%s", PROMPT, dm->input);
  XftDrawStringUtf8(dm->draw, &dm->fgc, dm->font, PAD, HEIGHT - 6, (const FcChar8 *)buf, strlen(buf));

  // Draw matches
  for (int i = 0; i < visible; i++) {
    int idx = dm->top + i;
    if (idx >= dm->nmatches) break;
    int y = HEIGHT + i * (HEIGHT - 1);
    int isel = (idx == dm->sel);
    if (isel) {
      XSetForeground(dm->dpy, dm->gc, dm->selc.pixel);
      XFillRectangle(dm->dpy, dm->win, dm->gc, 0, y - (HEIGHT - 1) + 2, dm->width, HEIGHT - 2);
    }
    XftDrawStringUtf8(dm->draw, isel ? &dm->fgc : &dm->fgc, dm->font, PAD,
                      y + HEIGHT - 8, (const FcChar8 *)dm->items[dm->matches[idx]],
                      strlen(dm->items[dm->matches[idx]]));
  }
}

static void run(DMenu *dm) {
  XEvent ev;
  char buf[32];
  KeySym ks;

  while (1) {
    XNextEvent(dm->dpy, &ev);
    if (ev.type == KeyPress) {
      int len = XLookupString(&ev.xkey, buf, sizeof(buf), &ks, NULL);
      if (ks == XK_Escape) { dm->sel = -1; break; }
      if (ks == XK_Return && dm->nmatches > 0) break;
      if (ks == XK_Tab || ks == XK_Down) {
        if (dm->nmatches > 0) {
          dm->sel = (dm->sel + 1) % dm->nmatches;
          filter(dm);
          draw(dm);
        }
        continue;
      }
      if (ks == XK_Up) {
        if (dm->nmatches > 0) {
          dm->sel = (dm->sel - 1 + dm->nmatches) % dm->nmatches;
          filter(dm);
          draw(dm);
        }
        continue;
      }
      if (ks == XK_BackSpace) {
        int l = strlen(dm->input);
        if (l > 0 && dm->cursor > 0) {
          memmove(dm->input + dm->cursor - 1, dm->input + dm->cursor, l - dm->cursor + 1);
          dm->cursor--;
          filter(dm);
          draw(dm);
        }
        continue;
      }
      if (ks == XK_Delete) {
        int l = strlen(dm->input);
        if (dm->cursor < l) {
          memmove(dm->input + dm->cursor, dm->input + dm->cursor + 1, l - dm->cursor);
          filter(dm);
          draw(dm);
        }
        continue;
      }
      if (ks == XK_Left) {
        if (dm->cursor > 0) dm->cursor--;
        continue;
      }
      if (ks == XK_Right) {
        if (dm->cursor < (int)strlen(dm->input)) dm->cursor++;
        continue;
      }
      if (ks == XK_End) {
        dm->cursor = strlen(dm->input);
        continue;
      }
      if (ks == XK_Home) {
        dm->cursor = 0;
        continue;
      }
      if (len > 0 && isprint((unsigned char)buf[0])) {
        int l = strlen(dm->input);
        if (l < INP_MAX - 1) {
          memmove(dm->input + dm->cursor + 1, dm->input + dm->cursor, l - dm->cursor + 1);
          dm->input[dm->cursor] = buf[0];
          dm->cursor++;
          filter(dm);
          draw(dm);
        }
      }
    }
    if (ev.type == Expose) draw(dm);
  }
}

static XftColor initcolor(Display *dpy, Colormap cmap, const char *hex) {
  XftColor c;
  XftColorAllocName(dpy, DefaultVisual(dpy, DefaultScreen(dpy)), cmap, hex, &c);
  return c;
}

int main(int argc, char **argv) {
  setlocale(LC_CTYPE, "");
  DMenu dm = {0};

  // Read items from stdin
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

  // Init X11
  dm.dpy = XOpenDisplay(NULL);
  if (!dm.dpy) { fprintf(stderr, "Cannot open display\n"); return 1; }
  int scr = DefaultScreen(dm.dpy);
  dm.vis = DefaultVisual(dm.dpy, scr);
  dm.cmap = DefaultColormap(dm.dpy, scr);

  // Font
  dm.font = XftFontOpenName(dm.dpy, scr, "monospace:size=12:antialias=true");
  if (!dm.font) dm.font = XftFontOpenName(dm.dpy, scr, "fixed:size=12");
  if (!dm.font) { fprintf(stderr, "Cannot load font\n"); return 1; }

  // Colors
  dm.fgc = initcolor(dm.dpy, dm.cmap, FG_COLOR);
  dm.bgc = initcolor(dm.dpy, dm.cmap, BG_COLOR);
  dm.selc = initcolor(dm.dpy, dm.cmap, SEL_COLOR);

  // Window dimensions
  dm.width = 800;

  int mh = dm.nitems < 10 ? dm.nitems : 10;
  dm.height = HEIGHT + mh * (HEIGHT - 1) + BORDER * 2;

  // Position on primary monitor
  int x = 0, y = 0;
  int nmon;
  XineramaScreenInfo *info = XineramaQueryScreens(dm.dpy, &nmon);
  if (info) {
    int w = dm.width > info[0].width ? info[0].width : dm.width;
    x = info[0].x_org + (info[0].width - w) / 2;
    y = info[0].y_org;
    dm.width = w;
    XFree(info);
  } else {
    dm.width = dm.width > DisplayWidth(dm.dpy, scr) ? DisplayWidth(dm.dpy, scr) : dm.width;
    x = (DisplayWidth(dm.dpy, scr) - dm.width) / 2;
  }

  dm.win = XCreateSimpleWindow(dm.dpy, RootWindow(dm.dpy, scr), x, y, dm.width, dm.height, BORDER,
                               dm.fgc.pixel, dm.bgc.pixel);
  dm.gc = XCreateGC(dm.dpy, dm.win, 0, NULL);
  dm.draw = XftDrawCreate(dm.dpy, dm.win, dm.vis, dm.cmap);

  XSelectInput(dm.dpy, dm.win, ExposureMask | KeyPressMask);
  XMapRaised(dm.dpy, dm.win);

  filter(&dm);
  run(&dm);

  // Output selection
  if (dm.sel >= 0 && dm.sel < dm.nmatches)
    puts(dm.items[dm.matches[dm.sel]]);

  // Cleanup
  XftDrawDestroy(dm.draw);
  XFreeGC(dm.dpy, dm.gc);
  XDestroyWindow(dm.dpy, dm.win);
  XCloseDisplay(dm.dpy);
  for (int i = 0; i < dm.nitems; i++) free(dm.items[i]);
  free(dm.items);
  free(dm.matches);
  return dm.sel >= 0 ? 0 : 1;
}
