# shidikwm — сборка через pkg-config.
# Зависимости (Debian 13): libwlroots-0.18-dev libwayland-dev
#                          libxkbcommon-dev wayland-protocols
# Заголовок xdg-shell-protocol.h не поставляется пакетами — он
# генерируется wayland-scanner'ом из XML-описания протокола (как в tinywl).
PKGS    = wlroots-0.18 wayland-server xkbcommon
CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CFLAGS += -DWLR_USE_UNSTABLE -I. $(shell pkg-config --cflags $(PKGS))
LDLIBS  = $(shell pkg-config --libs $(PKGS))
PREFIX ?= /usr/local

WAYLAND_PROTOCOLS = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER   = $(shell pkg-config --variable=wayland_scanner wayland-scanner)

HEADERS = xdg-shell-protocol.h wlr-layer-shell-unstable-v1-protocol.h

all: shidikwm

xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

# XML протокола layer-shell не поставляется пакетами Debian/Ubuntu —
# лежит в репозитории (protocols/, лицензия MIT, проект wlroots).
wlr-layer-shell-unstable-v1-protocol.h: ../../protocols/wlr-layer-shell-unstable-v1.xml
	$(WAYLAND_SCANNER) server-header $< $@

shidikwm: main.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ main.c $(LDLIBS)

install: shidikwm
	install -Dm755 shidikwm $(DESTDIR)$(PREFIX)/bin/shidikwm

clean:
	rm -f shidikwm $(HEADERS)

.PHONY: all install clean
