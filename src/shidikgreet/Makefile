# Зависимости (Debian): libgtk-3-dev
PKGS    = gtk+-3.0
CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS  = $(shell pkg-config --libs $(PKGS))
PREFIX ?= /usr/local

all: shidikgreet

shidikgreet: greet.c
	$(CC) $(CFLAGS) -o $@ greet.c $(LDLIBS)

install: shidikgreet
	install -Dm755 shidikgreet $(DESTDIR)$(PREFIX)/bin/shidikgreet

clean:
	rm -f shidikgreet

.PHONY: all install clean
