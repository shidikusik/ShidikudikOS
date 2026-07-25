CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
LDLIBS   = -lpam
PREFIX  ?= /usr/local

OBJS = main.o auth.o session.o

all: shidikdm

shidikdm: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c auth.h session.h
	$(CC) $(CFLAGS) -c $< -o $@

install: shidikdm
	install -Dm755 shidikdm $(DESTDIR)$(PREFIX)/bin/shidikdm
	install -Dm644 shidikdm.pam $(DESTDIR)/etc/pam.d/shidikdm
	install -Dm644 shidikdm-autologin.pam \
		$(DESTDIR)/etc/pam.d/shidikdm-autologin

clean:
	rm -f shidikdm $(OBJS)

.PHONY: all install clean
