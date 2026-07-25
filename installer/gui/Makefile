# Зависимости (Debian): libgtk-3-dev
PKGS    = gtk+-3.0
CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS  = $(shell pkg-config --libs $(PKGS))
PREFIX ?= /usr/local

all: shidik-install-gui

shidik-install-gui: install-gui.c
	$(CC) $(CFLAGS) -o $@ install-gui.c $(LDLIBS)

install: shidik-install-gui
	install -Dm755 shidik-install-gui \
		$(DESTDIR)$(PREFIX)/bin/shidik-install-gui
	install -Dm755 ../shidik-install $(DESTDIR)$(PREFIX)/bin/shidik-install
	install -Dm644 ../shidikudik-installer.desktop \
		$(DESTDIR)/usr/share/applications/shidikudik-installer.desktop

clean:
	rm -f shidik-install-gui

.PHONY: all install clean
