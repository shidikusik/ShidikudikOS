/* shidik-control — центр настроек ShidikusikOS.
 *
 * Одно окно, боковая панель разделов:
 *   О системе   — логотип, ядро, процессор, память, диск, аптайм;
 *   Внешний вид — тёмная тема, обои, размер шрифта;
 *   Сеть        — состояние, список Wi-Fi, подключение, nmtui;
 *   Bluetooth   — питание адаптера, сопряжение через bluetoothctl;
 *   Звук        — громкость и выбор устройства вывода (wpctl);
 *   Экраны      — подключённые выводы и их режимы;
 *   Питание     — батарея, режим сна;
 *   Обновления  — apt update/upgrade с живым логом;
 *   Пользователь— группы, смена пароля.
 *
 * Тяжёлые операции делегируются штатным инструментам (nmcli, wpctl,
 * bluetoothctl, apt), которые запускаются либо напрямую, либо в
 * shidik-term — модуль остаётся тонким и предсказуемым.
 *
 * Зависимости: libgtk-3-dev
 */
#include <gtk/gtk.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <string.h>

static GtkWidget *window;
static GtkWidget *net_label, *bt_label, *update_log;
static GtkWidget *wifi_list;

/* ---------- утилиты ---------- */

static gchar *run_capture(const gchar *cmd) {
    gchar *out = NULL;
    if (!g_spawn_command_line_sync(cmd, &out, NULL, NULL, NULL))
        return NULL;
    if (out)
        g_strchomp(out);
    return out;
}

static void run_async(const gchar *cmd) {
    g_spawn_command_line_async(cmd, NULL);
}

static void spawn_in_terminal(const gchar *cmd) {
    gchar *full = g_strdup_printf("shidik-term -e %s", cmd);
    run_async(full);
    g_free(full);
}

static GtkWidget *info_row(const gchar *key, const gchar *value) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *k = gtk_label_new(key);
    gtk_widget_set_halign(k, GTK_ALIGN_START);
    gtk_widget_set_size_request(k, 150, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(k),
        "dim-label");
    GtkWidget *v = gtk_label_new(value ? value : "—");
    gtk_widget_set_halign(v, GTK_ALIGN_START);
    gtk_label_set_selectable(GTK_LABEL(v), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(v), TRUE);
    gtk_box_pack_start(GTK_BOX(row), k, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), v, TRUE, TRUE, 0);
    return row;
}

static GtkWidget *page_box(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 18);
    return box;
}

static GtkWidget *section(const gchar *title) {
    GtkWidget *l = gtk_label_new(NULL);
    gchar *m = g_markup_printf_escaped("<b>%s</b>", title);
    gtk_label_set_markup(GTK_LABEL(l), m);
    g_free(m);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_widget_set_margin_top(l, 6);
    return l;
}

static GtkWidget *scrolled_page(GtkWidget *content) {
    GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sc),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sc), content);
    return sc;
}

/* ---------- О системе ---------- */

static gchar *cpu_model(void) {
    gchar *contents = NULL;
    if (!g_file_get_contents("/proc/cpuinfo", &contents, NULL, NULL))
        return NULL;
    gchar *result = NULL;
    gchar **lines = g_strsplit(contents, "\n", -1);
    for (gchar **l = lines; *l; l++) {
        if (g_str_has_prefix(*l, "model name")) {
            gchar *colon = strchr(*l, ':');
            if (colon)
                result = g_strstrip(g_strdup(colon + 1));
            break;
        }
    }
    g_strfreev(lines);
    g_free(contents);
    return result;
}

static GtkWidget *build_system_page(void) {
    GtkWidget *box = page_box();

    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    if (g_file_test("/usr/share/shidikusik/logo.svg", G_FILE_TEST_EXISTS)) {
        GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(
            "/usr/share/shidikusik/logo.svg", 88, 88, NULL);
        if (pb) {
            GtkWidget *logo = gtk_image_new_from_pixbuf(pb);
            g_object_unref(pb);
            gtk_box_pack_start(GTK_BOX(head), logo, FALSE, FALSE, 0);
        }
    }
    GtkWidget *names = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *big = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(big),
        "<span size='20000' weight='bold'>ShidikusikOS</span>");
    gtk_widget_set_halign(big, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(names), big, FALSE, FALSE, 0);
    GtkWidget *sub = gtk_label_new("на базе Debian 13 · ShidikDE");
    gtk_widget_set_halign(sub, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(sub),
        "dim-label");
    gtk_box_pack_start(GTK_BOX(names), sub, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(head), names, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), head, FALSE, FALSE, 6);

    struct utsname un;
    uname(&un);
    gtk_box_pack_start(GTK_BOX(box), section("Оборудование"),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), info_row("Компьютер",
        g_get_host_name()), FALSE, FALSE, 0);

    gchar *cpu = cpu_model();
    gtk_box_pack_start(GTK_BOX(box), info_row("Процессор", cpu),
        FALSE, FALSE, 0);
    g_free(cpu);

    gchar *contents = NULL;
    if (g_file_get_contents("/proc/meminfo", &contents, NULL, NULL)) {
        long total = 0, avail = 0;
        sscanf(contents, "MemTotal: %ld", &total);
        gchar *ap = strstr(contents, "MemAvailable:");
        if (ap)
            sscanf(ap, "MemAvailable: %ld", &avail);
        gchar *s = g_strdup_printf("%.1f ГиБ (свободно %.1f ГиБ)",
            total / 1048576.0, avail / 1048576.0);
        gtk_box_pack_start(GTK_BOX(box), info_row("Память", s),
            FALSE, FALSE, 0);
        g_free(s);
        g_free(contents);
    }

    struct statvfs vfs;
    if (statvfs("/", &vfs) == 0) {
        double total = (double)vfs.f_blocks * vfs.f_frsize / 1e9;
        double freeb = (double)vfs.f_bavail * vfs.f_frsize / 1e9;
        gchar *s = g_strdup_printf("%.0f ГБ (свободно %.0f ГБ)",
            total, freeb);
        gtk_box_pack_start(GTK_BOX(box), info_row("Диск /", s),
            FALSE, FALSE, 0);
        g_free(s);
    }

    gtk_box_pack_start(GTK_BOX(box), section("Система"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), info_row("Ядро", un.release),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), info_row("Архитектура", un.machine),
        FALSE, FALSE, 0);

    if (g_file_get_contents("/proc/uptime", &contents, NULL, NULL)) {
        double up = g_ascii_strtod(contents, NULL);
        gchar *s = g_strdup_printf("%d ч %d мин",
            (int)(up / 3600), (int)(up / 60) % 60);
        gtk_box_pack_start(GTK_BOX(box), info_row("Аптайм", s),
            FALSE, FALSE, 0);
        g_free(s);
        g_free(contents);
    }
    return scrolled_page(box);
}

/* ---------- Внешний вид ---------- */

static void write_gtk_settings(gboolean dark, int font_size) {
    const gchar *dirs[] = { "gtk-3.0", "gtk-4.0" };
    for (gsize i = 0; i < G_N_ELEMENTS(dirs); i++) {
        gchar *dir = g_build_filename(g_get_user_config_dir(),
            dirs[i], NULL);
        g_mkdir_with_parents(dir, 0755);
        gchar *path = g_build_filename(dir, "settings.ini", NULL);
        gchar *content = g_strdup_printf(
            "[Settings]\n"
            "gtk-application-prefer-dark-theme=%d\n"
            "gtk-font-name=DejaVu Sans %d\n", dark ? 1 : 0, font_size);
        g_file_set_contents(path, content, -1, NULL);
        g_free(content);
        g_free(path);
        g_free(dir);
    }
}

static gboolean pref_dark(void) {
    gboolean dark = TRUE;
    gchar *ini = g_build_filename(g_get_user_config_dir(),
        "gtk-3.0", "settings.ini", NULL);
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, ini, G_KEY_FILE_NONE, NULL))
        dark = g_key_file_get_boolean(kf, "Settings",
            "gtk-application-prefer-dark-theme", NULL);
    g_key_file_free(kf);
    g_free(ini);
    return dark;
}

static GtkWidget *font_spin;

static void apply_appearance(GObject *o, GParamSpec *p, gpointer data) {
    (void)o; (void)p;
    gboolean dark = gtk_switch_get_active(GTK_SWITCH(data));
    int size = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(font_spin));
    write_gtk_settings(dark, size);
    g_object_set(gtk_settings_get_default(),
        "gtk-application-prefer-dark-theme", dark, NULL);
    gchar *font = g_strdup_printf("DejaVu Sans %d", size);
    g_object_set(gtk_settings_get_default(), "gtk-font-name", font, NULL);
    g_free(font);
}

static void on_font_changed(GtkSpinButton *sb, gpointer data) {
    (void)sb;
    apply_appearance(NULL, NULL, data);
}

static void on_pick_wallpaper(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    GtkWidget *dlg = gtk_file_chooser_dialog_new("Выберите обои",
        GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_OPEN,
        "Отмена", GTK_RESPONSE_CANCEL, "Выбрать", GTK_RESPONSE_ACCEPT,
        NULL);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        gchar *file = gtk_file_chooser_get_filename(
            GTK_FILE_CHOOSER(dlg));
        if (file) {
            /* запомнить выбор и применить сразу, если есть swaybg */
            gchar *conf = g_build_filename(g_get_user_config_dir(),
                "shidikusik", NULL);
            g_mkdir_with_parents(conf, 0755);
            gchar *path = g_build_filename(conf, "wallpaper", NULL);
            g_file_set_contents(path, file, -1, NULL);
            g_free(path);
            g_free(conf);

            if (g_find_program_in_path("swaybg")) {
                run_async("pkill swaybg");
                gchar *cmd = g_strdup_printf("swaybg -m fill -i '%s'", file);
                run_async(cmd);
                g_free(cmd);
            }
            g_free(file);
        }
    }
    gtk_widget_destroy(dlg);
}

static GtkWidget *build_appearance_page(void) {
    GtkWidget *box = page_box();
    gtk_box_pack_start(GTK_BOX(box), section("Тема"), FALSE, FALSE, 0);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *label = gtk_label_new("Тёмная тема");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    GtkWidget *sw = gtk_switch_new();
    gtk_widget_set_halign(sw, GTK_ALIGN_END);
    gtk_switch_set_active(GTK_SWITCH(sw), pref_dark());
    gtk_box_pack_start(GTK_BOX(row), label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), sw, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);

    GtkWidget *frow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *flabel = gtk_label_new("Размер шрифта");
    gtk_widget_set_halign(flabel, GTK_ALIGN_START);
    font_spin = gtk_spin_button_new_with_range(8, 18, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(font_spin), 11);
    gtk_box_pack_start(GTK_BOX(frow), flabel, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(frow), font_spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), frow, FALSE, FALSE, 0);

    g_signal_connect(sw, "notify::active",
        G_CALLBACK(apply_appearance), sw);
    g_signal_connect(font_spin, "value-changed",
        G_CALLBACK(on_font_changed), sw);

    gtk_box_pack_start(GTK_BOX(box), section("Рабочий стол"),
        FALSE, FALSE, 0);
    GtkWidget *wall = gtk_button_new_with_label("Выбрать обои…");
    gtk_widget_set_halign(wall, GTK_ALIGN_START);
    g_signal_connect(wall, "clicked", G_CALLBACK(on_pick_wallpaper), NULL);
    gtk_box_pack_start(GTK_BOX(box), wall, FALSE, FALSE, 0);

    GtkWidget *hint = gtk_label_new(
        "Тема применяется к новым запускаемым приложениям.\n"
        "Для обоев нужен пакет swaybg (есть в магазине приложений).");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint),
        "dim-label");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 6);
    return scrolled_page(box);
}

/* ---------- Сеть ---------- */

static void refresh_network(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    gchar *out = run_capture(
        "nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device");
    gtk_label_set_text(GTK_LABEL(net_label),
        (out && *out) ? out : "NetworkManager недоступен");
    g_free(out);

    /* список Wi-Fi */
    GList *rows = gtk_container_get_children(GTK_CONTAINER(wifi_list));
    for (GList *l = rows; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(rows);

    gchar *wifi = run_capture(
        "nmcli -t -f SSID,SIGNAL device wifi list --rescan no");
    if (wifi && *wifi) {
        gchar **lines = g_strsplit(wifi, "\n", -1);
        int shown = 0;
        for (gchar **l = lines; *l && shown < 10; l++) {
            if (!**l)
                continue;
            gchar **f = g_strsplit(*l, ":", 2);
            if (f[0] && *f[0]) {
                gchar *text = g_strdup_printf("%s  ·  %s%%", f[0],
                    f[1] ? f[1] : "?");
                GtkWidget *row = gtk_list_box_row_new();
                GtkWidget *lab = gtk_label_new(text);
                gtk_widget_set_halign(lab, GTK_ALIGN_START);
                gtk_widget_set_margin_start(lab, 8);
                gtk_widget_set_margin_top(lab, 4);
                gtk_widget_set_margin_bottom(lab, 4);
                gtk_container_add(GTK_CONTAINER(row), lab);
                gtk_container_add(GTK_CONTAINER(wifi_list), row);
                g_free(text);
                shown++;
            }
            g_strfreev(f);
        }
        g_strfreev(lines);
    }
    gtk_widget_show_all(wifi_list);
    g_free(wifi);
}

static void on_nmtui(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    spawn_in_terminal("nmtui");
}

/* ---------- мобильный интернет (USB-модем) ---------- */

static GtkWidget *modem_label;

static void refresh_modems(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    if (!g_find_program_in_path("mmcli")) {
        gtk_label_set_text(GTK_LABEL(modem_label),
            "ModemManager не установлен");
        return;
    }
    gchar *out = run_capture("mmcli -L");
    if (!out || !*out || strstr(out, "No modems")) {
        gtk_label_set_text(GTK_LABEL(modem_label),
            "Модемы не найдены.\n"
            "Телефон в режиме USB-модема и свистки HiLink подключаются "
            "сами — смотрите раздел «Устройства» выше.");
    } else {
        gtk_label_set_text(GTK_LABEL(modem_label), out);
    }
    g_free(out);
}

/* Создание GSM-подключения: спрашиваем APN и, если нужно, логин/пароль. */
static void on_modem_connect(GtkButton *b, gpointer data) {
    (void)b; (void)data;

    GtkWidget *dlg = gtk_dialog_new_with_buttons("Мобильный интернет",
        GTK_WINDOW(window), GTK_DIALOG_MODAL,
        "Отмена", GTK_RESPONSE_CANCEL, "Подключить", GTK_RESPONSE_OK, NULL);
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(area), 12);
    gtk_box_set_spacing(GTK_BOX(area), 6);

    GtkWidget *hint = gtk_label_new(
        "APN оператора (примеры):\n"
        "  МТС — internet.mts.ru, логин/пароль mts/mts\n"
        "  Билайн — internet.beeline.ru, beeline/beeline\n"
        "  МегаФон — internet, gdata/gdata\n"
        "  Tele2 — internet.tele2.ru, без логина");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(area), hint, FALSE, FALSE, 0);

    GtkWidget *apn = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(apn), "APN, например internet");
    gtk_box_pack_start(GTK_BOX(area), apn, FALSE, FALSE, 0);
    GtkWidget *user = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(user), "логин (если нужен)");
    gtk_box_pack_start(GTK_BOX(area), user, FALSE, FALSE, 0);
    GtkWidget *pass = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(pass), "пароль (если нужен)");
    gtk_box_pack_start(GTK_BOX(area), pass, FALSE, FALSE, 0);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        const gchar *a = gtk_entry_get_text(GTK_ENTRY(apn));
        const gchar *u = gtk_entry_get_text(GTK_ENTRY(user));
        const gchar *p = gtk_entry_get_text(GTK_ENTRY(pass));
        if (a && *a) {
            GString *cmd = g_string_new(
                "nmcli connection add type gsm ifname '*' "
                "con-name shidik-mobile");
            g_string_append_printf(cmd, " apn '%s'", a);
            if (u && *u)
                g_string_append_printf(cmd, " gsm.username '%s'", u);
            if (p && *p)
                g_string_append_printf(cmd, " gsm.password '%s'", p);
            run_async(cmd->str);
            g_string_free(cmd, TRUE);
            /* поднимаем соединение чуть позже — nmcli добавляет не мгновенно */
            run_async("sh -c 'sleep 2; nmcli connection up shidik-mobile'");
            gtk_label_set_text(GTK_LABEL(modem_label), "Подключаюсь…");
        }
    }
    gtk_widget_destroy(dlg);
}

static void on_open_shade(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    run_async("shidikshade --toggle");
}

static GtkWidget *build_network_page(void) {
    GtkWidget *box = page_box();
    gtk_box_pack_start(GTK_BOX(box), section("Устройства"),
        FALSE, FALSE, 0);

    net_label = gtk_label_new("");
    gtk_widget_set_halign(net_label, GTK_ALIGN_START);
    gtk_label_set_selectable(GTK_LABEL(net_label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), net_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), section("Доступные сети Wi-Fi"),
        FALSE, FALSE, 0);
    GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(sc, -1, 160);
    wifi_list = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(sc), wifi_list);
    gtk_box_pack_start(GTK_BOX(box), sc, FALSE, FALSE, 0);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *refresh = gtk_button_new_with_label("Обновить");
    g_signal_connect(refresh, "clicked",
        G_CALLBACK(refresh_network), NULL);
    GtkWidget *shade = gtk_button_new_with_label("Подключиться (шторка)");
    g_signal_connect(shade, "clicked", G_CALLBACK(on_open_shade), NULL);
    GtkWidget *conf = gtk_button_new_with_label("nmtui");
    g_signal_connect(conf, "clicked", G_CALLBACK(on_nmtui), NULL);
    gtk_box_pack_start(GTK_BOX(row), refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), shade, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), conf, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), section("Мобильный интернет (USB-модем)"),
        FALSE, FALSE, 0);
    modem_label = gtk_label_new("");
    gtk_widget_set_halign(modem_label, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(modem_label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(modem_label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), modem_label, FALSE, FALSE, 0);

    GtkWidget *mrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *mrefresh = gtk_button_new_with_label("Найти модемы");
    g_signal_connect(mrefresh, "clicked", G_CALLBACK(refresh_modems), NULL);
    GtkWidget *mconnect = gtk_button_new_with_label("Подключить…");
    g_signal_connect(mconnect, "clicked", G_CALLBACK(on_modem_connect), NULL);
    gtk_box_pack_start(GTK_BOX(mrow), mrefresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mrow), mconnect, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), mrow, FALSE, FALSE, 0);

    refresh_network(NULL, NULL);
    refresh_modems(NULL, NULL);
    return scrolled_page(box);
}

/* ---------- Bluetooth ---------- */

static void refresh_bt(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    gchar *out = run_capture("bluetoothctl show");
    if (!out || !*out) {
        gtk_label_set_text(GTK_LABEL(bt_label),
            "Адаптер Bluetooth не найден (или BlueZ не запущен)");
    } else {
        gboolean powered = strstr(out, "Powered: yes") != NULL;
        gchar *devices = run_capture("bluetoothctl devices");
        gchar *text = g_strdup_printf("Адаптер: %s\n\nСопряжённые:\n%s",
            powered ? "включён" : "выключен",
            (devices && *devices) ? devices : "нет");
        gtk_label_set_text(GTK_LABEL(bt_label), text);
        g_free(text);
        g_free(devices);
    }
    g_free(out);
}

static void on_bt_power(GtkButton *b, gpointer data) {
    (void)b;
    const gchar *on = data;
    if (g_strcmp0(on, "on") == 0) {
        run_async("rfkill unblock bluetooth");
        run_async("bluetoothctl power on");
    } else {
        run_async("bluetoothctl power off");
    }
}

static void on_bt_pair(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    /* интерактивное сопряжение удобнее делать в консоли bluetoothctl */
    spawn_in_terminal("bluetoothctl");
}

static GtkWidget *build_bluetooth_page(void) {
    GtkWidget *box = page_box();
    gtk_box_pack_start(GTK_BOX(box), section("Состояние"), FALSE, FALSE, 0);

    bt_label = gtk_label_new("");
    gtk_widget_set_halign(bt_label, GTK_ALIGN_START);
    gtk_label_set_selectable(GTK_LABEL(bt_label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), bt_label, FALSE, FALSE, 0);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *on = gtk_button_new_with_label("Включить");
    g_signal_connect(on, "clicked", G_CALLBACK(on_bt_power), "on");
    GtkWidget *off = gtk_button_new_with_label("Выключить");
    g_signal_connect(off, "clicked", G_CALLBACK(on_bt_power), "off");
    GtkWidget *pair = gtk_button_new_with_label("Сопряжение…");
    g_signal_connect(pair, "clicked", G_CALLBACK(on_bt_pair), NULL);
    GtkWidget *refresh = gtk_button_new_with_label("Обновить");
    g_signal_connect(refresh, "clicked", G_CALLBACK(refresh_bt), NULL);
    gtk_box_pack_start(GTK_BOX(row), on, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), off, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), pair, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 6);

    GtkWidget *hint = gtk_label_new(
        "Сопряжение: в открывшемся терминале введите\n"
        "  scan on → power on → pair <MAC> → trust <MAC> → connect <MAC>");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint),
        "dim-label");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);

    refresh_bt(NULL, NULL);
    return scrolled_page(box);
}

/* ---------- Звук ---------- */

static void on_volume(GtkRange *r, gpointer data) {
    (void)data;
    gchar *cmd = g_strdup_printf(
        "wpctl set-volume @DEFAULT_AUDIO_SINK@ %.2f",
        gtk_range_get_value(r) / 100.0);
    run_async(cmd);
    g_free(cmd);
}

static void on_mute(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    run_async("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle");
}

static GtkWidget *build_sound_page(void) {
    GtkWidget *box = page_box();
    gtk_box_pack_start(GTK_BOX(box), section("Громкость"), FALSE, FALSE, 0);

    GtkWidget *scale = gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, 0, 100, 5);
    gtk_scale_set_draw_value(GTK_SCALE(scale), TRUE);

    gchar *out = run_capture("wpctl get-volume @DEFAULT_AUDIO_SINK@");
    double v = 0.5;
    if (out) {
        const char *p = strstr(out, "Volume:");
        if (p)
            v = g_ascii_strtod(p + 7, NULL);
        g_free(out);
    }
    gtk_range_set_value(GTK_RANGE(scale), v * 100.0);
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_volume), NULL);
    gtk_box_pack_start(GTK_BOX(box), scale, FALSE, FALSE, 0);

    GtkWidget *mute = gtk_button_new_with_label("Выключить/включить звук");
    gtk_widget_set_halign(mute, GTK_ALIGN_START);
    g_signal_connect(mute, "clicked", G_CALLBACK(on_mute), NULL);
    gtk_box_pack_start(GTK_BOX(box), mute, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), section("Устройства вывода"),
        FALSE, FALSE, 0);
    gchar *sinks = run_capture("wpctl status");
    GtkWidget *l = gtk_label_new(
        (sinks && *sinks) ? sinks : "PipeWire не отвечает");
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_label_set_selectable(GTK_LABEL(l), TRUE);
    gtk_box_pack_start(GTK_BOX(box), l, FALSE, FALSE, 0);
    g_free(sinks);
    return scrolled_page(box);
}

/* ---------- Экраны ---------- */

static GtkWidget *build_displays_page(void) {
    GtkWidget *box = page_box();
    gtk_box_pack_start(GTK_BOX(box), section("Подключённые экраны"),
        FALSE, FALSE, 0);

    GDir *dir = g_dir_open("/sys/class/drm", 0, NULL);
    gboolean found = FALSE;
    const gchar *name;
    while (dir && (name = g_dir_read_name(dir)) != NULL) {
        if (!strchr(name, '-'))
            continue;
        gchar *status_path = g_strdup_printf(
            "/sys/class/drm/%s/status", name);
        gchar *status = NULL;
        g_file_get_contents(status_path, &status, NULL, NULL);
        if (status && g_str_has_prefix(status, "connected")) {
            gchar *modes_path = g_strdup_printf(
                "/sys/class/drm/%s/modes", name);
            gchar *modes = NULL;
            g_file_get_contents(modes_path, &modes, NULL, NULL);
            gchar *first = modes ? g_strdelimit(modes, "\n", '\0') : NULL;
            gtk_box_pack_start(GTK_BOX(box),
                info_row(name, first ? first : "нет режимов"),
                FALSE, FALSE, 0);
            found = TRUE;
            g_free(modes);
            g_free(modes_path);
        }
        g_free(status);
        g_free(status_path);
    }
    if (dir)
        g_dir_close(dir);
    if (!found)
        gtk_box_pack_start(GTK_BOX(box),
            gtk_label_new("Подключённые экраны не найдены"),
            FALSE, FALSE, 0);

    GtkWidget *hint = gtk_label_new(
        "Смена разрешения и раскладка мониторов — в дорожной карте\n"
        "(нужен протокол wlr-output-management в композиторе).");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint),
        "dim-label");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 8);
    return scrolled_page(box);
}

/* ---------- Питание ---------- */

static GtkWidget *build_power_page(void) {
    GtkWidget *box = page_box();
    gtk_box_pack_start(GTK_BOX(box), section("Батарея"), FALSE, FALSE, 0);

    gchar *cap = NULL, *status = NULL;
    g_file_get_contents("/sys/class/power_supply/BAT0/capacity",
        &cap, NULL, NULL);
    g_file_get_contents("/sys/class/power_supply/BAT0/status",
        &status, NULL, NULL);
    if (cap) {
        g_strchomp(cap);
        if (status)
            g_strchomp(status);
        gchar *s = g_strdup_printf("%s%% (%s)", cap,
            status ? status : "?");
        gtk_box_pack_start(GTK_BOX(box), info_row("Заряд", s),
            FALSE, FALSE, 0);
        g_free(s);
    } else {
        gtk_box_pack_start(GTK_BOX(box),
            gtk_label_new("Батарея не обнаружена — питание от сети"),
            FALSE, FALSE, 0);
    }
    g_free(cap);
    g_free(status);

    gtk_box_pack_start(GTK_BOX(box), section("Действия"), FALSE, FALSE, 0);
    const char *acts[][2] = {
        { "Заблокировать экран", "lock" },
        { "Спящий режим",        "suspend" },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(acts); i++) {
        GtkWidget *btn = gtk_button_new_with_label(acts[i][0]);
        gtk_widget_set_halign(btn, GTK_ALIGN_START);
        gchar *cmd = g_strdup_printf("shidik-session-ctl %s", acts[i][1]);
        g_signal_connect_data(btn, "clicked", G_CALLBACK(run_async), cmd,
            NULL, G_CONNECT_SWAPPED);
        gtk_box_pack_start(GTK_BOX(box), btn, FALSE, FALSE, 0);
    }
    return scrolled_page(box);
}

/* ---------- Обновления ---------- */

static void log_line(const gchar *text) {
    GtkTextBuffer *buf = gtk_text_view_get_buffer(
        GTK_TEXT_VIEW(update_log));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_insert(buf, &end, text, -1);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(update_log), &end,
        0.0, FALSE, 0, 0);
}

static void run_apt(const gchar *args) {
    gchar *cmd = g_strdup_printf(
        "pkexec env DEBIAN_FRONTEND=noninteractive apt-get %s", args);
    gchar **argv = NULL;
    g_shell_parse_argv(cmd, NULL, &argv, NULL);
    g_free(cmd);

    gint out_fd;
    GPid pid;
    if (!g_spawn_async_with_pipes(NULL, argv, NULL,
            G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD |
            G_SPAWN_STDERR_TO_DEV_NULL,
            NULL, NULL, &pid, NULL, &out_fd, NULL, NULL)) {
        log_line("Не удалось запустить apt (нет pkexec?)\n");
        g_strfreev(argv);
        return;
    }
    g_strfreev(argv);

    GIOChannel *ch = g_io_channel_unix_new(out_fd);
    g_io_channel_set_encoding(ch, NULL, NULL);
    gchar *line = NULL;
    gsize len;
    while (g_io_channel_read_line(ch, &line, &len, NULL, NULL) ==
            G_IO_STATUS_NORMAL && line) {
        log_line(line);
        g_free(line);
        line = NULL;
        while (gtk_events_pending())
            gtk_main_iteration();
    }
    g_io_channel_shutdown(ch, FALSE, NULL);
    g_io_channel_unref(ch);
    g_spawn_close_pid(pid);
    log_line("\n— готово —\n\n");
}

static void on_check_updates(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    log_line("Обновляю списки пакетов…\n");
    run_apt("update");
    log_line("Доступные обновления:\n");
    gchar *out = run_capture("apt list --upgradable 2>/dev/null");
    log_line(out && *out ? out : "нет");
    log_line("\n");
    g_free(out);
}

static void on_do_upgrade(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    log_line("Устанавливаю обновления…\n");
    run_apt("-y upgrade");
}

static GtkWidget *build_updates_page(void) {
    GtkWidget *box = page_box();
    gtk_box_pack_start(GTK_BOX(box), section("Обновление системы"),
        FALSE, FALSE, 0);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *check = gtk_button_new_with_label("Проверить обновления");
    g_signal_connect(check, "clicked",
        G_CALLBACK(on_check_updates), NULL);
    GtkWidget *upgrade = gtk_button_new_with_label("Обновить всё");
    g_signal_connect(upgrade, "clicked",
        G_CALLBACK(on_do_upgrade), NULL);
    gtk_box_pack_start(GTK_BOX(row), check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), upgrade, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);

    GtkWidget *sc = gtk_scrolled_window_new(NULL, NULL);
    update_log = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(update_log), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(update_log), TRUE);
    gtk_container_add(GTK_CONTAINER(sc), update_log);
    gtk_box_pack_start(GTK_BOX(box), sc, TRUE, TRUE, 0);
    return box; /* лог сам скроллится, страницу не оборачиваем */
}

/* ---------- Пользователь ---------- */

static void on_passwd(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    spawn_in_terminal("passwd");
}

static void on_users_admin(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    spawn_in_terminal("sudo bash");
}

static GtkWidget *build_user_page(void) {
    GtkWidget *box = page_box();
    gtk_box_pack_start(GTK_BOX(box), section("Текущий пользователь"),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box),
        info_row("Имя", g_get_user_name()), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box),
        info_row("Полное имя", g_get_real_name()), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box),
        info_row("Домашний каталог", g_get_home_dir()), FALSE, FALSE, 0);

    gchar *groups = run_capture("id -Gn");
    gtk_box_pack_start(GTK_BOX(box), info_row("Группы", groups),
        FALSE, FALSE, 0);
    g_free(groups);

    gtk_box_pack_start(GTK_BOX(box), section("Действия"), FALSE, FALSE, 0);
    GtkWidget *pw = gtk_button_new_with_label("Сменить пароль");
    gtk_widget_set_halign(pw, GTK_ALIGN_START);
    g_signal_connect(pw, "clicked", G_CALLBACK(on_passwd), NULL);
    gtk_box_pack_start(GTK_BOX(box), pw, FALSE, FALSE, 0);

    GtkWidget *admin = gtk_button_new_with_label(
        "Консоль администратора (sudo)");
    gtk_widget_set_halign(admin, GTK_ALIGN_START);
    g_signal_connect(admin, "clicked", G_CALLBACK(on_users_admin), NULL);
    gtk_box_pack_start(GTK_BOX(box), admin, FALSE, FALSE, 0);
    return scrolled_page(box);
}

/* ---------- main ---------- */

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Настройки — Shidik-Control");
    gtk_window_set_default_size(GTK_WINDOW(window), 780, 560);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack),
        GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    GtkWidget *sidebar = gtk_stack_sidebar_new();
    gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar),
        GTK_STACK(stack));

    gtk_stack_add_titled(GTK_STACK(stack), build_system_page(),
        "system", "О системе");
    gtk_stack_add_titled(GTK_STACK(stack), build_appearance_page(),
        "appearance", "Внешний вид");
    gtk_stack_add_titled(GTK_STACK(stack), build_network_page(),
        "network", "Сеть");
    gtk_stack_add_titled(GTK_STACK(stack), build_bluetooth_page(),
        "bluetooth", "Bluetooth");
    gtk_stack_add_titled(GTK_STACK(stack), build_sound_page(),
        "sound", "Звук");
    gtk_stack_add_titled(GTK_STACK(stack), build_displays_page(),
        "displays", "Экраны");
    gtk_stack_add_titled(GTK_STACK(stack), build_power_page(),
        "power", "Питание");
    gtk_stack_add_titled(GTK_STACK(stack), build_updates_page(),
        "updates", "Обновления");
    gtk_stack_add_titled(GTK_STACK(stack), build_user_page(),
        "user", "Пользователь");

    gtk_box_pack_start(GTK_BOX(hbox), sidebar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), stack, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(window), hbox);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
