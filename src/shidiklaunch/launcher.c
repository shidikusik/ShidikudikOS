/* shidiklaunch — меню приложений ShidikDE.
 *
 * Сканирует .desktop-файлы (GKeyFile из GLib), показывает список с
 * поиском, по Enter/клику запускает приложение и закрывается.
 * Вызывается кнопкой "☰" панели или хоткеем Win+D композитора.
 *
 * Зависимости: libgtk-3-dev (GLib идёт в комплекте)
 */
#include <gtk/gtk.h>
#include <string.h>

struct app_entry {
    gchar *name;
    gchar *exec;
    gchar *comment;
};

static GPtrArray *apps;          /* массив struct app_entry* */
static GtkWidget *listbox;
static GtkWidget *search_entry;

static void app_entry_free(gpointer p) {
    struct app_entry *e = p;
    g_free(e->name);
    g_free(e->exec);
    g_free(e->comment);
    g_free(e);
}

/* Убирает из Exec= коды подстановки %f %F %u %U %i %c %k */
static gchar *strip_field_codes(const gchar *exec) {
    GString *out = g_string_new(NULL);
    for (const gchar *p = exec; *p; p++) {
        if (*p == '%' && p[1] != '\0') {
            if (p[1] == '%')
                g_string_append_c(out, '%');
            p++; /* пропустить код */
        } else {
            g_string_append_c(out, *p);
        }
    }
    return g_string_free(out, FALSE);
}

static void scan_desktop_dir(const gchar *dir_path) {
    GDir *dir = g_dir_open(dir_path, 0, NULL);
    if (!dir)
        return;

    const gchar *fname;
    while ((fname = g_dir_read_name(dir)) != NULL) {
        if (!g_str_has_suffix(fname, ".desktop"))
            continue;
        gchar *path = g_build_filename(dir_path, fname, NULL);
        GKeyFile *kf = g_key_file_new();

        if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
            const gchar *G = G_KEY_FILE_DESKTOP_GROUP;
            gboolean hidden =
                g_key_file_get_boolean(kf, G, "NoDisplay", NULL) ||
                g_key_file_get_boolean(kf, G, "Hidden", NULL);
            gchar *type = g_key_file_get_string(kf, G, "Type", NULL);
            gchar *name = g_key_file_get_locale_string(kf, G, "Name",
                NULL, NULL);
            gchar *exec = g_key_file_get_string(kf, G, "Exec", NULL);

            if (!hidden && exec && name &&
                    g_strcmp0(type, "Application") == 0) {
                struct app_entry *e = g_new0(struct app_entry, 1);
                e->name = g_strdup(name);
                e->exec = strip_field_codes(exec);
                e->comment = g_key_file_get_locale_string(kf, G,
                    "Comment", NULL, NULL);
                g_ptr_array_add(apps, e);
            }
            g_free(type);
            g_free(name);
            g_free(exec);
        }
        g_key_file_free(kf);
        g_free(path);
    }
    g_dir_close(dir);
}

static gint compare_apps(gconstpointer a, gconstpointer b) {
    const struct app_entry *ea = *(struct app_entry * const *)a;
    const struct app_entry *eb = *(struct app_entry * const *)b;
    return g_utf8_collate(ea->name, eb->name);
}

static void load_apps(void) {
    apps = g_ptr_array_new_with_free_func(app_entry_free);
    scan_desktop_dir("/usr/share/applications");
    scan_desktop_dir("/usr/local/share/applications");
    gchar *user_dir = g_build_filename(g_get_user_data_dir(),
        "applications", NULL);
    scan_desktop_dir(user_dir);
    g_free(user_dir);
    g_ptr_array_sort(apps, compare_apps);
}

static void launch_and_quit(struct app_entry *e) {
    gchar *argv[] = { "/bin/sh", "-c", e->exec, NULL };
    g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
        NULL, NULL, NULL, NULL);
    gtk_main_quit();
}

static void on_row_activated(GtkListBox *box, GtkListBoxRow *row,
        gpointer data) {
    (void)box; (void)data;
    struct app_entry *e = g_object_get_data(G_OBJECT(row), "app");
    if (e)
        launch_and_quit(e);
}

/* Фильтр списка по строке поиска (регистронезависимо). */
static gboolean filter_func(GtkListBoxRow *row, gpointer data) {
    (void)data;
    const gchar *needle = gtk_entry_get_text(GTK_ENTRY(search_entry));
    if (needle[0] == '\0')
        return TRUE;
    struct app_entry *e = g_object_get_data(G_OBJECT(row), "app");
    gchar *hay = g_utf8_casefold(e->name, -1);
    gchar *ndl = g_utf8_casefold(needle, -1);
    gboolean match = strstr(hay, ndl) != NULL;
    g_free(hay);
    g_free(ndl);
    return match;
}

static void on_search_changed(GtkSearchEntry *entry, gpointer data) {
    (void)entry; (void)data;
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(listbox));
}

/* Enter в поиске — запустить первый видимый результат. */
static void on_search_activate(GtkEntry *entry, gpointer data) {
    (void)entry; (void)data;
    GList *rows = gtk_container_get_children(GTK_CONTAINER(listbox));
    for (GList *l = rows; l; l = l->next) {
        GtkListBoxRow *row = GTK_LIST_BOX_ROW(l->data);
        if (filter_func(row, NULL)) {
            launch_and_quit(g_object_get_data(G_OBJECT(row), "app"));
            break;
        }
    }
    g_list_free(rows);
}

static gboolean on_key(GtkWidget *w, GdkEventKey *ev, gpointer data) {
    (void)w; (void)data;
    if (ev->keyval == GDK_KEY_Escape) {
        gtk_main_quit();
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    load_apps();

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Приложения");
    gtk_window_set_default_size(GTK_WINDOW(window), 420, 520);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    search_entry = gtk_search_entry_new();
    g_signal_connect(search_entry, "search-changed",
        G_CALLBACK(on_search_changed), NULL);
    g_signal_connect(search_entry, "activate",
        G_CALLBACK(on_search_activate), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), search_entry, FALSE, FALSE, 0);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

    listbox = gtk_list_box_new();
    gtk_list_box_set_filter_func(GTK_LIST_BOX(listbox), filter_func,
        NULL, NULL);
    g_signal_connect(listbox, "row-activated",
        G_CALLBACK(on_row_activated), NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), listbox);

    for (guint i = 0; i < apps->len; i++) {
        struct app_entry *e = g_ptr_array_index(apps, i);
        GtkWidget *row = gtk_list_box_row_new();
        g_object_set_data(G_OBJECT(row), "app", e);

        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_container_set_border_width(GTK_CONTAINER(box), 6);
        GtkWidget *name = gtk_label_new(e->name);
        gtk_widget_set_halign(name, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(box), name, FALSE, FALSE, 0);
        if (e->comment) {
            GtkWidget *comment = gtk_label_new(e->comment);
            gtk_widget_set_halign(comment, GTK_ALIGN_START);
            gtk_style_context_add_class(
                gtk_widget_get_style_context(comment), "dim-label");
            gtk_box_pack_start(GTK_BOX(box), comment, FALSE, FALSE, 0);
        }
        gtk_container_add(GTK_CONTAINER(row), box);
        gtk_container_add(GTK_CONTAINER(listbox), row);
    }

    gtk_widget_show_all(window);
    gtk_widget_grab_focus(search_entry);
    gtk_main();

    g_ptr_array_unref(apps);
    return 0;
}
