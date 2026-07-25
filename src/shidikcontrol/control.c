/* shidik-control — центр настроек ShidikDE (v1).
 *
 * Единое окно с боковой панелью (GtkStackSidebar) и страницами:
 *   Система     — hostname, ядро, uptime, память;
 *   Внешний вид — тёмная/светлая тема (пишет settings.ini GTK 3/4);
 *   Сеть        — статус устройств NetworkManager, запуск nmtui;
 *   Экраны      — подключённые выводы из /sys/class/drm;
 *   Пользователь— текущий пользователь, группы, смена пароля.
 *
 * Тяжёлые операции делегируются штатным инструментам (nmtui, passwd),
 * запускаемым в shidik-term — модуль остаётся тонким.
 *
 * Зависимости: libgtk-3-dev
 */
#include <gtk/gtk.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <string.h>

/* ---------- утилиты ---------- */

static gchar *run_capture(const gchar *cmd) {
    gchar *out = NULL;
    if (!g_spawn_command_line_sync(cmd, &out, NULL, NULL, NULL))
        return NULL;
    return out;
}

static void spawn_in_terminal(const gchar *cmd) {
    gchar *full = g_strdup_printf("shidik-term -e %s", cmd);
    g_spawn_command_line_async(full, NULL);
    g_free(full);
}

static GtkWidget *info_row(const gchar *key, const gchar *value) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *k = gtk_label_new(key);
    gtk_widget_set_halign(k, GTK_ALIGN_START);
    gtk_widget_set_size_request(k, 140, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(k),
        "dim-label");
    GtkWidget *v = gtk_label_new(value ? value : "—");
    gtk_widget_set_halign(v, GTK_ALIGN_START);
    gtk_label_set_selectable(GTK_LABEL(v), TRUE);
    gtk_box_pack_start(GTK_BOX(row), k, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), v, TRUE, TRUE, 0);
    return row;
}

static GtkWidget *page_box(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 18);
    return box;
}

/* ---------- Система ---------- */

static GtkWidget *build_system_page(void) {
    GtkWidget *box = page_box();

    if (g_file_test("/usr/share/shidikudik/logo.svg", G_FILE_TEST_EXISTS)) {
        GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(
            "/usr/share/shidikudik/logo.svg", 96, 96, NULL);
        if (pb) {
            GtkWidget *logo = gtk_image_new_from_pixbuf(pb);
            g_object_unref(pb);
            gtk_widget_set_halign(logo, GTK_ALIGN_START);
            gtk_box_pack_start(GTK_BOX(box), logo, FALSE, FALSE, 0);
        }
    }

    struct utsname un;
    uname(&un);
    gtk_box_pack_start(GTK_BOX(box),
        info_row("ОС", "ShidikudikOS"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box),
        info_row("Хост", g_get_host_name()), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box),
        info_row("Ядро", un.release), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box),
        info_row("Архитектура", un.machine), FALSE, FALSE, 0);

    gchar *contents = NULL;
    if (g_file_get_contents("/proc/uptime", &contents, NULL, NULL)) {
        double up = g_ascii_strtod(contents, NULL);
        gchar *s = g_strdup_printf("%d ч %d мин",
            (int)(up / 3600), (int)(up / 60) % 60);
        gtk_box_pack_start(GTK_BOX(box),
            info_row("Аптайм", s), FALSE, FALSE, 0);
        g_free(s);
        g_free(contents);
    }
    if (g_file_get_contents("/proc/meminfo", &contents, NULL, NULL)) {
        long kb = 0;
        sscanf(contents, "MemTotal: %ld", &kb);
        gchar *s = g_strdup_printf("%.1f ГиБ", kb / 1048576.0);
        gtk_box_pack_start(GTK_BOX(box),
            info_row("Память", s), FALSE, FALSE, 0);
        g_free(s);
        g_free(contents);
    }
    return box;
}

/* ---------- Внешний вид ---------- */

static void write_gtk_settings(gboolean dark) {
    const gchar *dirs[] = { "gtk-3.0", "gtk-4.0" };
    for (gsize i = 0; i < G_N_ELEMENTS(dirs); i++) {
        gchar *dir = g_build_filename(g_get_user_config_dir(),
            dirs[i], NULL);
        g_mkdir_with_parents(dir, 0755);
        gchar *path = g_build_filename(dir, "settings.ini", NULL);
        gchar *content = g_strdup_printf(
            "[Settings]\n"
            "gtk-application-prefer-dark-theme=%d\n"
            "gtk-font-name=DejaVu Sans 11\n", dark ? 1 : 0);
        g_file_set_contents(path, content, -1, NULL);
        g_free(content);
        g_free(path);
        g_free(dir);
    }
}

static void on_dark_toggled(GObject *sw, GParamSpec *pspec, gpointer data) {
    (void)pspec; (void)data;
    gboolean dark = gtk_switch_get_active(GTK_SWITCH(sw));
    write_gtk_settings(dark);
    g_object_set(gtk_settings_get_default(),
        "gtk-application-prefer-dark-theme", dark, NULL);
}

static GtkWidget *build_appearance_page(void) {
    GtkWidget *box = page_box();

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *label = gtk_label_new("Тёмная тема");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    GtkWidget *sw = gtk_switch_new();
    gtk_widget_set_halign(sw, GTK_ALIGN_END);

    /* текущее значение — из settings.ini */
    gboolean dark = TRUE;
    gchar *ini = g_build_filename(g_get_user_config_dir(),
        "gtk-3.0", "settings.ini", NULL);
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, ini, G_KEY_FILE_NONE, NULL))
        dark = g_key_file_get_boolean(kf, "Settings",
            "gtk-application-prefer-dark-theme", NULL);
    g_key_file_free(kf);
    g_free(ini);
    gtk_switch_set_active(GTK_SWITCH(sw), dark);
    g_signal_connect(sw, "notify::active",
        G_CALLBACK(on_dark_toggled), NULL);

    gtk_box_pack_start(GTK_BOX(row), label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), sw, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);

    GtkWidget *hint = gtk_label_new(
        "Применяется к новым запускаемым приложениям.");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint),
        "dim-label");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);
    return box;
}

/* ---------- Сеть ---------- */

static GtkWidget *net_label;

static void refresh_network(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    gchar *out = run_capture(
        "nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device");
    gtk_label_set_text(GTK_LABEL(net_label),
        (out && *out) ? out : "NetworkManager недоступен");
    g_free(out);
}

static void on_nmtui(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    spawn_in_terminal("nmtui");
}

static GtkWidget *build_network_page(void) {
    GtkWidget *box = page_box();

    net_label = gtk_label_new("");
    gtk_widget_set_halign(net_label, GTK_ALIGN_START);
    gtk_label_set_selectable(GTK_LABEL(net_label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), net_label, FALSE, FALSE, 0);
    refresh_network(NULL, NULL);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *refresh = gtk_button_new_with_label("Обновить");
    g_signal_connect(refresh, "clicked",
        G_CALLBACK(refresh_network), NULL);
    GtkWidget *conf = gtk_button_new_with_label("Настроить сеть (nmtui)");
    g_signal_connect(conf, "clicked", G_CALLBACK(on_nmtui), NULL);
    gtk_box_pack_start(GTK_BOX(row), refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), conf, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    return box;
}

/* ---------- Экраны ---------- */

static GtkWidget *build_displays_page(void) {
    GtkWidget *box = page_box();
    GDir *dir = g_dir_open("/sys/class/drm", 0, NULL);
    gboolean found = FALSE;

    const gchar *name;
    while (dir && (name = g_dir_read_name(dir)) != NULL) {
        if (!strchr(name, '-'))
            continue; /* card0 без коннектора */
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
    return box;
}

/* ---------- Пользователь ---------- */

static void on_passwd(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    spawn_in_terminal("passwd");
}

static GtkWidget *build_user_page(void) {
    GtkWidget *box = page_box();
    gtk_box_pack_start(GTK_BOX(box),
        info_row("Пользователь", g_get_user_name()), FALSE, FALSE, 0);

    gchar *groups = run_capture("id -Gn");
    if (groups)
        g_strchomp(groups);
    gtk_box_pack_start(GTK_BOX(box),
        info_row("Группы", groups), FALSE, FALSE, 0);
    g_free(groups);

    GtkWidget *btn = gtk_button_new_with_label("Сменить пароль");
    gtk_widget_set_halign(btn, GTK_ALIGN_START);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_passwd), NULL);
    gtk_box_pack_start(GTK_BOX(box), btn, FALSE, FALSE, 8);
    return box;
}

/* ---------- main ---------- */

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Настройки — Shidik-Control");
    gtk_window_set_default_size(GTK_WINDOW(window), 640, 440);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack),
        GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    GtkWidget *sidebar = gtk_stack_sidebar_new();
    gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar),
        GTK_STACK(stack));

    gtk_stack_add_titled(GTK_STACK(stack), build_system_page(),
        "system", "Система");
    gtk_stack_add_titled(GTK_STACK(stack), build_appearance_page(),
        "appearance", "Внешний вид");
    gtk_stack_add_titled(GTK_STACK(stack), build_network_page(),
        "network", "Сеть");
    gtk_stack_add_titled(GTK_STACK(stack), build_displays_page(),
        "displays", "Экраны");
    gtk_stack_add_titled(GTK_STACK(stack), build_user_page(),
        "user", "Пользователь");

    gtk_box_pack_start(GTK_BOX(hbox), sidebar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), stack, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(window), hbox);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
