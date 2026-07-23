# Зависимости (Debian): libgtk-3-dev
PKGS    = gtk+-3.0
CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS  = $(shell pkg-config --libs $(PKGS))
PREFIX ?= /usr/local

all: shidiklaunch

shidiklaunch: launcher.c
	$(CC) $(CFLAGS) -o $@ launcher.c $(LDLIBS)

install: shidiklaunch
	install -Dm755 shidiklaunch $(DESTDIR)$(PREFIX)/bin/shidiklaunch

clean:
	rm -f shidiklaunch

.PHONY: all install clean
