# Зависимости (Debian): libgtk-3-dev libgtk-layer-shell-dev libpam0g-dev
PKGS    = gtk+-3.0 gtk-layer-shell-0
CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS  = $(shell pkg-config --libs $(PKGS)) -lpam
PREFIX ?= /usr/local

all: shidiklock

shidiklock: lock.c
	$(CC) $(CFLAGS) -o $@ lock.c $(LDLIBS)

install: shidiklock
	install -Dm755 shidiklock $(DESTDIR)$(PREFIX)/bin/shidiklock
	install -Dm644 shidiklock.pam $(DESTDIR)/etc/pam.d/shidiklock

clean:
	rm -f shidiklock

.PHONY: all install clean
