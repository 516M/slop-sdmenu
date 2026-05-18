X11_INC = /nix/store/9m0938zahq7kcfzzix4kkpm8d1iz3nmq-libx11-1.8.12-dev/include
XIN_INC = /nix/store/7hhx40b2c2j35f11ms5k6yc5cqblwbmd-libxinerama-1.1.5-dev/include
PROTO_INC = /nix/store/082v1jh8kiyfah8vpw203d7dr8dp94an-xorgproto-2024.1/include
X11_LIB = /nix/store/0d2nplzyyigdjbd9l7s1ka4809zm7pwl-libx11-1.8.12/lib
XIN_LIB = /nix/store/plc9r597rhbnwc4ip3zzyxndmrd8cb83-libxinerama-1.1.5/lib

CFLAGS = -I$(X11_INC) -I$(XIN_INC) -I$(PROTO_INC)
LDFLAGS = -L$(X11_LIB) -L$(XIN_LIB)

sdmenu: sdmenu.c
	gcc -O3 -flto -march=native -s $(CFLAGS) -o $@ $< $(LDFLAGS) -lX11 -lXinerama

clean:
	rm -f sdmenu
