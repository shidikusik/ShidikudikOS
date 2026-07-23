# shidikwm — сборка через pkg-config.
# Зависимости (Debian 13): libwlroots-0.18-dev libwayland-dev libxkbcommon-dev
PKGS    = wlroots-0.18 wayland-server xkbcommon
CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CFLAGS += -DWLR_USE_UNSTABLE $(shell pkg-config --cflags $(PKGS))
LDLIBS  = $(shell pkg-config --libs $(PKGS))
PREFIX ?= /usr/local

all: shidikwm

shidikwm: main.c
	$(CC) $(CFLAGS) -o $@ main.c $(LDLIBS)

install: shidikwm
	install -Dm755 shidikwm $(DESTDIR)$(PREFIX)/bin/shidikwm

clean:
	rm -f shidikwm

.PHONY: all install clean
