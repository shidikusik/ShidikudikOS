#!/bin/sh
# Собирает .deb-пакет со всем рабочим окружением ShidikusikOS.
#
#   ./packaging/build-deb.sh [версия] [каталог-вывода]
#
# Пакет ставит те же файлы, что и `make install`, но система теперь знает
# о них: `apt upgrade` обновляет DE как любой другой пакет, а не требует
# переустанавливать ОС.
#
# Префикс — /usr/local. Для .deb это нетипично (политика Debian отдаёт
# /usr/local человеку, а не пакетам), но здесь так сделано намеренно:
# ровно те же пути, что при сборке из исходников, значит ни один
# зашитый путь (юнит systemd, .desktop, вызовы между компонентами) не
# расходится между способами установки.
set -e

VERSION="${1:-1.0.$(date -u +%Y%m%d%H%M)}"
OUTDIR="${2:-$(pwd)}"
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PKGROOT=$(mktemp -d)
trap 'rm -rf "$PKGROOT"' EXIT

echo "== собираю ShidikDE версии $VERSION"
make -C "$ROOT" clean >/dev/null 2>&1 || true
make -C "$ROOT" all
make -C "$ROOT" DESTDIR="$PKGROOT" install

# Брендинг и документация — то, что хук ISO ставил отдельно
install -Dm644 "$ROOT/assets/logo.svg"            "$PKGROOT/usr/share/shidikusik/logo.svg"
install -Dm644 "$ROOT/assets/wallpaper.png"       "$PKGROOT/usr/share/shidikusik/wallpaper.png"
install -Dm644 "$ROOT/assets/wallpaper-blur.png"  "$PKGROOT/usr/share/shidikusik/wallpaper-blur.png"
install -Dm644 "$ROOT/assets/logo-ascii.txt"      "$PKGROOT/usr/share/shidikusik/logo-ascii.txt"
install -Dm644 "$ROOT/docs/hardware.md"           "$PKGROOT/usr/share/doc/shidikusik/hardware.md"
install -Dm755 "$ROOT/packaging/shidik-update"    "$PKGROOT/usr/local/bin/shidik-update"

SIZE=$(du -sk "$PKGROOT" | cut -f1)

mkdir -p "$PKGROOT/DEBIAN"
cat > "$PKGROOT/DEBIAN/control" <<EOF
Package: shidikusik-desktop
Version: $VERSION
Section: x11
Priority: optional
Architecture: amd64
Installed-Size: $SIZE
Maintainer: ShidikusikOS <noreply@shidikusik.local>
Depends: libc6, libwlroots-0.18, libwayland-server0, libxkbcommon0,
 libgtk-3-0 | libgtk-3-0t64, libgtk-layer-shell0, libvte-2.91-0,
 libpam0g, libsystemd0, libglib2.0-0 | libglib2.0-0t64,
 adwaita-icon-theme, librsvg2-common, seatd
Recommends: network-manager, bluez, pipewire, wireplumber, brightnessctl,
 lxpolkit, pkexec, foot, swaybg, modemmanager
Description: Рабочее окружение ShidikusikOS (ShidikDM + ShidikDE)
 Собственные display manager и окружение рабочего стола, написанные с
 нуля: Wayland-композитор на wlroots, панель, меню приложений, шторка
 уведомлений и быстрых настроек, экран блокировки, терминал, файловый
 менеджер, магазин приложений, центр настроек и установщик на диск.
EOF

# После установки: перечитать юниты и напомнить про перезапуск сессии
cat > "$PKGROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = configure ]; then
    systemctl daemon-reload >/dev/null 2>&1 || true
    systemctl enable shidikdm.service >/dev/null 2>&1 || true
    echo "ShidikusikOS: окружение обновлено."
    echo "Изменения вступят в силу после выхода из сессии или перезагрузки."
fi
exit 0
EOF
chmod 755 "$PKGROOT/DEBIAN/postinst"

DEB="$OUTDIR/shidikusik-desktop_${VERSION}_amd64.deb"
dpkg-deb --build --root-owner-group "$PKGROOT" "$DEB" >/dev/null
echo "== готов: $DEB"
