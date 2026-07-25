/* shidik-store — магазин приложений ShidikudikOS.
 *
 * Витрина над apt: подобранный каталог приложений (GKeyFile из
 * /usr/share/shidikudik/store-catalog.ini) с фильтром по категориям и
 * поиском; статус «установлено» берётся у dpkg-query; установка и
 * удаление — `pkexec apt-get -y install|remove <пакет>` с живым логом.
 *
 * Через поиск доступен и весь остальной репозиторий Debian: если строка
 * не нашлась в каталоге, предлагается поиск по apt-cache.
 *
 * Зависимости: libgtk-3-dev
 */
#include <gtk/gtk.h>
#include <string.h>

#define CATALOG "/usr/share/shidikudik/store-catalog.ini"

struct app_item {
    gchar *pkg;
    gchar *name;
    gchar *comment;
    gchar *category;
    gboolean installed;
    GtkWidget *row;
    GtkWidget *action_button;
    GtkWidget *status_label;
};

static GPtrArray *items;
static GtkWidget *window, *listbox, *search_entry, *cat_combo, *count_label;
static GPid child_pid;

/* ---------- статус пакетов ---------- */

static gboolean pkg_installed(const gchar *pkg) {
    gchar *cmd = g_strdup_printf(
        "dpkg-query -W -f=${Status} %s", pkg);
    gchar *out = NULL;
    gint status = 0;
    gboolean ok = g_spawn_command_line_sync(cmd, &out, NULL, &status, NULL);
    gboolean installed = ok && out &&
        strstr(out, "install ok installed") != NULL;
    g_free(out);
    g_free(cmd);
    return installed;
}

static void refresh_item(struct app_item *it) {
    it->installed = pkg_installed(it->pkg);
    gtk_button_set_label(GTK_BUTTON(it->action_button),
        it->installed ? "Удалить" : "Установить");
    gtk_label_set_text(GTK_LABEL(it->status_label),
        it->installed ? "установлено" : "");
}

/* ---------- каталог ---------- */

static void load_catalog(void) {
    items = g_ptr_array_new();
    GKeyFile *kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, CATALOG, G_KEY_FILE_NONE, NULL)) {
        g_key_file_free(kf);
        return;
    }

    gsize n = 0;
    gchar **groups = g_key_file_get_groups(kf, &n);
    for (gsize i = 0; i < n; i++) {
        struct app_item *it = g_new0(struct app_item, 1);
        it->pkg = g_strdup(groups[i]);
        it->name = g_key_file_get_locale_string(kf, groups[i],
            "Name", NULL, NULL);
        it->comment = g_key_file_get_locale_string(kf, groups[i],
            "Comment", NULL, NULL);
        it->category = g_key_file_get_string(kf, groups[i],
            "Category", NULL);
        if (!it->name)
            it->name = g_strdup(groups[i]);
        if (!it->category)
            it->category = g_strdup("Разное");
        g_ptr_array_add(items, it);
    }
    g_strfreev(groups);
    g_key_file_free(kf);
}

/* ---------- установка / удаление ---------- */

static void run_apt(struct app_item *it, gboolean install) {
    if (child_pid != 0)
        return; /* одна операция за раз — apt всё равно блокируется */

    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        install ? "Установка" : "Удаление", GTK_WINDOW(window),
        GTK_DIALOG_MODAL, "Закрыть", GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 560, 380);
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(area), 10);

    gchar *head = g_strdup_printf("%s: %s",
        install ? "Устанавливаю" : "Удаляю", it->name);
    GtkWidget *title = gtk_label_new(head);
    g_free(head);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(area), title, FALSE, FALSE, 0);

    GtkWidget *spinner = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(spinner));
    gtk_box_pack_start(GTK_BOX(area), spinner, FALSE, FALSE, 0);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_container_add(GTK_CONTAINER(scrolled), view);
    gtk_box_pack_start(GTK_BOX(area), scrolled, TRUE, TRUE, 6);
    gtk_widget_show_all(dlg);

    /* apt в неинтерактивном режиме, вывод сливаем в одно окно */
    gchar *cmd = g_strdup_printf(
        "pkexec env DEBIAN_FRONTEND=noninteractive "
        "apt-get -y %s %s", install ? "install" : "remove", it->pkg);
    gchar **argv = NULL;
    g_shell_parse_argv(cmd, NULL, &argv, NULL);
    g_free(cmd);

    gint out_fd;
    GError *err = NULL;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    if (!g_spawn_async_with_pipes(NULL, argv, NULL,
            G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD |
            G_SPAWN_STDERR_TO_DEV_NULL,
            NULL, NULL, &child_pid, NULL, &out_fd, NULL, &err)) {
        gtk_text_buffer_set_text(buf, err->message, -1);
        g_error_free(err);
        g_strfreev(argv);
        gtk_spinner_stop(GTK_SPINNER(spinner));
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        child_pid = 0;
        return;
    }
    g_strfreev(argv);

    /* Читаем вывод синхронно, прокручивая главный цикл GTK: диалог
     * остаётся отзывчивым, а логика остаётся линейной. */
    GIOChannel *ch = g_io_channel_unix_new(out_fd);
    g_io_channel_set_encoding(ch, NULL, NULL);
    gchar *line = NULL;
    gsize len;
    while (g_io_channel_read_line(ch, &line, &len, NULL, NULL) ==
            G_IO_STATUS_NORMAL && line) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buf, &end);
        gtk_text_buffer_insert(buf, &end, line, -1);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(view), &end,
            0.0, FALSE, 0, 0);
        g_free(line);
        line = NULL;
        while (gtk_events_pending())
            gtk_main_iteration();
    }
    g_io_channel_shutdown(ch, FALSE, NULL);
    g_io_channel_unref(ch);

    gtk_spinner_stop(GTK_SPINNER(spinner));
    gtk_widget_hide(spinner);
    g_spawn_close_pid(child_pid);
    child_pid = 0;

    refresh_item(it);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_insert(buf, &end,
        it->installed ? "\nГотово: пакет установлен.\n"
                      : "\nГотово: пакет удалён.\n", -1);

    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static void on_action(GtkButton *b, gpointer data) {
    (void)b;
    struct app_item *it = data;
    run_apt(it, !it->installed);
}

/* ---------- фильтрация ---------- */

static gboolean filter_func(GtkListBoxRow *row, gpointer data) {
    (void)data;
    struct app_item *it = g_object_get_data(G_OBJECT(row), "item");
    if (!it)
        return FALSE;

    gchar *cat = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(cat_combo));
    gboolean cat_ok = !cat || g_strcmp0(cat, "Все") == 0 ||
        g_strcmp0(cat, it->category) == 0;
    g_free(cat);
    if (!cat_ok)
        return FALSE;

    const gchar *needle = gtk_entry_get_text(GTK_ENTRY(search_entry));
    if (!needle || !*needle)
        return TRUE;

    gchar *ndl = g_utf8_casefold(needle, -1);
    gchar *hay_name = g_utf8_casefold(it->name, -1);
    gchar *hay_pkg = g_utf8_casefold(it->pkg, -1);
    gchar *hay_comment = it->comment
        ? g_utf8_casefold(it->comment, -1) : g_strdup("");
    gboolean match = strstr(hay_name, ndl) || strstr(hay_pkg, ndl) ||
        strstr(hay_comment, ndl);
    g_free(ndl);
    g_free(hay_name);
    g_free(hay_pkg);
    g_free(hay_comment);
    return match;
}

static void update_count(void) {
    GList *rows = gtk_container_get_children(GTK_CONTAINER(listbox));
    int shown = 0;
    for (GList *l = rows; l; l = l->next)
        if (filter_func(GTK_LIST_BOX_ROW(l->data), NULL))
            shown++;
    g_list_free(rows);
    gchar *t = g_strdup_printf("%d приложений", shown);
    gtk_label_set_text(GTK_LABEL(count_label), t);
    g_free(t);
}

static void on_filter_changed(GtkWidget *w, gpointer data) {
    (void)w; (void)data;
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(listbox));
    update_count();
}

/* Поиск по всему репозиторию, когда каталога мало. */
static void on_search_apt(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    const gchar *needle = gtk_entry_get_text(GTK_ENTRY(search_entry));
    if (!needle || !*needle)
        return;
    gchar *cmd = g_strdup_printf(
        "shidik-term -e sh -c 'apt-cache search %s | less'", needle);
    g_spawn_command_line_async(cmd, NULL);
    g_free(cmd);
}

/* ---------- main ---------- */

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    load_catalog();

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window),
        "Магазин приложений — Shidik-Store");
    gtk_window_set_default_size(GTK_WINDOW(window), 700, 560);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* строка поиска и фильтр категорий */
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    search_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry),
        "Поиск приложений…");
    g_signal_connect(search_entry, "search-changed",
        G_CALLBACK(on_filter_changed), NULL);
    gtk_box_pack_start(GTK_BOX(bar), search_entry, TRUE, TRUE, 0);

    cat_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cat_combo), "Все");
    GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
    for (guint i = 0; i < items->len; i++) {
        struct app_item *it = g_ptr_array_index(items, i);
        if (!g_hash_table_contains(seen, it->category)) {
            g_hash_table_add(seen, it->category);
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cat_combo),
                it->category);
        }
    }
    g_hash_table_destroy(seen);
    gtk_combo_box_set_active(GTK_COMBO_BOX(cat_combo), 0);
    g_signal_connect(cat_combo, "changed",
        G_CALLBACK(on_filter_changed), NULL);
    gtk_box_pack_start(GTK_BOX(bar), cat_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), bar, FALSE, FALSE, 0);

    /* список приложений */
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    listbox = gtk_list_box_new();
    gtk_list_box_set_filter_func(GTK_LIST_BOX(listbox), filter_func,
        NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), listbox);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

    for (guint i = 0; i < items->len; i++) {
        struct app_item *it = g_ptr_array_index(items, i);

        GtkWidget *row = gtk_list_box_row_new();
        g_object_set_data(G_OBJECT(row), "item", it);
        it->row = row;

        GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_container_set_border_width(GTK_CONTAINER(hb), 8);

        GtkWidget *texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget *name = gtk_label_new(NULL);
        gchar *markup = g_markup_printf_escaped("<b>%s</b>", it->name);
        gtk_label_set_markup(GTK_LABEL(name), markup);
        g_free(markup);
        gtk_widget_set_halign(name, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(texts), name, FALSE, FALSE, 0);

        if (it->comment) {
            GtkWidget *cm = gtk_label_new(it->comment);
            gtk_widget_set_halign(cm, GTK_ALIGN_START);
            gtk_label_set_line_wrap(GTK_LABEL(cm), TRUE);
            gtk_style_context_add_class(gtk_widget_get_style_context(cm),
                "dim-label");
            gtk_box_pack_start(GTK_BOX(texts), cm, FALSE, FALSE, 0);
        }
        gtk_box_pack_start(GTK_BOX(hb), texts, TRUE, TRUE, 0);

        it->status_label = gtk_label_new("");
        gtk_style_context_add_class(
            gtk_widget_get_style_context(it->status_label), "dim-label");
        gtk_box_pack_start(GTK_BOX(hb), it->status_label, FALSE, FALSE, 0);

        it->action_button = gtk_button_new_with_label("Установить");
        g_signal_connect(it->action_button, "clicked",
            G_CALLBACK(on_action), it);
        gtk_widget_set_valign(it->action_button, GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(hb), it->action_button, FALSE, FALSE, 0);

        gtk_container_add(GTK_CONTAINER(row), hb);
        gtk_container_add(GTK_CONTAINER(listbox), row);
        refresh_item(it);
    }

    /* нижняя строка */
    GtkWidget *foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    count_label = gtk_label_new("");
    gtk_widget_set_halign(count_label, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(count_label),
        "dim-label");
    gtk_box_pack_start(GTK_BOX(foot), count_label, TRUE, TRUE, 0);

    GtkWidget *apt_search = gtk_button_new_with_label(
        "Искать во всём репозитории…");
    g_signal_connect(apt_search, "clicked", G_CALLBACK(on_search_apt), NULL);
    gtk_box_pack_start(GTK_BOX(foot), apt_search, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), foot, FALSE, FALSE, 0);

    update_count();
    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
