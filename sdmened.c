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
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <signal.h>
#include <fcntl.h>

#define INP_MAX 512
#define MAX_ITEMS 4096
#define PAD 2
#define BORDER 0
#define SOCK_PATH "/tmp/sdmened.sock"
#define ICON_SIZE 24

static char *fontstr = "Inconsolata LGC Markup:bold:size=14";
static char *prompt = ">";
static char *normbg = "#0d0d0d";
static char *normfg = "#c8c8c8";
static char *selbg  = "#2a2a2a";
static char *selfg  = "#ffffff";
static int lines = 0;
static int topbar = 1;
static int mon = -1;
static int insensitive = 0;
static int benchmark = 0;
static long t0 = 0;

static long ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}
#define MARK(msg) do { if (benchmark) \
  fprintf(stderr, "  %3ld ms  %s\n", ms() - t0, msg); } while (0)

typedef struct { Pixmap pixmap; int loaded; } Icon;

typedef struct {
  char **items; int nitems; int *matches; int nmatches;
  char text[INP_MAX]; int cursor; int sel; int top;
  int width; int height; int promptw; int maxvis;
  int fw, fh, BH; Icon *icons;
  Display *dpy; Window win; GC gc;
  XftFont *xfont; XftDraw *xdraw;
  XftColor normfg_c, normbg_c, selfg_c, selbg_c;
  unsigned long normfg_p, normbg_p, selfg_p, selbg_p;
  Colormap cmap; int scr; int basex, basey, monh;
} DMenu;

static int textw(DMenu *dm, const char *s, int n) {
  XGlyphInfo e; XftTextExtentsUtf8(dm->dpy, dm->xfont, (const FcChar8*)s, n, &e); return e.xOff;
}

static int prefixmatch(const char *item, const char *pat) {
  int n = strlen(pat);
  if (n == 0) return 1;
  return insensitive ? strncasecmp(item, pat, n) == 0 : strncmp(item, pat, n) == 0;
}

static long matchtime = 0;

static void match(DMenu *dm) {
  long mt0 = benchmark ? ms() : 0; dm->nmatches = 0;
  for (int i = 0; i < dm->nitems; i++)
    if (prefixmatch(dm->items[i], dm->text)) dm->matches[dm->nmatches++] = i;
  if (dm->nmatches == 0) dm->sel = 0;
  else if (dm->sel >= dm->nmatches) dm->sel = dm->nmatches - 1;
  matchtime += ms() - mt0;
}

static void draw(DMenu *dm) {
  int mh = dm->maxvis; if (mh > dm->nmatches) mh = dm->nmatches;
  if (dm->sel >= dm->top + mh) dm->top = dm->sel - mh + 1;
  if (dm->sel < dm->top) dm->top = dm->sel;
  XSetForeground(dm->dpy, dm->gc, dm->normbg_p);
  XFillRectangle(dm->dpy, dm->win, dm->gc, 0, 0, dm->width, dm->height);
  XSetForeground(dm->dpy, dm->gc, dm->normfg_p);
  int by = dm->xfont->ascent + 1;
  XftDrawStringUtf8(dm->xdraw, &dm->normfg_c, dm->xfont, PAD, by, (const FcChar8*)prompt, strlen(prompt));
  XftDrawStringUtf8(dm->xdraw, &dm->normfg_c, dm->xfont, PAD + dm->promptw, by, (const FcChar8*)dm->text, strlen(dm->text));
  int cx = PAD + dm->promptw + textw(dm, dm->text, dm->cursor);
  XFillRectangle(dm->dpy, dm->win, dm->gc, cx, (dm->BH-dm->fh)/2+1, 2, dm->fh-2);
  int to = PAD + ICON_SIZE + 4;
  for (int i = 0; i < mh; i++) {
    int idx = dm->top + i, y = dm->BH + i * dm->BH;
    int ty = y + (dm->BH + dm->xfont->ascent - dm->xfont->descent) / 2;
    if (idx == dm->sel) { XSetForeground(dm->dpy, dm->gc, dm->selbg_p); XFillRectangle(dm->dpy, dm->win, dm->gc, 0, y, dm->width, dm->BH); XSetForeground(dm->dpy, dm->gc, dm->selfg_p); }
    else { XSetForeground(dm->dpy, dm->gc, dm->normfg_p); }
    if (dm->icons && dm->icons[dm->matches[idx]].loaded)
      XCopyArea(dm->dpy, dm->icons[dm->matches[idx]].pixmap, dm->win, dm->gc, 0, 0, ICON_SIZE, ICON_SIZE, PAD, y + (dm->BH - ICON_SIZE) / 2);
    XftDrawStringUtf8(dm->xdraw, (idx==dm->sel)?&dm->selfg_c:&dm->normfg_c, dm->xfont, to, ty, (const FcChar8*)dm->items[dm->matches[idx]], strlen(dm->items[dm->matches[idx]]));
  }
}

static void matchanddraw(DMenu *dm) { match(dm); draw(dm); }

static void killword(DMenu *dm) {
  int i = dm->cursor;
  while (i > 0 && dm->text[i-1] == ' ') i--;
  while (i > 0 && dm->text[i-1] != ' ') i--;
  memmove(dm->text + i, dm->text + dm->cursor, strlen(dm->text) - dm->cursor + 1);
  dm->cursor = i;
}

static void run(DMenu *dm, int out_fd) {
  XEvent ev; char buf[32]; KeySym ks;
  matchanddraw(dm); XFlush(dm->dpy); XSync(dm->dpy, False);
  for (int g = 0; g < 10; g++) {
    if (XGrabKeyboard(dm->dpy, dm->win, False, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess) break;
    usleep(10000);
  }
  while (1) {
    XNextEvent(dm->dpy, &ev);
    if (ev.type == Expose) { draw(dm); XFlush(dm->dpy); continue; }
    if (ev.type != KeyPress) continue;
    int len = XLookupString(&ev.xkey, buf, sizeof(buf), &ks, NULL);
    if (ks == XK_Escape) { dm->sel = -1; break; }
    if ((ks == XK_Return || ks == XK_KP_Enter) && dm->nmatches > 0) break;
    if (ks == XK_Tab || ks == XK_Down || ks == XK_KP_Down) { if (dm->nmatches > 0) { dm->sel = (dm->sel + 1) % dm->nmatches; draw(dm); XFlush(dm->dpy); } continue; }
    if ((ks == XK_Up || ks == XK_KP_Up) || (ks == XK_Tab && (ev.xkey.state & ShiftMask))) { if (dm->nmatches > 0) { dm->sel = (dm->sel - 1 + dm->nmatches) % dm->nmatches; draw(dm); XFlush(dm->dpy); } continue; }
    if (ks == XK_Left || ks == XK_KP_Left) { if (dm->cursor > 0) { dm->cursor--; draw(dm); XFlush(dm->dpy); } continue; }
    if (ks == XK_Right || ks == XK_KP_Right) { int l = strlen(dm->text); if (dm->cursor < l) { dm->cursor++; draw(dm); XFlush(dm->dpy); } continue; }
    if (ks == XK_Home || ks == XK_KP_Home) { dm->cursor = 0; draw(dm); XFlush(dm->dpy); continue; }
    if (ks == XK_End || ks == XK_KP_End) { dm->cursor = strlen(dm->text); draw(dm); XFlush(dm->dpy); continue; }
    if (ks == XK_BackSpace) { if (dm->cursor > 0) { memmove(dm->text+dm->cursor-1, dm->text+dm->cursor, strlen(dm->text)-dm->cursor+1); dm->cursor--; matchanddraw(dm); XFlush(dm->dpy); } continue; }
    if (ks == XK_Delete) { int l = strlen(dm->text); if (dm->cursor < l) { memmove(dm->text+dm->cursor, dm->text+dm->cursor+1, l-dm->cursor); matchanddraw(dm); XFlush(dm->dpy); } continue; }
    if (len > 0) switch (buf[0]) {
      case 1: dm->cursor = 0; draw(dm); XFlush(dm->dpy); continue;
      case 2: if (dm->cursor > 0) { dm->cursor--; draw(dm); XFlush(dm->dpy); } continue;
      case 3: dm->sel = -1; goto done;
      case 4: if (dm->cursor < (int)strlen(dm->text)) { memmove(dm->text+dm->cursor, dm->text+dm->cursor+1, strlen(dm->text)-dm->cursor); matchanddraw(dm); XFlush(dm->dpy); } continue;
      case 5: dm->cursor = strlen(dm->text); draw(dm); XFlush(dm->dpy); continue;
      case 6: if (dm->cursor < (int)strlen(dm->text)) { dm->cursor++; draw(dm); XFlush(dm->dpy); } continue;
      case 7: continue;
      case 8: if (dm->cursor > 0) { memmove(dm->text+dm->cursor-1, dm->text+dm->cursor, strlen(dm->text)-dm->cursor+1); dm->cursor--; matchanddraw(dm); XFlush(dm->dpy); } continue;
      case 9: if (dm->nmatches > 0) { dm->sel = (dm->sel+1)%dm->nmatches; draw(dm); XFlush(dm->dpy); } continue;
      case 10: case 13: if (dm->nmatches > 0) goto done; continue;
      case 11: dm->text[dm->cursor] = 0; matchanddraw(dm); XFlush(dm->dpy); continue;
      case 14: if (dm->nmatches > 0) { dm->sel = (dm->sel+1)%dm->nmatches; draw(dm); XFlush(dm->dpy); } continue;
      case 16: if (dm->nmatches > 0) { dm->sel = (dm->sel-1+dm->nmatches)%dm->nmatches; draw(dm); XFlush(dm->dpy); } continue;
      case 21: dm->text[0] = 0; dm->cursor = 0; matchanddraw(dm); XFlush(dm->dpy); continue;
      case 23: killword(dm); matchanddraw(dm); XFlush(dm->dpy); continue;
      default:
        if (isprint((unsigned char)buf[0])) { int l = strlen(dm->text); if (l < INP_MAX-1) { memmove(dm->text+dm->cursor+1, dm->text+dm->cursor, l-dm->cursor+1); dm->text[dm->cursor] = buf[0]; dm->cursor++; matchanddraw(dm); XFlush(dm->dpy); } }
    }
  }
done:
  XUngrabKeyboard(dm->dpy, CurrentTime); XSync(dm->dpy, False);
  if (dm->sel >= 0 && dm->sel < dm->nmatches) write(out_fd, dm->items[dm->matches[dm->sel]], strlen(dm->items[dm->matches[dm->sel]]) + 1);
  if (benchmark) fprintf(stderr, "  %3ld ms  total match time\n", matchtime);
}

static int init_x11(DMenu *dm) {
  dm->dpy = XOpenDisplay(NULL); if (!dm->dpy) { fprintf(stderr, "sdmened: cannot open display\n"); return -1; }
  dm->scr = DefaultScreen(dm->dpy);
  dm->xfont = XftFontOpenName(dm->dpy, dm->scr, fontstr);
  if (!dm->xfont) dm->xfont = XftFontOpenName(dm->dpy, dm->scr, "monospace:bold:size=14");
  if (!dm->xfont) dm->xfont = XftFontOpenName(dm->dpy, dm->scr, "fixed:size=14");
  if (!dm->xfont) { fprintf(stderr, "sdmened: cannot load font\n"); return -1; }
  dm->fw = dm->xfont->max_advance_width; dm->fh = dm->xfont->ascent + dm->xfont->descent;
  dm->BH = dm->fh;
  dm->cmap = DefaultColormap(dm->dpy, dm->scr);
  XftColorAllocName(dm->dpy, DefaultVisual(dm->dpy, dm->scr), dm->cmap, normfg, &dm->normfg_c);
  dm->normfg_p = dm->normfg_c.pixel;
  XftColorAllocName(dm->dpy, DefaultVisual(dm->dpy, dm->scr), dm->cmap, normbg, &dm->normbg_c);
  dm->normbg_p = dm->normbg_c.pixel;
  XftColorAllocName(dm->dpy, DefaultVisual(dm->dpy, dm->scr), dm->cmap, selfg, &dm->selfg_c);
  dm->selfg_p = dm->selfg_c.pixel;
  XftColorAllocName(dm->dpy, DefaultVisual(dm->dpy, dm->scr), dm->cmap, selbg, &dm->selbg_c);
  dm->selbg_p = dm->selbg_c.pixel;
  dm->promptw = textw(dm, prompt, strlen(prompt)); return 0;
}

static Pixmap ppm_to_pixmap(DMenu *dm, const char *path) {
  char cmd[4096]; snprintf(cmd, sizeof(cmd), "convert '%s' -resize %dx%d ppm:- 2>/dev/null", path, ICON_SIZE, ICON_SIZE);
  FILE *fp = popen(cmd, "r"); if (!fp) return 0;
  char buf[256]; if (!fgets(buf, sizeof(buf), fp)) { pclose(fp); return 0; }
  if (buf[0] != 'P' || buf[1] != '6') { pclose(fp); return 0; }
  do { if (!fgets(buf, sizeof(buf), fp)) { pclose(fp); return 0; } } while (buf[0] == '#');
  int w, h; sscanf(buf, "%d %d", &w, &h);
  if (!fgets(buf, sizeof(buf), fp)) { pclose(fp); return 0; }
  int rb = w * 3; unsigned char *rgb = malloc(h * rb);
  for (int y = 0; y < h; y++) if (fread(rgb + y * rb, 1, rb, fp) != (size_t)rb) { free(rgb); pclose(fp); return 0; }
  pclose(fp);
  int dep = DefaultDepth(dm->dpy, dm->scr);
  XImage *img = XCreateImage(dm->dpy, DefaultVisual(dm->dpy, dm->scr), dep, ZPixmap, 0, NULL, w, h, 32, 0);
  if (!img) { free(rgb); return 0; }
  img->data = calloc(h, img->bytes_per_line);
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    unsigned char r = rgb[(y*w+x)*3], g = rgb[(y*w+x)*3+1], b = rgb[(y*w+x)*3+2];
    unsigned long px = (dep>=24)?(r<<16)|(g<<8)|b:(dep==16)?((r>>3)<<11)|((g>>2)<<5)|(b>>3):((r>>5)<<5)|((g>>5)<<2)|(b>>6);
    long d = y * img->bytes_per_line + x * (img->bits_per_pixel/8);
    if (img->byte_order == LSBFirst) {
      img->data[d] = px & 0xFF; if (img->bits_per_pixel >= 16) img->data[d+1] = (px>>8)&0xFF; if (img->bits_per_pixel >= 24) img->data[d+2] = (px>>16)&0xFF;
    } else {
      if (img->bits_per_pixel >= 24) img->data[d] = (px>>16)&0xFF; if (img->bits_per_pixel >= 16) img->data[d+1] = (px>>8)&0xFF; img->data[d+2] = px & 0xFF;
    }
  }
  free(rgb); Pixmap pm = XCreatePixmap(dm->dpy, RootWindow(dm->dpy, dm->scr), w, h, dep);
  GC gc = XCreateGC(dm->dpy, pm, 0, NULL); XPutImage(dm->dpy, pm, gc, img, 0, 0, 0, 0, w, h);
  XFreeGC(dm->dpy, gc); free(img->data); img->data = NULL; XDestroyImage(img); return pm;
}

static void load_icons(DMenu *dm) {
  dm->icons = calloc(dm->nitems, sizeof(Icon));
  DIR *dir = opendir("/run/current-system/sw/share/applications"); if (!dir) return;
  struct dirent *e; int nd = 0; char **dn = NULL, **di = NULL;
  while ((e = readdir(dir))) {
    int l = strlen(e->d_name); if (l < 9 || strcmp(e->d_name + l - 8, ".desktop")) continue;
    char p[4096]; snprintf(p, sizeof(p), "/run/current-system/sw/share/applications/%s", e->d_name);
    FILE *fp = fopen(p, "r"); if (!fp) continue;
    char *ic = NULL, ln[1024];
    while (fgets(ln, sizeof(ln), fp)) { if (strncmp(ln, "Icon=", 5) == 0) { free(ic); ic = strdup(ln+5); char *nl = strchr(ic, '\n'); if (nl) *nl = 0; } }
    fclose(fp); if (!ic) continue;
    char bn[256]; strncpy(bn, e->d_name, l-8); bn[l-8] = 0;
    dn = realloc(dn, (nd+1)*sizeof(char*)); di = realloc(di, (nd+1)*sizeof(char*));
    dn[nd] = strdup(bn); di[nd] = ic; nd++;
  }
  closedir(dir); if (nd == 0) return;
  for (int i = 0; i < dm->nitems; i++) for (int d = 0; d < nd; d++) if (strcmp(dm->items[i], dn[d]) == 0) {
    char *sz[] = {"48x48","32x32","24x24","64x64","96x96","128x128"};
    for (int s = 0; s < 6; s++) { char ip[4096]; snprintf(ip, sizeof(ip), "/run/current-system/sw/share/icons/hicolor/%s/apps/%s.png", sz[s], di[d]); FILE *t = fopen(ip, "r"); if (t) { fclose(t); Pixmap pm = ppm_to_pixmap(dm, ip); if (pm) { dm->icons[i].pixmap = pm; dm->icons[i].loaded = 1; } break; } }
    break;
  }
  for (int d = 0; d < nd; d++) { free(dn[d]); free(di[d]); } free(dn); free(di);
}

static void create_window(DMenu *dm) {
  int sw = DisplayWidth(dm->dpy,dm->scr), sh = DisplayHeight(dm->dpy,dm->scr), mw = sw;
  int nmon;
  XineramaScreenInfo *info = XineramaQueryScreens(dm->dpy, &nmon);
  if (info) { int idx = mon>=0&&mon<nmon?mon:0; mw=info[idx].width; dm->basex=info[idx].x_org; dm->basey=info[idx].y_org; dm->monh=info[idx].height; }
  else { dm->basex=0; dm->basey=0; dm->monh=sh; }
  dm->width = mw;
  dm->maxvis = lines>0?lines:0; if(dm->maxvis>dm->nitems)dm->maxvis=dm->nitems;
  dm->height = dm->BH + dm->maxvis*dm->BH;
  int x = dm->basex+(mw-dm->width)/2, y = topbar?dm->basey:dm->basey+dm->monh-dm->height;
  if (info) XFree(info);
  XSetWindowAttributes wa={0}; wa.override_redirect=1; wa.border_pixel=dm->normfg_p; wa.background_pixel=dm->normbg_p; wa.event_mask=ExposureMask|KeyPressMask;
  dm->win = XCreateWindow(dm->dpy,RootWindow(dm->dpy,dm->scr),x,y,dm->width,dm->height,0,CopyFromParent,InputOutput,CopyFromParent,CWOverrideRedirect|CWBorderPixel|CWBackPixel|CWEventMask,&wa);
  dm->gc = XCreateGC(dm->dpy,dm->win,0,NULL); dm->xdraw = XftDrawCreate(dm->dpy,dm->win,DefaultVisual(dm->dpy,dm->scr),dm->cmap); XMapRaised(dm->dpy,dm->win);
}

static void destroy_window(DMenu *dm) {
  XUnmapWindow(dm->dpy, dm->win);
  XSync(dm->dpy, False);
  XftDrawDestroy(dm->xdraw);
  XFreeGC(dm->dpy, dm->gc);
  XDestroyWindow(dm->dpy, dm->win);
}

static int cmpstr(const void *a, const void *b) { return strcmp(*(const char**)a, *(const char**)b); }

static void gen_items(DMenu *dm) {
  char *path = getenv("PATH"); if(!path)return;
  char b[4096]; strncpy(b,path,sizeof(b)-1); b[sizeof(b)-1]=0; char *d=b;
  while(d&&*d){char*n=strchr(d,':');if(n)*n++=0; DIR*dir=opendir(d);if(dir){struct dirent*e;while((e=readdir(dir))&&dm->nitems<MAX_ITEMS){if(e->d_name[0]=='.')continue;char f[4096];snprintf(f,sizeof(f),"%s/%s",d,e->d_name);struct stat st;if(stat(f,&st)==0&&S_ISREG(st.st_mode)&&(st.st_mode&S_IXUSR)){int dp=0;for(int i=0;i<dm->nitems;i++)if(strcmp(dm->items[i],e->d_name)==0){dp=1;break;}if(!dp)dm->items[dm->nitems++]=strdup(e->d_name);}}closedir(dir);}d=n;}
  qsort(dm->items,dm->nitems,sizeof(char*),cmpstr);
}

static int stdin_has_data(void) { struct stat st; fstat(0,&st); return !S_ISCHR(st.st_mode); }

static int cache_valid(const char *cache) {
  struct stat ca; if(stat(cache,&ca)<0)return 0;
  char*path=getenv("PATH");if(!path)return 0;
  char b[4096];strncpy(b,path,sizeof(b)-1);b[sizeof(b)-1]=0;char*d=b;
  while(d&&*d){char*n=strchr(d,':');if(n)*n++=0;struct stat ds;if(stat(d,&ds)==0&&ds.st_mtime>ca.st_mtime)return 0;d=n;}
  return 1;
}

static void read_items(DMenu *dm) {
  if(!stdin_has_data()){
    const char*ch=getenv("XDG_CACHE_HOME");char ca[4096];
    if(ch)snprintf(ca,sizeof(ca),"%s/sdmenu_items",ch);
    else snprintf(ca,sizeof(ca),"%s/.cache/sdmenu_items",getenv("HOME")?getenv("HOME"):"/tmp");
    if(cache_valid(ca)){FILE*fp=fopen(ca,"r");if(fp){char l[4096];while(dm->nitems<MAX_ITEMS&&fgets(l,sizeof(l),fp)){int ln=strlen(l);if(ln>0&&l[ln-1]=='\n')l[ln-1]=0;if(strlen(l)>0)dm->items[dm->nitems++]=strdup(l);}fclose(fp);if(dm->nitems>0)return;}}
    gen_items(dm);FILE*fp=fopen(ca,"w");if(fp){for(int i=0;i<dm->nitems;i++)fprintf(fp,"%s\n",dm->items[i]);fclose(fp);}return;
  }
  char l[4096];while(dm->nitems<MAX_ITEMS&&fgets(l,sizeof(l),stdin)){int ln=strlen(l);if(ln>0&&l[ln-1]=='\n')l[ln-1]=0;if(strlen(l)>0)dm->items[dm->nitems++]=strdup(l);}
}

static int alive(void) {
  FILE*pf=fopen("/tmp/sdmened.pid","r");if(!pf)return 0;
  int pid; if(fscanf(pf,"%d",&pid)!=1){fclose(pf);return 0;} fclose(pf);
  return kill(pid,0)==0;
}

static int daemon_sfd = -1;

static void daemon_serve(DMenu *dm) {
  for(;;){
    int cfd=accept(daemon_sfd,NULL,NULL);
    if(cfd<0) continue;
    dm->text[0]=0; dm->cursor=0; dm->sel=0; dm->top=0;
    create_window(dm);
    run(dm,cfd);
    destroy_window(dm);
    XSync(dm->dpy,False);
    close(cfd);
  }
}

static void usage(void) {
  fprintf(stderr,"usage: sdmened [-b] [-i] [-l lines] [-m monitor] [-p prompt] [-fn font] [-nb color] [-nf color] [-sb color] [-sf color]\n");exit(1);
}

int main(int argc, char **argv) {
  setlocale(LC_CTYPE,""); DMenu dm={0};
  benchmark=getenv("SDMENU_BENCH")!=NULL; t0=ms();
  for(int i=1;i<argc;i++){
    if(argv[i][0]=='-')switch(argv[i][1]){
      case'b':topbar=0;break;
      case'f':if(++i<argc)fontstr=argv[i];break;
      case'i':insensitive=1;break;
      case'l':if(++i<argc)lines=atoi(argv[i]);break;
      case'm':if(++i<argc)mon=atoi(argv[i]);break;
      case'p':if(++i<argc)prompt=argv[i];break;
      case'n':switch(argv[i][2]){case'b':if(++i<argc)normbg=argv[i];break;case'f':if(++i<argc)normfg=argv[i];break;default:usage();}break;
      case's':switch(argv[i][2]){case'b':if(++i<argc)selbg=argv[i];break;case'f':if(++i<argc)selfg=argv[i];break;default:usage();}break;
      default:usage();
    }else usage();
  }
  if(alive())return 0;
  signal(SIGPIPE,SIG_IGN);
  prctl(PR_SET_NAME,"sdmened");

  unlink(SOCK_PATH);
  struct sockaddr_un sa; sa.sun_family=AF_UNIX; strcpy(sa.sun_path,SOCK_PATH);
  daemon_sfd=socket(AF_UNIX,SOCK_STREAM,0); bind(daemon_sfd,(struct sockaddr*)&sa,sizeof(sa)); listen(daemon_sfd,4);

  FILE*pf=fopen("/tmp/sdmened.pid","w");if(pf){fprintf(pf,"%d\n",getpid());fclose(pf);}

  dm.items=malloc(MAX_ITEMS*sizeof(char*));dm.matches=malloc(MAX_ITEMS*sizeof(int));
  read_items(&dm);if(dm.nitems==0)return 1;MARK("items read from stdin");
  if(init_x11(&dm)<0)return 1;MARK("X11 initialized");
  daemon_serve(&dm);
  return 0;
}
