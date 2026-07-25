/* shidikshade — шторка уведомлений и быстрых настроек ShidikDE.
 *
 * Один демон, две роли:
 *
 *  1. Сервер уведомлений org.freedesktop.Notifications (GDBus): любое
 *     приложение (браузер, апдейтер, наш магазин) может показать
 *     всплывающее уведомление. Всплывашка — отдельная layer-shell
 *     поверхность в правом верхнем углу, гаснет сама; текст остаётся в
 *     истории внутри шторки.
 *
 *  2. Шторка: выезжает справа по Win+N или по кнопке панели. Внутри —
 *     Wi-Fi (список сетей и подключение), Bluetooth, громкость,
 *     яркость и история уведомлений.
 *
 * Управление снаружи: `shidikshade --toggle` дёргает метод Toggle у уже
 * запущенного демона через собственное имя org.shidikusik.Shade; если
 * демон не запущен — запускается сам.
 *
 * Реальная работа делегируется штатным утилитам: nmcli (NetworkManager),
 * bluetoothctl/rfkill (BlueZ), wpctl (PipeWire), brightnessctl.
 *
 * Зависимости: libgtk-3-dev libgtk-layer-shell-dev
 */
#include <gtk/gtk.h>
#include <gtk-layer-shell/gtk-layer-shell.h>
#include <string.h>

#define SHADE_BUS_NAME "org.shidikusik.Shade"
#define SHADE_OBJ_PATH "/org/shidikusik/Shade"

static GtkWidget *shade_window;
static GtkWidget *wifi_switch, *bt_switch, *volume_scale, *bright_scale;
static GtkWidget *wifi_list, *notif_list, *wifi_status_label;
static GList *notifications;      /* строки истории */
static guint32 next_notif_id = 1;
static gboolean updating_ui;      /* защита от рекурсии сигналов */

/* ---------- вспомогательное: запуск команд ---------- */

static gchar *run_cmd(const gchar *cmd) {
    gchar *out = NULL;
    gint status = 0;
    if (!g_spawn_command_line_sync(cmd, &out, NULL, &status, NULL)) {
        g_free(out);
        return NULL;
    }
    if (out)
        g_strchomp(out);
    return out;
}

static void run_async(const gchar *cmd) {
    g_spawn_command_line_async(cmd, NULL);
}

/* Освобождение данных, привязанных к сигналу (без каста g_free). */
static void free_closure_data(gpointer data, GClosure *closure) {
    (void)closure;
    g_free(data);
}

/* Обёртка «выполнить команду по клику» — команда живёт с сигналом. */
static void on_run_cmd_clicked(GtkButton *b, gpointer data) {
    (void)b;
    run_async((const gchar *)data);
}

static gboolean have_tool(const gchar *tool) {
    gchar *path = g_find_program_in_path(tool);
    gboolean ok = path != NULL;
    g_free(path);
    return ok;
}

/* ---------- Wi-Fi ---------- */

static gboolean wifi_enabled(void) {
    gchar *out = run_cmd("nmcli radio wifi");
    gboolean on = out && g_str_has_prefix(out, "enabled");
    g_free(out);
    return on;
}

static void clear_list(GtkWidget *list) {
    GList *rows = gtk_container_get_children(GTK_CONTAINER(list));
    for (GList *l = rows; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(rows);
}

static void on_wifi_connect(GtkButton *b, gpointer data) {
    const gchar *ssid = data;

    /* Диалог пароля — обычное окно, композитор ставит его по центру. */
    GtkWidget *dlg = gtk_dialog_new_with_buttons("Подключение к сети",
        GTK_WINDOW(shade_window), GTK_DIALOG_MODAL,
        "Отмена", GTK_RESPONSE_CANCEL, "Подключиться", GTK_RESPONSE_OK,
        NULL);
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(area), 12);

    gchar *msg = g_strdup_printf("Сеть: %s", ssid);
    gtk_box_pack_start(GTK_BOX(area), gtk_label_new(msg), FALSE, FALSE, 4);
    g_free(msg);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry),
        "пароль (пусто — если сеть открытая)");
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    gtk_box_pack_start(GTK_BOX(area), entry, FALSE, FALSE, 4);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        const gchar *pass = gtk_entry_get_text(GTK_ENTRY(entry));
        gchar *cmd = (pass && *pass)
            ? g_strdup_printf("nmcli device wifi connect '%s' password '%s'",
                              ssid, pass)
            : g_strdup_printf("nmcli device wifi connect '%s'", ssid);
        run_async(cmd);
        g_free(cmd);
        gtk_label_set_text(GTK_LABEL(wifi_status_label),
            "Подключаюсь…");
    }
    gtk_widget_destroy(dlg);
    (void)b;
}

static void refresh_wifi_list(void) {
    clear_list(wifi_list);

    if (!have_tool("nmcli")) {
        gtk_label_set_text(GTK_LABEL(wifi_status_label),
            "NetworkManager не установлен");
        return;
    }
    if (!wifi_enabled()) {
        gtk_label_set_text(GTK_LABEL(wifi_status_label), "Wi-Fi выключен");
        return;
    }

    gchar *active = run_cmd(
        "nmcli -t -f NAME connection show --active");
    gchar *out = run_cmd(
        "nmcli -t -f SSID,SIGNAL,SECURITY device wifi list --rescan no");
    if (!out || !*out) {
        gtk_label_set_text(GTK_LABEL(wifi_status_label),
            "Сети не найдены (адаптер есть?)");
        g_free(out);
        g_free(active);
        return;
    }
    gtk_label_set_text(GTK_LABEL(wifi_status_label), "");

    gchar **lines = g_strsplit(out, "\n", -1);
    GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
    int shown = 0;
    for (gchar **l = lines; *l && shown < 12; l++) {
        if (!**l)
            continue;
        gchar **f = g_strsplit(*l, ":", 3);
        if (g_strv_length(f) < 2 || !*f[0]) {
            g_strfreev(f);
            continue;
        }
        if (g_hash_table_contains(seen, f[0])) { /* один SSID — одна строка */
            g_strfreev(f);
            continue;
        }
        g_hash_table_add(seen, g_strdup(f[0]));

        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_container_set_border_width(GTK_CONTAINER(hb), 6);

        gboolean is_active = active && strstr(active, f[0]) != NULL;
        gchar *label = g_strdup_printf("%s%s  ·  %s%%",
            is_active ? "✓ " : "", f[0], f[1]);
        GtkWidget *name = gtk_label_new(label);
        g_free(label);
        gtk_widget_set_halign(name, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(hb), name, TRUE, TRUE, 0);

        GtkWidget *btn = gtk_button_new_with_label(
            is_active ? "Отключить" : "Подключить");
        if (is_active) {
            gchar *cmd = g_strdup_printf(
                "nmcli connection down '%s'", f[0]);
            g_signal_connect_data(btn, "clicked",
                G_CALLBACK(on_run_cmd_clicked), cmd,
                free_closure_data, 0);
        } else {
            g_signal_connect_data(btn, "clicked",
                G_CALLBACK(on_wifi_connect), g_strdup(f[0]),
                free_closure_data, 0);
        }
        gtk_box_pack_start(GTK_BOX(hb), btn, FALSE, FALSE, 0);

        gtk_container_add(GTK_CONTAINER(row), hb);
        gtk_container_add(GTK_CONTAINER(wifi_list), row);
        shown++;
        g_strfreev(f);
    }
    gtk_widget_show_all(wifi_list);
    g_hash_table_destroy(seen);
    g_strfreev(lines);
    g_free(out);
    g_free(active);
}

/* Список сетей появляется не мгновенно после включения адаптера. */
static gboolean refresh_wifi_later(gpointer data) {
    (void)data;
    refresh_wifi_list();
    return G_SOURCE_REMOVE;
}

static void on_rescan_clicked(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    refresh_wifi_list();
}

static void on_wifi_toggled(GObject *sw, GParamSpec *p, gpointer data) {
    (void)p; (void)data;
    if (updating_ui)
        return;
    gboolean on = gtk_switch_get_active(GTK_SWITCH(sw));
    run_async(on ? "nmcli radio wifi on" : "nmcli radio wifi off");
    g_timeout_add(1200, refresh_wifi_later, NULL);
}

/* ---------- Bluetooth ---------- */

static gboolean bt_enabled(void) {
    gchar *out = run_cmd("bluetoothctl show");
    gboolean on = out && strstr(out, "Powered: yes") != NULL;
    g_free(out);
    return on;
}

static void on_bt_toggled(GObject *sw, GParamSpec *p, gpointer data) {
    (void)p; (void)data;
    if (updating_ui)
        return;
    gboolean on = gtk_switch_get_active(GTK_SWITCH(sw));
    if (on) {
        run_async("rfkill unblock bluetooth");
        run_async("bluetoothctl power on");
    } else {
        run_async("bluetoothctl power off");
    }
}

/* ---------- звук и яркость ---------- */

static double get_volume(void) {
    gchar *out = run_cmd("wpctl get-volume @DEFAULT_AUDIO_SINK@");
    double v = 0.5;
    if (out) {
        const char *p = strstr(out, "Volume:");
        if (p)
            v = g_ascii_strtod(p + 7, NULL);
        g_free(out);
    }
    return v;
}

static void on_volume_changed(GtkRange *range, gpointer data) {
    (void)data;
    if (updating_ui)
        return;
    gchar *cmd = g_strdup_printf(
        "wpctl set-volume @DEFAULT_AUDIO_SINK@ %.2f",
        gtk_range_get_value(range) / 100.0);
    run_async(cmd);
    g_free(cmd);
}

static double get_brightness(void) {
    gchar *out = run_cmd("brightnessctl -m");
    double pct = 100.0;
    if (out) {
        /* формат: устройство,класс,текущее,ПРОЦЕНТ%,максимум */
        gchar **f = g_strsplit(out, ",", -1);
        if (g_strv_length(f) >= 4)
            pct = g_ascii_strtod(f[3], NULL);
        g_strfreev(f);
        g_free(out);
    }
    return pct;
}

static void on_bright_changed(GtkRange *range, gpointer data) {
    (void)data;
    if (updating_ui)
        return;
    gchar *cmd = g_strdup_printf("brightnessctl set %d%%",
        (int)gtk_range_get_value(range));
    run_async(cmd);
    g_free(cmd);
}

/* ---------- уведомления ---------- */

static gboolean close_popup(gpointer window) {
    if (GTK_IS_WIDGET(window))
        gtk_widget_destroy(GTK_WIDGET(window));
    return G_SOURCE_REMOVE;
}

/* Всплывашка в правом верхнем углу, гаснет через несколько секунд. */
static void show_popup(const gchar *summary, const gchar *body) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_layer_init_for_window(GTK_WINDOW(win));
    gtk_layer_set_layer(GTK_WINDOW(win), GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_TOP, 48);
    gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, 12);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "popup");
    gtk_container_set_border_width(GTK_CONTAINER(box), 14);
    gtk_widget_set_size_request(box, 320, -1);

    GtkWidget *s = gtk_label_new(NULL);
    gchar *markup = g_markup_printf_escaped("<b>%s</b>", summary);
    gtk_label_set_markup(GTK_LABEL(s), markup);
    g_free(markup);
    gtk_widget_set_halign(s, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(s), TRUE);
    gtk_box_pack_start(GTK_BOX(box), s, FALSE, FALSE, 0);

    if (body && *body) {
        GtkWidget *b = gtk_label_new(body);
        gtk_widget_set_halign(b, GTK_ALIGN_START);
        gtk_label_set_line_wrap(GTK_LABEL(b), TRUE);
        gtk_box_pack_start(GTK_BOX(box), b, FALSE, FALSE, 0);
    }

    gtk_container_add(GTK_CONTAINER(win), box);
    gtk_widget_show_all(win);
    g_timeout_add_seconds(6, close_popup, win);
}

static void add_notification(const gchar *summary, const gchar *body) {
    GDateTime *now = g_date_time_new_now_local();
    gchar *time_str = g_date_time_format(now, "%H:%M");
    g_date_time_unref(now);

    gchar *entry = g_strdup_printf("%s|%s|%s", time_str, summary,
        (body && *body) ? body : "");
    g_free(time_str);

    notifications = g_list_prepend(notifications, entry);
    /* держим последние 30 */
    while (g_list_length(notifications) > 30) {
        GList *last = g_list_last(notifications);
        g_free(last->data);
        notifications = g_list_delete_link(notifications, last);
    }

    show_popup(summary, body);
}

static void refresh_notif_list(void) {
    clear_list(notif_list);
    if (notifications == NULL) {
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *l = gtk_label_new("Уведомлений нет");
        gtk_style_context_add_class(gtk_widget_get_style_context(l),
            "dim-label");
        gtk_widget_set_margin_top(l, 8);
        gtk_widget_set_margin_bottom(l, 8);
        gtk_container_add(GTK_CONTAINER(row), l);
        gtk_container_add(GTK_CONTAINER(notif_list), row);
    }
    for (GList *l = notifications; l; l = l->next) {
        gchar **f = g_strsplit(l->data, "|", 3);
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_container_set_border_width(GTK_CONTAINER(box), 6);

        gchar *head = g_markup_printf_escaped(
            "<b>%s</b>  <span size='small'>%s</span>", f[1], f[0]);
        GtkWidget *hl = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(hl), head);
        g_free(head);
        gtk_widget_set_halign(hl, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(box), hl, FALSE, FALSE, 0);

        if (f[2] && *f[2]) {
            GtkWidget *bl = gtk_label_new(f[2]);
            gtk_widget_set_halign(bl, GTK_ALIGN_START);
            gtk_label_set_line_wrap(GTK_LABEL(bl), TRUE);
            gtk_style_context_add_class(gtk_widget_get_style_context(bl),
                "dim-label");
            gtk_box_pack_start(GTK_BOX(box), bl, FALSE, FALSE, 0);
        }
        gtk_container_add(GTK_CONTAINER(row), box);
        gtk_container_add(GTK_CONTAINER(notif_list), row);
        g_strfreev(f);
    }
    gtk_widget_show_all(notif_list);
}

static void on_clear_notifications(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    g_list_free_full(notifications, g_free);
    notifications = NULL;
    refresh_notif_list();
}

/* ---------- D-Bus: org.freedesktop.Notifications ---------- */

static const gchar *NOTIFY_XML =
    "<node><interface name='org.freedesktop.Notifications'>"
    "  <method name='Notify'>"
    "    <arg type='s' name='app_name' direction='in'/>"
    "    <arg type='u' name='replaces_id' direction='in'/>"
    "    <arg type='s' name='app_icon' direction='in'/>"
    "    <arg type='s' name='summary' direction='in'/>"
    "    <arg type='s' name='body' direction='in'/>"
    "    <arg type='as' name='actions' direction='in'/>"
    "    <arg type='a{sv}' name='hints' direction='in'/>"
    "    <arg type='i' name='expire_timeout' direction='in'/>"
    "    <arg type='u' name='id' direction='out'/>"
    "  </method>"
    "  <method name='CloseNotification'>"
    "    <arg type='u' name='id' direction='in'/>"
    "  </method>"
    "  <method name='GetCapabilities'>"
    "    <arg type='as' name='caps' direction='out'/>"
    "  </method>"
    "  <method name='GetServerInformation'>"
    "    <arg type='s' name='name' direction='out'/>"
    "    <arg type='s' name='vendor' direction='out'/>"
    "    <arg type='s' name='version' direction='out'/>"
    "    <arg type='s' name='spec_version' direction='out'/>"
    "  </method>"
    "</interface></node>";

static void notify_method(GDBusConnection *conn, const gchar *sender,
        const gchar *path, const gchar *iface, const gchar *method,
        GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)data;

    if (g_strcmp0(method, "Notify") == 0) {
        const gchar *app = NULL, *icon = NULL, *summary = NULL, *body = NULL;
        guint32 replaces = 0;
        gint32 timeout = 0;
        g_variant_get(params, "(&su&s&s&sas a{sv}i)",
            &app, &replaces, &icon, &summary, &body, NULL, NULL, &timeout);
        add_notification(summary ? summary : "", body ? body : "");
        if (notif_list)
            refresh_notif_list();
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(u)", next_notif_id++));
        return;
    }
    if (g_strcmp0(method, "GetCapabilities") == 0) {
        const gchar *caps[] = { "body", "persistence", NULL };
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(^as)", caps));
        return;
    }
    if (g_strcmp0(method, "GetServerInformation") == 0) {
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(ssss)", "shidikshade", "ShidikusikOS",
                "1.0", "1.2"));
        return;
    }
    /* CloseNotification и всё остальное — просто подтверждаем */
    g_dbus_method_invocation_return_value(inv, NULL);
}

/* ---------- D-Bus: собственный интерфейс шторки ---------- */

static const gchar *SHADE_XML =
    "<node><interface name='org.shidikusik.Shade'>"
    "  <method name='Toggle'/>"
    "</interface></node>";

static void refresh_quick_settings(void) {
    updating_ui = TRUE;
    gtk_switch_set_active(GTK_SWITCH(wifi_switch), wifi_enabled());
    gtk_switch_set_active(GTK_SWITCH(bt_switch), bt_enabled());
    gtk_range_set_value(GTK_RANGE(volume_scale), get_volume() * 100.0);
    gtk_range_set_value(GTK_RANGE(bright_scale), get_brightness());
    updating_ui = FALSE;
}

static void shade_toggle(void) {
    if (gtk_widget_get_visible(shade_window)) {
        gtk_widget_hide(shade_window);
        return;
    }
    refresh_quick_settings();
    refresh_wifi_list();
    refresh_notif_list();
    gtk_widget_show(shade_window);
}

static void shade_method(GDBusConnection *conn, const gchar *sender,
        const gchar *path, const gchar *iface, const gchar *method,
        GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
    (void)conn; (void)sender; (void)path; (void)iface; (void)params;
    (void)data;
    if (g_strcmp0(method, "Toggle") == 0)
        shade_toggle();
    g_dbus_method_invocation_return_value(inv, NULL);
}

static void on_bus_acquired(GDBusConnection *conn, const gchar *name,
        gpointer data) {
    (void)name; (void)data;
    static const GDBusInterfaceVTable notify_vtable = {
        notify_method, NULL, NULL, { 0 } };
    static const GDBusInterfaceVTable shade_vtable = {
        shade_method, NULL, NULL, { 0 } };

    GDBusNodeInfo *ni = g_dbus_node_info_new_for_xml(NOTIFY_XML, NULL);
    if (ni) {
        g_dbus_connection_register_object(conn,
            "/org/freedesktop/Notifications", ni->interfaces[0],
            &notify_vtable, NULL, NULL, NULL);
        g_dbus_node_info_unref(ni);
    }
    GDBusNodeInfo *si = g_dbus_node_info_new_for_xml(SHADE_XML, NULL);
    if (si) {
        g_dbus_connection_register_object(conn, SHADE_OBJ_PATH,
            si->interfaces[0], &shade_vtable, NULL, NULL, NULL);
        g_dbus_node_info_unref(si);
    }
}

/* ---------- сборка шторки ---------- */

static GtkWidget *toggle_row(const gchar *text, GtkWidget **sw) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    *sw = gtk_switch_new();
    gtk_widget_set_halign(*sw, GTK_ALIGN_END);
    gtk_box_pack_start(GTK_BOX(row), label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), *sw, FALSE, FALSE, 0);
    return row;
}

static GtkWidget *slider_row(const gchar *text, GtkWidget **scale) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *label = gtk_label_new(text);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
        0, 100, 5);
    gtk_scale_set_draw_value(GTK_SCALE(*scale), FALSE);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), *scale, FALSE, FALSE, 0);
    return box;
}

static void build_shade(void) {
    shade_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_layer_init_for_window(GTK_WINDOW(shade_window));
    gtk_layer_set_layer(GTK_WINDOW(shade_window),
        GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_anchor(GTK_WINDOW(shade_window),
        GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(shade_window),
        GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(shade_window),
        GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_margin(GTK_WINDOW(shade_window),
        GTK_LAYER_SHELL_EDGE_TOP, 40);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(shade_window),
        GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(root),
        "shade");
    gtk_container_set_border_width(GTK_CONTAINER(root), 14);
    gtk_widget_set_size_request(root, 340, -1);
    gtk_container_add(GTK_CONTAINER(shade_window), root);

    /* быстрые настройки */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<b>Быстрые настройки</b>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(root), title, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(root), toggle_row("Wi-Fi", &wifi_switch),
        FALSE, FALSE, 0);
    g_signal_connect(wifi_switch, "notify::active",
        G_CALLBACK(on_wifi_toggled), NULL);

    wifi_status_label = gtk_label_new("");
    gtk_widget_set_halign(wifi_status_label, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(wifi_status_label), "dim-label");
    gtk_box_pack_start(GTK_BOX(root), wifi_status_label, FALSE, FALSE, 0);

    GtkWidget *wifi_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(wifi_scroll, -1, 150);
    wifi_list = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(wifi_scroll), wifi_list);
    gtk_box_pack_start(GTK_BOX(root), wifi_scroll, FALSE, FALSE, 0);

    GtkWidget *rescan = gtk_button_new_with_label("Обновить список сетей");
    g_signal_connect(rescan, "clicked",
        G_CALLBACK(on_rescan_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(root), rescan, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(root), toggle_row("Bluetooth", &bt_switch),
        FALSE, FALSE, 0);
    g_signal_connect(bt_switch, "notify::active",
        G_CALLBACK(on_bt_toggled), NULL);

    gtk_box_pack_start(GTK_BOX(root),
        slider_row("Громкость", &volume_scale), FALSE, FALSE, 0);
    g_signal_connect(volume_scale, "value-changed",
        G_CALLBACK(on_volume_changed), NULL);

    gtk_box_pack_start(GTK_BOX(root),
        slider_row("Яркость", &bright_scale), FALSE, FALSE, 0);
    g_signal_connect(bright_scale, "value-changed",
        G_CALLBACK(on_bright_changed), NULL);

    gtk_box_pack_start(GTK_BOX(root),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    /* уведомления */
    GtkWidget *nhead = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *ntitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(ntitle), "<b>Уведомления</b>");
    gtk_widget_set_halign(ntitle, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(nhead), ntitle, TRUE, TRUE, 0);
    GtkWidget *clear = gtk_button_new_with_label("Очистить");
    g_signal_connect(clear, "clicked",
        G_CALLBACK(on_clear_notifications), NULL);
    gtk_box_pack_start(GTK_BOX(nhead), clear, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), nhead, FALSE, FALSE, 0);

    GtkWidget *nscroll = gtk_scrolled_window_new(NULL, NULL);
    notif_list = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(nscroll), notif_list);
    gtk_box_pack_start(GTK_BOX(root), nscroll, TRUE, TRUE, 0);

    GtkWidget *close = gtk_button_new_with_label("Закрыть");
    g_signal_connect_swapped(close, "clicked",
        G_CALLBACK(gtk_widget_hide), shade_window);
    gtk_box_pack_start(GTK_BOX(root), close, FALSE, FALSE, 0);

    gtk_widget_show_all(root);
}

/* ---------- main ---------- */

int main(int argc, char *argv[]) {
    gboolean toggle_only = (argc > 1 &&
        g_strcmp0(argv[1], "--toggle") == 0);

    /* Уже запущенному демону просто говорим «покажись». */
    if (toggle_only) {
        GDBusConnection *bus =
            g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
        if (bus) {
            GError *err = NULL;
            g_dbus_connection_call_sync(bus, SHADE_BUS_NAME,
                SHADE_OBJ_PATH, SHADE_BUS_NAME, "Toggle", NULL, NULL,
                G_DBUS_CALL_FLAGS_NO_AUTO_START, 800, NULL, &err);
            g_object_unref(bus);
            if (err == NULL)
                return 0; /* демон услышал — выходим */
            g_error_free(err);
        }
        /* демона нет — запускаемся и показываем шторку сразу */
    }

    gtk_init(&argc, &argv);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        ".shade { background-color: #1a1b26; color: #c0caf5;"
        "  border-left: 1px solid #2f3550; }"
        ".popup { background-color: #24283b; color: #c0caf5;"
        "  border-radius: 12px; border: 1px solid #2f3550; }"
        "list { background-color: transparent; }"
        "row { border-bottom: 1px solid rgba(192,202,245,0.08); }", -1,
        NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    build_shade();

    g_bus_own_name(G_BUS_TYPE_SESSION, "org.freedesktop.Notifications",
        G_BUS_NAME_OWNER_FLAGS_REPLACE, on_bus_acquired, NULL, NULL,
        NULL, NULL);
    g_bus_own_name(G_BUS_TYPE_SESSION, SHADE_BUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_REPLACE, NULL, NULL, NULL, NULL, NULL);

    if (toggle_only)
        shade_toggle();

    gtk_main();
    return 0;
}
