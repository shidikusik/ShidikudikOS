# Зависимости (Debian): libgtk-3-dev
PKGS    = gtk+-3.0
CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS  = $(shell pkg-config --libs $(PKGS))
PREFIX ?= /usr/local

all: shidik-files

shidik-files: files.c
	$(CC) $(CFLAGS) -o $@ files.c $(LDLIBS)

install: shidik-files
	install -Dm755 shidik-files $(DESTDIR)$(PREFIX)/bin/shidik-files
	install -Dm644 shidik-files.desktop \
		$(DESTDIR)/usr/share/applications/shidik-files.desktop

clean:
	rm -f shidik-files

.PHONY: all install clean
