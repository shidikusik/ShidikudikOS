# Сборка ISO-образа ShidikudikOS

Используем **live-build** — штатный инструмент Debian для сборки live/установочных
образов. Внутри он сам вызывает `debootstrap`, поэтому отдельный ручной
debootstrap-процесс не нужен; для понимания в конце описано, что происходит
под капотом, и приведён эквивалентный ручной путь.

Требования к хосту: Debian 12/13 (или контейнер/VM с ним), root-права,
~15 ГБ свободного места, интернет.

---

## Шаг 0. Подготовка хоста

```sh
sudo apt update
sudo apt install live-build
```

Вся конфигурация уже лежит в каталоге `iso/` репозитория:

```
iso/
├── auto/config                                  ← параметры lb config
├── build.sh                                     ← обёртка «одна кнопка»
└── config/
    ├── package-lists/shidikudik.list.chroot     ← пакеты образа
    ├── hooks/live/0100-build-shidikudik.hook.chroot ← компиляция DM/DE
    └── includes.chroot/                         ← файлы, копируемые в /
```

## Шаг 1. Конфигурация (chroot-окружение и базовые пакеты)

`iso/auto/config` фиксирует параметры дистрибутива: Debian **trixie**,
amd64, hybrid-ISO (BIOS+UEFI), live-инсталлятор, секции
`main contrib non-free-firmware` (прошивки Wi-Fi/GPU):

```sh
cd iso
lb config          # читает auto/config, создаёт каталог config/
```

Список пакетов будущей системы —
`config/package-lists/shidikudik.list.chroot`: база (systemd,
NetworkManager, PipeWire), рантайм графического стека (libwlroots-0.18,
seatd, xwayland, GTK3, gtk-layer-shell), приложения (foot, firefox-esr)
и — временно — компиляторы для шага 2.

## Шаг 2. Компиляция и вшивание ShidikDM/ShidikDE

Два механизма live-build работают в связке:

1. **`config/includes.chroot/`** — всё содержимое копируется в корень
   будущей системы как есть. `build.sh` кладёт туда исходники репозитория
   в `/opt/shidikudik/{src,session,systemd,assets}`.

2. **`config/hooks/live/0100-build-shidikudik.hook.chroot`** — скрипт,
   который live-build выполняет **внутри chroot** после установки пакетов:

   ```sh
   for component in shidikdm shidikwm shidikpanel shidiklaunch shidiksession; do
       make -C /opt/shidikudik/src/$component clean all install
   done
   ```

   Итог внутри образа:
   - `/usr/local/bin/{shidikdm,shidikwm,shidikpanel,shidiklaunch,shidik-session-ctl,shidikde-session,shidikde-autostart}`
   - `/etc/pam.d/shidikdm`
   - `/usr/share/wayland-sessions/shidikde.desktop`

> Альтернатива по мере взросления проекта: собирать компоненты в `.deb`
> (debhelper) и класть их в `config/packages.chroot/` — live-build
> установит их через dpkg, а компиляторы в образе будут не нужны.

## Шаг 3. Автозапуск ShidikDM через systemd

Тот же хук устанавливает юнит и включает его:

```sh
install -Dm644 /opt/shidikudik/systemd/shidikdm.service \
    /etc/systemd/system/shidikdm.service
systemctl enable shidikdm.service     # в chroot это просто создаёт симлинки
systemctl set-default graphical.target
```

Как работает `systemd/shidikdm.service`:

- `Conflicts=getty@tty1.service` + `TTYPath=/dev/tty1` — DM забирает
  первую виртуальную консоль вместо getty (схема greetd/ly);
- `After=systemd-user-sessions.service systemd-logind.service` — логин
  разрешён, logind готов регистрировать сессии;
- `Restart=always` — упавший DM перезапускается за секунду;
- `Alias=display-manager.service` — стандартное имя, под которым Debian
  ожидает видеть активный DM.

Цепочка загрузки готовой системы:

```
GRUB → ядро+initrd → systemd → graphical.target
  → shidikdm.service (tty1) → PAM-логин
    → logind-сессия → shidikde-session → shidikwm (KMS/DRM)
      → shidikpanel + приложения
```

## Шаг 4. Сборка и упаковка ISO

```sh
cd iso
sudo ./build.sh
```

Что делает `lb build` по стадиям:

| Стадия      | Действие                                                        |
|-------------|-----------------------------------------------------------------|
| `bootstrap` | `debootstrap` разворачивает минимальный Debian в `chroot/`      |
| `chroot`    | установка пакетов из списков, копирование includes, запуск хуков (здесь компилируется наш код) |
| `binary`    | squashfs из chroot, ядро+initrd, загрузчики BIOS/UEFI, ISO      |

Результат — `iso/live-image-amd64.hybrid.iso`. Проверка и запись:

```sh
# виртуалка
qemu-system-x86_64 -enable-kvm -m 4G -cdrom live-image-amd64.hybrid.iso

# флешка (ОСТОРОЖНО: затирает /dev/sdX целиком)
sudo dd if=live-image-amd64.hybrid.iso of=/dev/sdX bs=4M status=progress oflag=sync
```

Live-система логинится пользователем `shidik` с паролем `live` (пользователь
создаётся статически chroot-хуком — на live-config полагаться нельзя, с
нестандартным DM его user-setup может не сработать); пункт «Install» в меню
загрузки ставит систему на диск.

Пересборка после правок кода:

```sh
sudo lb clean            # чистит chroot/binary, кэш пакетов остаётся
sudo ./build.sh
```

---

## Приложение: что под капотом (ручной путь через debootstrap)

Для понимания — тот же результат руками, без live-build:

```sh
# 1. базовая система в каталог
sudo debootstrap --arch=amd64 trixie ./rootfs http://deb.debian.org/debian

# 2. войти в chroot
sudo mount --bind /dev  rootfs/dev
sudo mount --bind /proc rootfs/proc
sudo mount --bind /sys  rootfs/sys
sudo chroot rootfs /bin/bash

# 3. внутри: пакеты, ядро, наш код, юнит
apt install linux-image-amd64 live-boot systemd-sysv <пакеты из списка>
make -C /opt/shidikudik/src/... install
systemctl enable shidikdm && systemctl set-default graphical.target
exit

# 4. упаковать rootfs в squashfs, собрать дерево ISO (ядро, initrd,
#    grub/isolinux) и прогнать xorriso
sudo mksquashfs rootfs live/filesystem.squashfs -comp zstd
xorriso -as mkisofs ... # + grub-mkstandalone для UEFI
```

Шаг 4 руками — самый муторный (гибридная загрузка BIOS+UEFI, initrd с
live-boot); именно его live-build автоматизирует, поэтому основной путь
проекта — live-build.
