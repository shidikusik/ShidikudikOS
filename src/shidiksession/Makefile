# Зависимости (Debian): libsystemd-dev
PKGS    = libsystemd
CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS  = $(shell pkg-config --libs $(PKGS))
PREFIX ?= /usr/local

all: shidik-session-ctl

shidik-session-ctl: session-ctl.c
	$(CC) $(CFLAGS) -o $@ session-ctl.c $(LDLIBS)

install: shidik-session-ctl
	install -Dm755 shidik-session-ctl $(DESTDIR)$(PREFIX)/bin/shidik-session-ctl
	install -Dm755 ../../session/shidikde-session $(DESTDIR)$(PREFIX)/bin/shidikde-session
	install -Dm755 ../../session/shidikde-autostart $(DESTDIR)$(PREFIX)/bin/shidikde-autostart
	install -Dm644 ../../session/shidikde.desktop $(DESTDIR)/usr/share/wayland-sessions/shidikde.desktop

clean:
	rm -f shidik-session-ctl

.PHONY: all install clean
