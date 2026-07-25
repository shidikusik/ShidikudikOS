# ShidikusikOS

**Русский** · [English](README.en.md)

<img src="assets/logo.svg" width="160" align="right" alt="логотип ShidikusikOS"/>

**ShidikusikOS** — учебно-практический Debian-based дистрибутив Linux с
собственными, написанными с нуля Display Manager (**ShidikDM**) и Desktop
Environment (**ShidikDE**).

📀 **[Скачать готовый ISO →](https://github.com/shidikusik/ShidikusikOS/releases)**
— live-сессия **входит без пароля**, установка на диск в один клик.

```
   ██████╗
   ██╔═══╝
   ╚█████╗     ShidikusikOS
    ╚═══██╗    маленький · быстрый · свой
   ██████╔╝
   ╚═════╝
```

---

## Скриншоты

### Вход и рабочий стол

| Экран входа (shidikgreet) | Рабочий стол (ShidikDE) | Меню приложений (Win+D) |
|---|---|---|
| ![Графический экран входа ShidikDM](assets/screenshots/shidikdm-greeter.png) | ![Рабочий стол ShidikDE с панелью](assets/screenshots/shidikde-desktop.png) | ![Меню приложений shidiklaunch](assets/screenshots/shidiklaunch-menu.png) |
| размытые обои, часы, карточка логина | панель: меню, часы, нагрузка, питание | поиск по `.desktop`-файлам |

### Шторка и блокировка

| Шторка уведомлений и быстрых настроек (Win+N) | Экран блокировки (Win+L) |
|---|---|
| ![Шторка быстрых настроек](assets/screenshots/shidik-shade.png) | ![Экран блокировки](assets/screenshots/shidiklock.png) |
| Wi-Fi со списком сетей, Bluetooth, громкость, яркость, история уведомлений | часы, пользователь, проверка пароля через PAM |

### Собственные приложения

| Shidik-Term | Shidik-Files |
|---|---|
| ![Терминал Shidik-Term](assets/screenshots/shidik-term.png) | ![Файловый менеджер Shidik-Files](assets/screenshots/shidik-files.png) |
| терминал в фирменной палитре (Win+Enter) | места, размеры, типы файлов, корзина |

| Shidik-Store | Shidik-Control |
|---|---|
| ![Магазин приложений Shidik-Store](assets/screenshots/shidik-store.png) | ![Центр настроек Shidik-Control](assets/screenshots/shidik-control.png) |
| каталог с категориями, установка в один клик | 9 разделов: система, тема, сеть, Bluetooth, звук, экраны, питание, обновления, пользователь |

### Установка на диск

![Графический установщик](assets/screenshots/shidik-install.png)

Мастер из пяти шагов: диск → пользователь и пароль → подтверждение →
установка с прогрессом. Есть и консольный вариант — `sudo shidik-install`
([скриншот текстового входа](assets/screenshots/shidikdm-login.png) —
запасной greeter ShidikDM на случай проблем с графикой).

---

## 1. Общие характеристики

| Параметр            | Значение                                             |
|---------------------|------------------------------------------------------|
| База                | Debian 13 "trixie" (stable), ядро Linux 6.12 LTS     |
| Пакетный менеджер   | `apt` / `dpkg`                                       |
| Инициализация       | `systemd` (+ `systemd-logind` для сессий и питания)  |
| Графический стек    | **Wayland** через **wlroots 0.18**                   |
| Display Manager     | **ShidikDM** — C, PAM, консольный greeter            |
| Desktop Environment | **ShidikDE** — композитор + панель + лаунчер + сессия|
| Сборка ISO          | `live-build` (внутри использует `debootstrap`)       |

### Почему Wayland/wlroots, а не X11

X11 для нового DE в 2026 году — тупиковая ветка: сервер X.Org фактически
заморожен, а «оконный менеджер поверх Xlib/xcb» наследует всю архаичную
модель (отдельный X-сервер, композитор как надстройка, небезопасный
глобальный ввод). Wayland-путь с **wlroots** даёт:

- **Одна программа = весь стек.** Композитор wlroots — это одновременно
  дисплей-сервер, оконный менеджер и композитор. Меньше движущихся частей.
- **~50 000 строк готовой инфраструктуры**: DRM/KMS-вывод, libinput,
  аппаратный рендеринг, реализация протоколов — а политика окон остаётся
  полностью нашей (проверено Sway, Hyprland, river, labwc).
- **Безопасность**: клиенты не видят чужой ввод и чужие окна.
- **Debian trixie уже содержит** `libwlroots-0.18-dev` — собираем штатным
  тулчейном без внешних репозиториев.

Цена: X11-приложения требуют XWayland (пакет включён в ISO), а панели
нужен протокол layer-shell. Это принятая цена всей индустрии.

---

## 2. Структура репозитория

```
ShidikusikOS/
├── README.md / README.en.md   ← этот файл (RU/EN)
├── Makefile                   ← сборка/установка всех компонентов
├── assets/                    ← логотип, скриншоты
├── src/
│   ├── shidikdm/              ← Display Manager (C + PAM)
│   │   ├── main.c             ← greeter: баннер, логин/пароль, главный цикл
│   │   ├── auth.c / auth.h    ← PAM: аутентификация, открытие сессии
│   │   ├── session.c/.h       ← fork, сброс привилегий, exec DE
│   │   └── shidikdm.pam       ← /etc/pam.d/shidikdm
│   ├── shidikwm/              ← Wayland-композитор (C + wlroots 0.18)
│   ├── shidikpanel/           ← панель (C + GTK3 + gtk-layer-shell)
│   ├── shidiklaunch/          ← меню приложений (.desktop, C + GTK3)
│   ├── shidiksession/         ← shidik-session-ctl (C + sd-bus/logind)
│   ├── shidikgreet/           ← графический greeter DM (glassmorphism)
│   ├── shidikterm/            ← Shidik-Term: терминал (GTK3 + VTE)
│   ├── shidikcontrol/         ← Shidik-Control: центр настроек
│   ├── shidikfiles/           ← Shidik-Files: файловый менеджер
│   ├── shidikstore/           ← Shidik-Store: магазин приложений (apt)
│   ├── shidikshade/           ← шторка уведомлений и быстрых настроек
│   └── shidiklock/            ← экран блокировки (PAM)
├── installer/                 ← установка на диск
│   ├── shidik-install         ← движок: разметка, копирование, GRUB
│   └── gui/                   ← мастер на GtkAssistant поверх него
├── session/
│   ├── shidikde-session       ← точка входа в сессию (env + dbus + wm)
│   ├── shidikde-autostart     ← поднимает панель и пр. внутри Wayland
│   └── shidikde.desktop       ← регистрация сессии (wayland-sessions)
├── systemd/
│   └── shidikdm.service       ← автозапуск DM на tty1
├── iso/                       ← конфигурация live-build + build.sh
├── packaging/                 ← сборка .deb и shidik-update
├── .github/workflows/         ← CI: .deb → apt-репозиторий → ISO
└── docs/
    ├── iso-build.md           ← пошаговая инструкция сборки ISO
    ├── updates.md             ← обновление без переустановки
    └── hardware.md            ← железо: что работает, MT7902, модемы
```

---

## 3. ShidikDM — Display Manager

Минималистичный консольный greeter в духе `greetd`/`ly` (~450 строк C,
единственная зависимость — `libpam`).

### Архитектура

```
systemd (graphical.target)
   └── shidikdm.service  (tty1, root)
        └── shidikdm ──── main.c: цикл greeter'а
             ├── auth.c:    pam_start → pam_authenticate → pam_acct_mgmt
             │              pam_open_session  ← здесь pam_systemd
             │                                  регистрирует сессию в logind
             │                                  и создаёт XDG_RUNTIME_DIR
             └── session.c: fork →
                   [child] setsid → initgroups/setgid/setuid →
                           окружение из pam_getenvlist →
                           exec $SHELL -l -c shidikde-session
                   [parent, root] waitpid → pam_close_session → снова greeter
```

Ключевые решения:

- **PAM-конверсация** (`sdm_conv` в `auth.c`): пароль собирается UI-слоем
  заранее и отдаётся PAM'у по запросу `PAM_PROMPT_ECHO_OFF` — UI и
  авторизация полностью развязаны, консольный greeter легко заменить на
  графический (SDL2/Raylib/GTK), не трогая auth/session-код.
- **`pam_systemd` обязателен** (идёт через Debian-овский `common-session`):
  без него не будет `XDG_RUNTIME_DIR`, а wlroots-композитор не запустится
  и не получит доступ к DRM/input через logind.
- Привилегии сбрасываются **только в потомке** и строго в порядке
  `initgroups → setgid → setuid`; root-процесс DM продолжает жить, чтобы
  корректно закрыть PAM-сессию и показать greeter снова.
- Защита от перебора: 3 попытки + `sleep(1)`, пароль затирается
  `explicit_bzero`.

Сборка: `make -C src/shidikdm` (нужен `libpam0g-dev`).

## 4. ShidikDE — Desktop Environment

Модульная архитектура: четыре независимых процесса, связанных протоколами
Wayland и D-Bus — любой компонент можно заменить, не трогая остальные.

```
shidikde-session (shell)
 └── dbus-run-session
      └── shidikwm  ← композитор: ЯДРО. Владеет экраном и вводом
           │           (WAYLAND_DISPLAY=wayland-0)
           └── shidikde-autostart
                ├── shidikpanel   ← Wayland-клиент (layer-shell)
                ├── shidiklaunch  ← по требованию (☰ или Win+D)
                └── shidik-session-ctl ← вызывается по кнопкам питания
                        └── D-Bus → systemd-logind (PowerOff/Reboot/...)
```

### 4.1 shidikwm — композитор (`src/shidikwm/main.c`)

Каркас на базе tinywl (CC0) под **wlroots 0.18**. Уже реализовано:

- вывод на мониторы: `wlr_output_layout` + сценовый граф `wlr_scene`
  (рендеринг и damage-tracking бесплатно);
- окна `xdg-shell`: map/unmap, фокус по клику, поднятие наверх,
  интерактивные перемещение и ресайз, попапы, полноэкранный режим;
  новое окно открывается по центру экрана (со сдвигом-каскадом);
- **layer-shell** (`wlr_layer_shell_v1`): панель — настоящая полоса у
  края экрана с резервированием места, а не обычное окно. Сцена разбита
  на слои background / bottom / окна / top / overlay, поэтому окна
  приложений никогда не перекрывают панель. XML протокола не
  поставляется пакетами и лежит в `protocols/` (MIT, проект wlroots);
  заголовок генерируется `wayland-scanner`'ом при сборке;
- ввод: клавиатура через `xkbcommon` (раскладка из `XKB_DEFAULT_LAYOUT`),
  курсор через `wlr_cursor` + `xcursor`;
- хоткеи (модификатор — Win/Super): `Win+Enter` — терминал,
  `Win+D` — меню приложений, `Win+N` — шторка, `Win+L` — блокировка,
  `Win+Tab` — переключение окон, `Win+Q` — закрыть окно, `Win+T` —
  тайлинг вкл/выкл, `Win+[` и `Win+]` — ширина мастер-окна,
  `Win+Esc` — выход из сессии;
- **тайлинг** (Win+T): раскладка мастер/стек — первое окно занимает
  левую часть экрана, остальные делят правую. Окна не наползают друг
  на друга и не залезают под панель;
- `-s <cmd>` — автостарт-скрипт получает готовый `WAYLAND_DISPLAY`.

Отмеченные в коде точки роста: `wlr_foreign_toplevel_manager_v1`
(список окон в панели), XWayland, воркспейсы.

### 4.2 shidikpanel — панель (`src/shidikpanel/panel.c`)

GTK3 + `gtk-layer-shell`: полоса, прибитая к верхнему краю экрана с
резервированием места (exclusive zone). Показывает часы (обновление 1 с),
load average из `/proc/loadavg`, заряд батареи из `/sys/class/power_supply`,
кнопку меню «☰» и меню питания «⏻» (Выйти/Перезагрузка/Выключение/Сон →
`shidik-session-ctl`). Список окон — заглушка с описанным в комментарии
планом подключения `wlr-foreign-toplevel-management-v1`.

### 4.3 shidiklaunch — меню приложений (`src/shidiklaunch/launcher.c`)

Сканирует `.desktop`-файлы (`/usr/share/applications`,
`~/.local/share/applications`) через `GKeyFile`, уважает
`NoDisplay`/`Hidden`, локализованные `Name`/`Comment`, вычищает
код-подстановки `%f %u …` из `Exec`. GTK-окно: поиск + список,
Enter запускает первый найденный, Esc закрывает.

### 4.4 shidik-session-ctl — менеджер сессии (`src/shidiksession/`)

Тонкий клиент `systemd-logind` по D-Bus (sd-bus из libsystemd):
`poweroff`/`reboot`/`suspend` — методы `Manager.PowerOff` и т.д. (права
проверяет polkit, root не нужен); `logout` — `TerminateSession("")`:
logind убивает все процессы сессии, композитор гаснет, ShidikDM
показывает экран входа.

### 4.5 Shidik-Files и Shidik-Store

**Shidik-Files** (`src/shidikfiles/`) — файловый менеджер на GTK3 + GIO:
навигация с историей, боковая панель мест, системные иконки типов файлов,
открытие через `GAppInfo`, создание папки, переименование, удаление в
корзину (`g_file_trash`), показ скрытых файлов.

**Shidik-Store** (`src/shidikstore/`) — магазин приложений, витрина над
`apt`. Каталог описан обычным ini-файлом
`/usr/share/shidikusik/store-catalog.ini` (29 приложений: браузеры, офис,
графика, мультимедиа, разработка, игры, системные утилиты) — его можно
дополнять руками. Статус пакета берётся у `dpkg-query`, установка и
удаление — `pkexec apt-get -y install|remove` с живым логом; кнопка
«Искать во всём репозитории» открывает `apt-cache search` в терминале.

### 4.6 Шторка, блокировка и тайлинг

**shidikshade** (`src/shidikshade/`) — один демон в двух ролях:
сервер уведомлений `org.freedesktop.Notifications` (всплывашки в правом
верхнем углу + история) и шторка быстрых настроек по Win+N: Wi-Fi со
списком сетей и вводом пароля, Bluetooth, громкость, яркость. Реальная
работа делегируется `nmcli`, `bluetoothctl`, `wpctl`, `brightnessctl`.

**shidiklock** (`src/shidiklock/`) — экран блокировки: layer-shell
поверхность слоя OVERLAY с эксклюзивным захватом клавиатуры, пароль
проверяется через PAM от имени текущего пользователя (root не нужен —
работает штатный `unix_chkpwd`). Вызов: Win+L или пункт меню питания.

**Тайлинг** (Win+T) живёт в композиторе: раскладка мастер/стек с
отступами, учитывает exclusive zone панели. Ширина мастера — Win+[ и
Win+]. При выключенном тайлинге окна открываются по центру каскадом.

### Сборка всего DE

```sh
sudo apt install gcc make pkg-config libpam0g-dev libwlroots-0.18-dev \
    libwayland-dev libxkbcommon-dev wayland-protocols libgtk-3-dev \
    libgtk-layer-shell-dev libsystemd-dev libvte-2.91-dev
make            # собрать всё
sudo make install
sudo systemctl enable shidikdm   # автозапуск DM
```

Быстрый тест без установки: `shidikwm` запускается вложенно из-под любого
работающего Wayland/X11-сеанса (wlroots сам выберет nested-бэкенд).

---

## 5. Live-режим и установка

### Live-сессия: без пароля

ISO загружается **сразу в рабочий стол, пароль не спрашивается**. Как это
устроено:

- chroot-хук пишет `/etc/shidikusik/autologin` с именем `shidik`;
- ShidikDM при старте видит этот файл и логинит пользователя через
  PAM-сервис `shidikdm-autologin`, где стадия `auth` заменена на
  `pam_permit` (сессия при этом полноценная: `pam_systemd`, logind,
  `XDG_RUNTIME_DIR`);
- автовход срабатывает **один раз** — после «Выйти» появляется обычный
  экран входа (иначе выйти было бы невозможно);
- в live-режиме также включены `sudo` без пароля и правило polkit, чтобы
  установщик и магазин приложений не спрашивали пароль, которого нет.

Все три файла **удаляются установщиком** — на диске система спрашивает
пароль как обычно.

### Установщик

Графический мастер `shidik-install-gui` (пункт «Установить ShidikusikOS»
в меню приложений) поверх движка `installer/shidik-install`:

1. выбор целевого диска (диск стирается целиком, с подтверждением);
2. разметка GPT под UEFI (ESP + ext4) или BIOS (bios_grub + ext4) —
   режим определяется автоматически;
3. копирование работающей live-системы `rsync`'ом;
4. **пользователь сам задаёт** имя компьютера, часовой пояс, логин и
   пароль (root блокируется, работа через sudo);
5. fstab по UUID, GRUB под нужный режим, свежий initramfs;
6. удаление live-обвязки: автовход, беспарольные sudo/polkit,
   live-boot/live-config и сам пользователь `shidik`.

Движок работает и сам по себе — `sudo shidik-install` даёт те же вопросы
в консоли, а `--unattended` позволяет скриптовать установку:

```sh
echo 'мой-пароль' | sudo shidik-install --unattended \
    --disk /dev/sda --user ivan --hostname mypc --timezone Europe/Moscow \
    --password-stdin
```

### Обновление без переустановки

Рабочее окружение — обычный пакет `shidikusik-desktop` из собственного
apt-репозитория (ветка `apt-repo` этого проекта), поэтому обновляется
как всё остальное:

```sh
sudo shidik-update      # система + окружение одной командой
```

или «Настройки → Обновления → Обновить ShidikusikOS». Переустанавливать
систему не нужно. Подробности, откат версий и запасная сборка из
исходников — в [`docs/updates.md`](docs/updates.md).

---

## 6. Сборка ISO

Полная инструкция — [`docs/iso-build.md`](docs/iso-build.md). Кратко:

```sh
sudo apt install live-build
cd iso
sudo ./build.sh          # → live-image-amd64.hybrid.iso
```

`build.sh` копирует исходники в образ, `lb build` разворачивает Debian
через debootstrap, chroot-хук компилирует ShidikDM/ShidikDE прямо внутри
будущей системы и включает `shidikdm.service`, после чего всё пакуется в
загрузочный hybrid-ISO (BIOS+UEFI, можно писать на флешку через `dd`).

Готовые образы собираются автоматически: workflow
`.github/workflows/release-iso.yml` строит ISO в контейнере
`debian:trixie` и прикладывает его к GitHub Release вместе с sha256.

Проверка в QEMU:

```sh
qemu-system-x86_64 -enable-kvm -m 4G -cdrom iso/live-image-amd64.hybrid.iso
```

## 7. Дорожная карта

- [x] тайлинг (Win+T), центрирование окон, полноэкранный режим
- [x] шторка уведомлений и быстрых настроек (Wi-Fi, Bluetooth, звук)
- [x] экран блокировки через PAM
- [x] прошивки, Bluetooth, печать, кодеки — готовность к ежедневной работе
- [x] мобильный интернет: USB-модемы, телефон как модем (ModemManager,
      usb-modeswitch, usbmuxd) + подключение по APN из настроек
- [x] обновление без переустановки: окружение — пакет
      `shidikusik-desktop`, свой apt-репозиторий, `shidik-update`
- [ ] подпись репозитория GPG-ключом
- [x] layer-shell в shidikwm — панель как полоса у края экрана
- [x] автовход в live-режиме без пароля
- [x] графический установщик (мастер на GtkAssistant)
- [x] магазин приложений Shidik-Store (витрина над apt)
- [x] инсталлер на диск (shidik-install)
- [x] графический greeter ShidikDM (shidikgreet: glassmorphism, свой
      композитор через seatd-launch, авторизация у root по сокету)
- [x] собственный терминал Shidik-Term (GTK3 + VTE, фирменная палитра)
- [x] центр настроек Shidik-Control (тема, сеть, экраны, пользователь)
- [x] тёмная тема по умолчанию, обои, брендинг в /usr/share/shidikusik
- [x] Shidik-Files: файловый менеджер (GTK3 + GIO: навигация, места,
      корзина, переименование, открытие файлов по умолчанию)
- [ ] foreign-toplevel → список открытых окон в панели (таскбар)
- [ ] воркспейсы и тайлинг-режим
- [ ] упаковка компонентов в .deb (debhelper) вместо сборки хуком
- [ ] собственный репозиторий apt (reprepro/aptly)
- [ ] брендинг: plymouth-тема, обои, GRUB-меню

## Лицензия

Код — MIT; `src/shidikwm/main.c` основан на tinywl (CC0) из проекта wlroots.
