/* shidiklock — экран блокировки ShidikDE.
 *
 * Полноэкранная layer-shell поверхность на слое OVERLAY с эксклюзивным
 * захватом клавиатуры: пока процесс жив, до рабочего стола не добраться.
 * Пароль проверяется через PAM от имени текущего пользователя (pam_unix
 * умеет это через setuid-помощник unix_chkpwd, root не нужен).
 *
 * Ограничение: это не ext-session-lock-v1. Если процесс убить извне
 * (только root может), экран разблокируется. Для локального «отошёл от
 * ноутбука» этого достаточно, для параноидального сценария нужен
 * ext-session-lock — он в дорожной карте.
 *
 * Зависимости: libgtk-3-dev libgtk-layer-shell-dev libpam0g-dev
 */
#include <gtk/gtk.h>
#include <gtk-layer-shell/gtk-layer-shell.h>
#include <security/pam_appl.h>
#include <string.h>
#include <unistd.h>

static GtkWidget *pass_entry, *status_label, *clock_label, *user_label;

/* ---- PAM ---- */

static int conv_fn(int num_msg, const struct pam_message **msg,
        struct pam_response **resp, void *appdata) {
    const char *password = appdata;
    struct pam_response *replies = calloc(num_msg, sizeof(*replies));
    if (!replies)
        return PAM_BUF_ERR;
    for (int i = 0; i < num_msg; i++) {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF)
            replies[i].resp = strdup(password ? password : "");
        else if (msg[i]->msg_style == PAM_PROMPT_ECHO_ON)
            replies[i].resp = strdup("");
    }
    *resp = replies;
    return PAM_SUCCESS;
}

static gboolean check_password(const char *password) {
    struct pam_conv conv = { conv_fn, (void *)password };
    pam_handle_t *pamh = NULL;
    const char *user = g_get_user_name();

    if (pam_start("shidiklock", user, &conv, &pamh) != PAM_SUCCESS)
        return FALSE;
    int r = pam_authenticate(pamh, 0);
    if (r == PAM_SUCCESS)
        r = pam_acct_mgmt(pamh, 0);
    pam_end(pamh, r);
    return r == PAM_SUCCESS;
}

/* ---- UI ---- */

static gboolean update_clock(gpointer data) {
    (void)data;
    GDateTime *now = g_date_time_new_now_local();
    gchar *t = g_date_time_format(now, "%H:%M");
    gchar *markup = g_markup_printf_escaped(
        "<span size='58000' weight='200'>%s</span>", t);
    gtk_label_set_markup(GTK_LABEL(clock_label), markup);
    g_free(markup);
    g_free(t);
    g_date_time_unref(now);
    return G_SOURCE_CONTINUE;
}

static gboolean reenable(gpointer data) {
    gtk_widget_set_sensitive(GTK_WIDGET(data), TRUE);
    gtk_widget_grab_focus(pass_entry);
    return G_SOURCE_REMOVE;
}

static void try_unlock(GtkWidget *w, gpointer data) {
    (void)w; (void)data;
    const gchar *pass = gtk_entry_get_text(GTK_ENTRY(pass_entry));

    if (check_password(pass)) {
        gtk_main_quit(); /* процесс завершается — экран разблокирован */
        return;
    }

    gtk_label_set_text(GTK_LABEL(status_label), "Неверный пароль");
    gtk_entry_set_text(GTK_ENTRY(pass_entry), "");
    gtk_widget_set_sensitive(pass_entry, FALSE);
    g_timeout_add(1000, reenable, pass_entry); /* пауза от перебора */
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "window { background-color: #12131c; }"
        ".card { background-color: rgba(26,27,38,0.92);"
        "  border-radius: 22px; padding: 32px 40px;"
        "  border: 1px solid rgba(192,202,245,0.14); }"
        ".clock { color: #c0caf5; }"
        ".user { color: #f0a860; font-size: 16px; font-weight: bold; }"
        ".hint { color: #565f89; font-size: 12px; }"
        ".err { color: #f7768e; font-size: 13px; }"
        "entry { background: rgba(36,40,59,0.95); color: #c0caf5;"
        "  border: 1px solid rgba(192,202,245,0.2); border-radius: 10px;"
        "  padding: 10px 14px; caret-color: #f0a860; }"
        "entry:focus { border-color: #f0a860; }", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_layer_init_for_window(GTK_WINDOW(window));
    gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_OVERLAY);
    for (int e = 0; e < GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER; e++)
        gtk_layer_set_anchor(GTK_WINDOW(window), e, TRUE);
    /* эксклюзивная клавиатура: ввод не уходит приложениям под замком */
    gtk_layer_set_keyboard_mode(GTK_WINDOW(window),
        GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(window), -1);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "card");
    gtk_widget_set_halign(card, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(card, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(card, 340, -1);

    clock_label = gtk_label_new(NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(clock_label),
        "clock");
    gtk_box_pack_start(GTK_BOX(card), clock_label, FALSE, FALSE, 0);

    user_label = gtk_label_new(g_get_user_name());
    gtk_style_context_add_class(gtk_widget_get_style_context(user_label),
        "user");
    gtk_box_pack_start(GTK_BOX(card), user_label, FALSE, FALSE, 0);

    pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(pass_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(pass_entry), "пароль");
    gtk_entry_set_input_purpose(GTK_ENTRY(pass_entry),
        GTK_INPUT_PURPOSE_PASSWORD);
    g_signal_connect(pass_entry, "activate", G_CALLBACK(try_unlock), NULL);
    gtk_box_pack_start(GTK_BOX(card), pass_entry, FALSE, FALSE, 6);

    status_label = gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(status_label),
        "err");
    gtk_box_pack_start(GTK_BOX(card), status_label, FALSE, FALSE, 0);

    GtkWidget *hint = gtk_label_new("Введите пароль и нажмите Enter");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "hint");
    gtk_box_pack_start(GTK_BOX(card), hint, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(window), card);

    update_clock(NULL);
    g_timeout_add_seconds(20, update_clock, NULL);

    gtk_widget_show_all(window);
    gtk_widget_grab_focus(pass_entry);
    gtk_main();
    return 0;
}
