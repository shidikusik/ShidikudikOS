# Зависимости (Debian): libgtk-3-dev libgtk-layer-shell-dev
PKGS    = gtk+-3.0 gtk-layer-shell-0
CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS  = $(shell pkg-config --libs $(PKGS))
PREFIX ?= /usr/local

all: shidikpanel

shidikpanel: panel.c
	$(CC) $(CFLAGS) -o $@ panel.c $(LDLIBS)

install: shidikpanel
	install -Dm755 shidikpanel $(DESTDIR)$(PREFIX)/bin/shidikpanel

clean:
	rm -f shidikpanel

.PHONY: all install clean
