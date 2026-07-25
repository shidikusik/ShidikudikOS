#!/bin/sh
# Сборка ISO-образа ShidikusikOS.
# Запускать под root (или через sudo) на Debian-хосте с установленным
# пакетом live-build. Всё выполняется в каталоге iso/.
#
#   sudo apt install live-build
#   cd iso && sudo ./build.sh
#
# Результат: iso/live-image-amd64.hybrid.iso
set -e
cd "$(dirname "$0")"

REPO_ROOT=$(cd .. && pwd)

# 1. Скопировать исходники и юниты внутрь будущего образа
#    (includes.chroot попадает в / целевой системы как есть).
echo "== копирую исходники в includes.chroot =="
rm -rf config/includes.chroot/opt/shidikusik
mkdir -p config/includes.chroot/opt/shidikusik
cp -r "$REPO_ROOT/src"     config/includes.chroot/opt/shidikusik/src
cp -r "$REPO_ROOT/session" config/includes.chroot/opt/shidikusik/session
cp -r "$REPO_ROOT/systemd" config/includes.chroot/opt/shidikusik/systemd
cp -r "$REPO_ROOT/assets"  config/includes.chroot/opt/shidikusik/assets
cp -r "$REPO_ROOT/installer" config/includes.chroot/opt/shidikusik/installer
# protocols/ нужен Makefile'у композитора: XML layer-shell не поставляется
# пакетами, заголовок генерируется из него wayland-scanner'ом
cp -r "$REPO_ROOT/protocols" config/includes.chroot/opt/shidikusik/protocols
cp -r "$REPO_ROOT/docs"      config/includes.chroot/opt/shidikusik/docs

# session-скрипты кладём и напрямую — Makefile shidiksession ссылается
# на ../../session относительно src/, структура сохранена копированием.

# 2. Конфигурация live-build (см. auto/config)
echo "== lb config =="
lb config

# 3. Сборка: debootstrap -> chroot -> хуки -> squashfs -> ISO
echo "== lb build (займёт заметное время) =="
lb build

echo "== готово: $(ls -1 *.iso 2>/dev/null || echo 'ISO не найден — смотри лог выше') =="
