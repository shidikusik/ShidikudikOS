/* shidikgreet — графический greeter ShidikDM (glassmorphism).
 *
 * Чистый UI без привилегий: рисует размытые обои и карточку логина,
 * а проверку пароля делает shidikdm (root) на другом конце сокета.
 *
 * Протокол (unix-сокет, fd передаётся через окружение SHIDIKDM_FD):
 *   greeter -> dm : "<user>\0<password>\0"
 *   dm -> greeter : 1 байт: 'O' (ок) или 'F' (отказ)
 * После 'O' greeter завершается; shidikdm гасит greeter-композитор и
 * запускает настоящую сессию пользователя.
 *
 * Запускается композитором shidikwm, который shidikdm поднимает через
 * seatd-launch (см. src/shidikdm/main.c).
 *
 * Зависимости: libgtk-3-dev
 */
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WALLPAPER "/usr/share/shidikusik/wallpaper-blur.png"

static int dm_fd = -1;
static GtkWidget *user_entry, *pass_entry, *error_label, *login_button,
                 *clock_label;

static const char *CSS =
    "window { background-color: #1a1b26; }"
    "window.with-wallpaper {"
    "  background-image: url('" WALLPAPER "');"
    "  background-size: cover; background-position: center; }"
    ".card { background-color: rgba(26,27,38,0.78);"
    "  border-radius: 24px; padding: 36px 44px;"
    "  border: 1px solid rgba(192,202,245,0.15); }"
    ".title { color: #f0a860; font-size: 26px; font-weight: bold; }"
    ".subtitle { color: #565f89; font-size: 12px; }"
    ".clock { color: #c0caf5; font-size: 42px; font-weight: 300; }"
    ".error { color: #f7768e; font-size: 13px; }"
    "entry { background: rgba(36,40,59,0.9); color: #c0caf5;"
    "  border: 1px solid rgba(192,202,245,0.2); border-radius: 10px;"
    "  padding: 10px 14px; caret-color: #f0a860; }"
    "entry:focus { border-color: #f0a860; }"
    "button.login { background: #f0a860; color: #1a1b26;"
    "  border-radius: 10px; padding: 10px; font-weight: bold; border: none; }"
    "button.login:hover { background: #f6c48d; }";

static gboolean update_clock(gpointer data) {
    (void)data;
    GDateTime *now = g_date_time_new_now_local();
    gchar *t = g_date_time_format(now, "%H:%M");
    gtk_label_set_text(GTK_LABEL(clock_label), t);
    g_free(t);
    g_date_time_unref(now);
    return G_SOURCE_CONTINUE;
}

static gboolean reenable_login(gpointer data) {
    (void)data;
    gtk_widget_set_sensitive(login_button, TRUE);
    gtk_widget_grab_focus(pass_entry);
    return G_SOURCE_REMOVE;
}

static void try_login(GtkWidget *w, gpointer data) {
    (void)w; (void)data;
    const gchar *user = gtk_entry_get_text(GTK_ENTRY(user_entry));
    const gchar *pass = gtk_entry_get_text(GTK_ENTRY(pass_entry));
    if (user[0] == '\0')
        return;
    if (dm_fd < 0) {
        gtk_label_set_text(GTK_LABEL(error_label),
            "нет связи с shidikdm (SHIDIKDM_FD)");
        return;
    }

    /* user\0pass\0 -> dm; ответ — один байт */
    if (write(dm_fd, user, strlen(user) + 1) < 0 ||
        write(dm_fd, pass, strlen(pass) + 1) < 0) {
        gtk_label_set_text(GTK_LABEL(error_label), "shidikdm не отвечает");
        return;
    }
    char resp = 'F';
    if (read(dm_fd, &resp, 1) <= 0) {
        gtk_label_set_text(GTK_LABEL(error_label), "shidikdm не отвечает");
        return;
    }

    if (resp == 'O') {
        gtk_main_quit(); /* dm перехватит управление и запустит сессию */
    } else {
        gtk_label_set_text(GTK_LABEL(error_label),
            "неверный логин или пароль");
        gtk_entry_set_text(GTK_ENTRY(pass_entry), "");
        gtk_widget_set_sensitive(login_button, FALSE);
        g_timeout_add(1000, reenable_login, NULL); /* анти-brute-force */
    }
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    const char *fd_env = g_getenv("SHIDIKDM_FD");
    if (fd_env)
        dm_fd = atoi(fd_env);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "shidikgreet");
    if (g_file_test(WALLPAPER, G_FILE_TEST_EXISTS))
        gtk_style_context_add_class(gtk_widget_get_style_context(window),
            "with-wallpaper");
    gtk_window_fullscreen(GTK_WINDOW(window));
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    /* карточка по центру экрана */
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "card");
    gtk_widget_set_halign(card, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(card, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(card, 360, -1);

    clock_label = gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(clock_label),
        "clock");
    gtk_box_pack_start(GTK_BOX(card), clock_label, FALSE, FALSE, 0);

    GtkWidget *title = gtk_label_new("ShidikusikOS");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "title");
    gtk_box_pack_start(GTK_BOX(card), title, FALSE, FALSE, 0);

    GtkWidget *subtitle = gtk_label_new("маленький · быстрый · свой");
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitle),
        "subtitle");
    gtk_box_pack_start(GTK_BOX(card), subtitle, FALSE, FALSE, 4);

    user_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(user_entry), "логин");
    gtk_box_pack_start(GTK_BOX(card), user_entry, FALSE, FALSE, 0);

    pass_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(pass_entry), "пароль");
    gtk_entry_set_visibility(GTK_ENTRY(pass_entry), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(pass_entry),
        GTK_INPUT_PURPOSE_PASSWORD);
    g_signal_connect(pass_entry, "activate", G_CALLBACK(try_login), NULL);
    gtk_box_pack_start(GTK_BOX(card), pass_entry, FALSE, FALSE, 0);

    error_label = gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(error_label),
        "error");
    gtk_box_pack_start(GTK_BOX(card), error_label, FALSE, FALSE, 0);

    login_button = gtk_button_new_with_label("Войти");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(login_button), "login");
    g_signal_connect(login_button, "clicked", G_CALLBACK(try_login), NULL);
    gtk_box_pack_start(GTK_BOX(card), login_button, FALSE, FALSE, 4);

    gtk_container_add(GTK_CONTAINER(window), card);

    update_clock(NULL);
    g_timeout_add_seconds(30, update_clock, NULL);

    gtk_widget_show_all(window);
    gtk_widget_grab_focus(user_entry);
    gtk_main();
    return 0;
}
