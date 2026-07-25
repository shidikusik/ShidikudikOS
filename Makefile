# Корневой Makefile ShidikudikOS: собирает и ставит все компоненты.
# Зависимости для сборки (Debian 13):
#   apt install gcc make pkg-config libpam0g-dev libwlroots-0.18-dev \
#       libwayland-dev libxkbcommon-dev wayland-protocols libgtk-3-dev \
#       libgtk-layer-shell-dev libsystemd-dev

COMPONENTS = shidikdm shidikwm shidikpanel shidiklaunch shidiksession \
             shidikgreet shidikterm shidikcontrol shidikfiles shidikstore

all:
	for c in $(COMPONENTS); do $(MAKE) -C src/$$c all || exit 1; done
	$(MAKE) -C installer/gui all

install:
	for c in $(COMPONENTS); do $(MAKE) -C src/$$c install || exit 1; done
	$(MAKE) -C installer/gui install
	install -Dm644 systemd/shidikdm.service \
		$(DESTDIR)/etc/systemd/system/shidikdm.service

clean:
	for c in $(COMPONENTS); do $(MAKE) -C src/$$c clean; done
	$(MAKE) -C installer/gui clean

.PHONY: all install clean
