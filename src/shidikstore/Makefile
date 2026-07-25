# Зависимости (Debian): libgtk-3-dev
PKGS    = gtk+-3.0
CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS  = $(shell pkg-config --libs $(PKGS))
PREFIX ?= /usr/local

all: shidik-store

shidik-store: store.c
	$(CC) $(CFLAGS) -o $@ store.c $(LDLIBS)

install: shidik-store
	install -Dm755 shidik-store $(DESTDIR)$(PREFIX)/bin/shidik-store
	install -Dm644 shidik-store.desktop \
		$(DESTDIR)/usr/share/applications/shidik-store.desktop
	install -Dm644 store-catalog.ini \
		$(DESTDIR)/usr/share/shidikusik/store-catalog.ini

clean:
	rm -f shidik-store

.PHONY: all install clean
