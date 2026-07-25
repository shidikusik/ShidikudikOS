/* shidikpanel — панель ShidikDE.
 *
 * GTK3 + gtk-layer-shell: окно закрепляется поверх рабочего стола по
 * протоколу wlr-layer-shell (композитор shidikwm должен создать глобал
 * zwlr_layer_shell_v1 — см. TODO в shidikwm/main.c; до тех пор панель
 * работает и как обычное окно).
 *
 * Состав панели (слева направо):
 *   [☰ меню]  [список окон*]  ...  [загрузка CPU | батарея]  [часы]  [⏻]
 *
 * (*) Список окон требует протокола wlr-foreign-toplevel-management-v1;
 *     здесь оставлена заглушка с пояснением, как его подключить.
 *
 * Зависимости: libgtk-3-dev libgtk-layer-shell-dev
 */
#include <gtk/gtk.h>
#include <gtk-layer-shell/gtk-layer-shell.h>
#include <stdio.h>

static GtkWidget *clock_label;
static GtkWidget *status_label;

/* ---- часы + системный статус, обновление раз в секунду ---- */

static gboolean update_status(gpointer data) {
    (void)data;

    /* время */
    GDateTime *now = g_date_time_new_now_local();
    gchar *t = g_date_time_format(now, "%H:%M:%S  %d.%m.%Y");
    gtk_label_set_text(GTK_LABEL(clock_label), t);
    g_free(t);
    g_date_time_unref(now);

    /* load average */
    double load = 0.0;
    FILE *f = fopen("/proc/loadavg", "r");
    if (f) {
        if (fscanf(f, "%lf", &load) != 1)
            load = 0.0;
        fclose(f);
    }

    /* батарея (если есть) */
    int battery = -1;
    f = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    if (f) {
        if (fscanf(f, "%d", &battery) != 1)
            battery = -1;
        fclose(f);
    }

    gchar *status = (battery >= 0)
        ? g_strdup_printf("cpu %.2f  ·  bat %d%%", load, battery)
        : g_strdup_printf("cpu %.2f", load);
    gtk_label_set_text(GTK_LABEL(status_label), status);
    g_free(status);

    return G_SOURCE_CONTINUE;
}

/* ---- кнопки ---- */

static void spawn(const char *cmd) {
    gchar *argv[] = { "/bin/sh", "-c", (gchar *)cmd, NULL };
    g_spawn_async(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL, NULL);
}

static void on_menu_clicked(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    spawn("shidiklaunch");
}

static void on_shade_clicked(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    spawn("shidikshade --toggle");
}

/* Диалог питания — отдельное окно по центру экрана.
 *
 * Раньше это был GtkPopover, привязанный к кнопке панели. Панель —
 * layer-shell поверхность у верхнего края, поэтому popover выезжал
 * вверх за пределы экрана. Обычное окно композитор центрирует сам. */
static void on_power_action(GtkButton *b, gpointer data) {
    GtkWidget *dialog = gtk_widget_get_toplevel(GTK_WIDGET(b));
    gchar *cmd = g_strdup_printf("shidik-session-ctl %s", (const char *)data);
    spawn(cmd);
    g_free(cmd);
    if (GTK_IS_WINDOW(dialog))
        gtk_widget_destroy(dialog);
}

static gboolean power_dialog_key(GtkWidget *w, GdkEventKey *ev,
        gpointer data) {
    (void)data;
    if (ev->keyval == GDK_KEY_Escape) {
        gtk_widget_destroy(w);
        return TRUE;
    }
    return FALSE;
}

static void on_power_clicked(GtkButton *b, gpointer data) {
    (void)b; (void)data;

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "Завершение работы");
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(win), 20);
    g_signal_connect(win, "key-press-event",
        G_CALLBACK(power_dialog_key), NULL);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(GTK_CONTAINER(win), box);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<b>Завершение работы</b>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 4);

    /* подпись, иконка темы, аргумент shidik-session-ctl */
    const char *actions[][3] = {
        { "Заблокировать", "system-lock-screen-symbolic",   "lock"     },
        { "Спящий режим",  "media-playback-pause-symbolic", "suspend"  },
        { "Выйти",         "system-log-out-symbolic",       "logout"   },
        { "Перезагрузить", "view-refresh-symbolic",         "reboot"   },
        { "Выключить",     "system-shutdown-symbolic",      "poweroff" },
    };
    for (size_t i = 0; i < G_N_ELEMENTS(actions); i++) {
        GtkWidget *item = gtk_button_new_with_label(actions[i][0]);
        gtk_button_set_image(GTK_BUTTON(item),
            gtk_image_new_from_icon_name(actions[i][1],
                GTK_ICON_SIZE_BUTTON));
        gtk_button_set_always_show_image(GTK_BUTTON(item), TRUE);
        gtk_widget_set_size_request(item, 250, 42);
        g_signal_connect(item, "clicked",
            G_CALLBACK(on_power_action), (gpointer)actions[i][2]);
        gtk_box_pack_start(GTK_BOX(box), item, FALSE, FALSE, 0);
    }

    GtkWidget *cancel = gtk_button_new_with_label("Отмена");
    g_signal_connect_swapped(cancel, "clicked",
        G_CALLBACK(gtk_widget_destroy), win);
    gtk_box_pack_start(GTK_BOX(box), cancel, FALSE, FALSE, 6);

    gtk_widget_show_all(win);
}

static GtkWidget *make_power_button(void) {
    GtkWidget *button = gtk_button_new();
    /* Иконка из темы, а не символ ⏻ (U+23FB): его нет в DejaVu, и
     * вместо кнопки рисуется пустой квадрат. */
    gtk_button_set_image(GTK_BUTTON(button),
        gtk_image_new_from_icon_name("system-shutdown-symbolic",
            GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(button), TRUE);
    gtk_widget_set_tooltip_text(button, "Завершение работы");
    g_signal_connect(button, "clicked", G_CALLBACK(on_power_clicked), NULL);
    return button;
}

/* ---- TODO: таскбар ----
 * Список запущенных окон берётся из протокола
 * wlr-foreign-toplevel-management-unstable-v1:
 *   1. в shidikwm вызвать wlr_foreign_toplevel_manager_v1_create(display);
 *   2. здесь забиндить zwlr_foreign_toplevel_manager_v1 через
 *      wl_registry (gdk_wayland_display_get_wl_display(...)),
 *      слушать события toplevel.title/state/closed;
 *   3. на каждое окно — GtkButton c toplevel.activate по клику.
 */
static GtkWidget *make_taskbar_stub(void) {
    GtkWidget *label = gtk_label_new("ShidikDE");
    gtk_widget_set_hexpand(label, TRUE);
    return label;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "shidikpanel");

    /* закрепить как слой поверх рабочего стола, во всю ширину экрана */
    gtk_layer_init_for_window(GTK_WINDOW(window));
    gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_auto_exclusive_zone_enable(GTK_WINDOW(window));

    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(bar), 4);
    gtk_container_add(GTK_CONTAINER(window), bar);

    GtkWidget *menu_btn = gtk_button_new();
    gtk_button_set_image(GTK_BUTTON(menu_btn),
        gtk_image_new_from_icon_name("open-menu-symbolic",
            GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(menu_btn), TRUE);
    gtk_widget_set_tooltip_text(menu_btn, "Приложения (Win+D)");
    g_signal_connect(menu_btn, "clicked", G_CALLBACK(on_menu_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(bar), menu_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(bar), make_taskbar_stub(), TRUE, TRUE, 0);

    status_label = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(bar), status_label, FALSE, FALSE, 8);

    clock_label = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(bar), clock_label, FALSE, FALSE, 8);

    /* шторка уведомлений и быстрых настроек (она же Win+N) */
    GtkWidget *shade_btn = gtk_button_new();
    gtk_button_set_image(GTK_BUTTON(shade_btn),
        gtk_image_new_from_icon_name("preferences-system-symbolic",
            GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(shade_btn), TRUE);
    gtk_widget_set_tooltip_text(shade_btn,
        "Уведомления и быстрые настройки (Win+N)");
    g_signal_connect(shade_btn, "clicked",
        G_CALLBACK(on_shade_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(bar), shade_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(bar), make_power_button(), FALSE, FALSE, 0);

    update_status(NULL);
    g_timeout_add_seconds(1, update_status, NULL);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
