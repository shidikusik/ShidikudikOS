# Зависимости (Debian): libgtk-3-dev libgtk-layer-shell-dev
PKGS    = gtk+-3.0 gtk-layer-shell-0 gio-2.0
CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS  = $(shell pkg-config --libs $(PKGS))
PREFIX ?= /usr/local

all: shidikshade

shidikshade: shade.c
	$(CC) $(CFLAGS) -o $@ shade.c $(LDLIBS)

install: shidikshade
	install -Dm755 shidikshade $(DESTDIR)$(PREFIX)/bin/shidikshade

clean:
	rm -f shidikshade

.PHONY: all install clean
