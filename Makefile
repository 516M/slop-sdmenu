X11_INC = /nix/store/9m0938zahq7kcfzzix4kkpm8d1iz3nmq-libx11-1.8.12-dev/include
XFT_INC = /nix/store/yqqki1sq1hkb15rg7fj4rkl08s32yxqv-libxft-2.3.9-dev/include
XIN_INC = /nix/store/7hhx40b2c2j35f11ms5k6yc5cqblwbmd-libxinerama-1.1.5-dev/include
PROTO_INC = /nix/store/082v1jh8kiyfah8vpw203d7dr8dp94an-xorgproto-2024.1/include
FREETYPE_INC = /nix/store/59j1dqa03j94z2spyargpyb7qmnrh2jq-freetype-2.13.3-dev/include
FONTCONFIG_INC = /nix/store/qrbwgd09fi7bilk7gx4121sm2cxjs55h-fontconfig-2.17.1-dev/include
XRENDER_INC = /nix/store/3rvss3aa0j994jvndf6wbd7llqb6fy3y-libxrender-0.9.12-dev/include
X11_LIB = /nix/store/0d2nplzyyigdjbd9l7s1ka4809zm7pwl-libx11-1.8.12/lib
XFT_LIB = /nix/store/vfl4msjsyqmr4wpv1z0xnvxma362fikm-libxft-2.3.9/lib
XIN_LIB = /nix/store/plc9r597rhbnwc4ip3zzyxndmrd8cb83-libxinerama-1.1.5/lib
CFLAGS = -I$(X11_INC) -I$(XFT_INC) -I$(XIN_INC) -I$(PROTO_INC) -I$(FREETYPE_INC) -I$(FONTCONFIG_INC) -I$(XRENDER_INC)
LDFLAGS = -L$(X11_LIB) -L$(XFT_LIB) -L$(XIN_LIB)

all: sdmenu sdmened

sdmenu: sdmenu.c
	gcc -O3 -flto -march=native -s -o $@ $<

sdmened: sdmened.c
	gcc -O3 -flto -march=native -s $(CFLAGS) -o $@ $< $(LDFLAGS) -lX11 -lXft -lXinerama

clean:
	rm -f sdmenu sdmened
