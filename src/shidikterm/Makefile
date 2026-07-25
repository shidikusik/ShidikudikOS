# Зависимости (Debian): libgtk-3-dev libvte-2.91-dev
PKGS    = gtk+-3.0 vte-2.91
CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS  = $(shell pkg-config --libs $(PKGS))
PREFIX ?= /usr/local

all: shidik-term

shidik-term: term.c
	$(CC) $(CFLAGS) -o $@ term.c $(LDLIBS)

install: shidik-term
	install -Dm755 shidik-term $(DESTDIR)$(PREFIX)/bin/shidik-term
	install -Dm644 shidik-term.desktop \
		$(DESTDIR)/usr/share/applications/shidik-term.desktop

clean:
	rm -f shidik-term

.PHONY: all install clean
