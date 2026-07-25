# Зависимости (Debian): libgtk-3-dev
PKGS    = gtk+-3.0
CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS  = $(shell pkg-config --libs $(PKGS))
PREFIX ?= /usr/local

all: shidik-control

shidik-control: control.c
	$(CC) $(CFLAGS) -o $@ control.c $(LDLIBS)

install: shidik-control
	install -Dm755 shidik-control $(DESTDIR)$(PREFIX)/bin/shidik-control
	install -Dm644 shidik-control.desktop \
		$(DESTDIR)/usr/share/applications/shidik-control.desktop

clean:
	rm -f shidik-control

.PHONY: all install clean
