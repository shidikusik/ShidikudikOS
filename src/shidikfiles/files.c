/* shidik-files — файловый менеджер ShidikDE (v1).
 *
 * GTK3 + GIO: список файлов (иконка, имя, размер, дата), боковая панель
 * стандартных мест, адресная строка, история «назад», открытие файлов
 * приложением по умолчанию (GAppInfo), создание папки, переименование,
 * удаление в корзину (g_file_trash), показ скрытых файлов.
 *
 * Зависимости: libgtk-3-dev
 */
#include <gtk/gtk.h>
#include <string.h>

enum { COL_GICON, COL_NAME, COL_IS_DIR, COL_SIZE, COL_MTIME, COL_PATH,
       N_COLS };

static GtkWidget *window, *treeview, *path_entry;
static GtkListStore *store;
static gchar *current_dir;
static GList *history;          /* стек путей для кнопки «назад» */
static gboolean show_hidden;

/* ---------- загрузка каталога ---------- */

static gint sort_entries(gconstpointer a, gconstpointer b) {
    GFileInfo *ia = G_FILE_INFO((gpointer)a);
    GFileInfo *ib = G_FILE_INFO((gpointer)b);
    gboolean da = g_file_info_get_file_type(ia) == G_FILE_TYPE_DIRECTORY;
    gboolean db = g_file_info_get_file_type(ib) == G_FILE_TYPE_DIRECTORY;
    if (da != db)
        return da ? -1 : 1; /* каталоги сверху */
    return g_utf8_collate(g_file_info_get_display_name(ia),
                          g_file_info_get_display_name(ib));
}

static void load_dir(const gchar *path) {
    GFile *dir = g_file_new_for_path(path);
    GFileEnumerator *en = g_file_enumerate_children(dir,
        "standard::name,standard::display-name,standard::type,"
        "standard::size,standard::is-hidden,standard::icon,time::modified",
        G_FILE_QUERY_INFO_NONE, NULL, NULL);
    if (!en) {
        g_object_unref(dir);
        return; /* нет прав или каталога — остаёмся где были */
    }

    if (g_strcmp0(current_dir, path) != 0) {
        if (current_dir)
            history = g_list_prepend(history, current_dir);
        current_dir = g_strdup(path);
    }
    gtk_entry_set_text(GTK_ENTRY(path_entry), path);
    gtk_list_store_clear(store);

    GList *entries = NULL;
    GFileInfo *info;
    while ((info = g_file_enumerator_next_file(en, NULL, NULL)) != NULL) {
        if (!show_hidden && g_file_info_get_is_hidden(info)) {
            g_object_unref(info);
            continue;
        }
        entries = g_list_insert_sorted(entries, info, sort_entries);
    }
    g_object_unref(en);

    for (GList *l = entries; l; l = l->next) {
        info = l->data;
        gboolean is_dir =
            g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
        gchar *size_str = is_dir ? g_strdup("—")
            : g_format_size(g_file_info_get_size(info));

        GDateTime *mt = g_file_info_get_modification_date_time(info);
        gchar *mtime_str = mt
            ? g_date_time_format(mt, "%d.%m.%Y %H:%M") : g_strdup("");
        if (mt)
            g_date_time_unref(mt);

        gchar *full = g_build_filename(path,
            g_file_info_get_name(info), NULL);

        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
            COL_GICON, g_file_info_get_icon(info),
            COL_NAME, g_file_info_get_display_name(info),
            COL_IS_DIR, is_dir,
            COL_SIZE, size_str,
            COL_MTIME, mtime_str,
            COL_PATH, full,
            -1);
        g_free(size_str);
        g_free(mtime_str);
        g_free(full);
        g_object_unref(info);
    }
    g_list_free(entries);
    g_object_unref(dir);
}

/* ---------- навигация ---------- */

static void on_row_activated(GtkTreeView *tv, GtkTreePath *tp,
        GtkTreeViewColumn *col, gpointer data) {
    (void)col; (void)data;
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, tp))
        return;
    gboolean is_dir;
    gchar *path;
    gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
        COL_IS_DIR, &is_dir, COL_PATH, &path, -1);
    (void)tv;

    if (is_dir) {
        load_dir(path);
    } else {
        GFile *f = g_file_new_for_path(path);
        gchar *uri = g_file_get_uri(f);
        g_app_info_launch_default_for_uri(uri, NULL, NULL);
        g_free(uri);
        g_object_unref(f);
    }
    g_free(path);
}

static void on_back(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    if (!history)
        return;
    gchar *prev = history->data;
    history = g_list_delete_link(history, history);
    g_free(current_dir);
    current_dir = NULL; /* не класть prev обратно в историю */
    load_dir(prev);
    g_free(prev);
}

static void on_up(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    gchar *parent = g_path_get_dirname(current_dir);
    load_dir(parent);
    g_free(parent);
}

static void on_home(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    load_dir(g_get_home_dir());
}

static void on_path_activate(GtkEntry *e, gpointer data) {
    (void)data;
    load_dir(gtk_entry_get_text(e));
}

static void on_hidden_toggled(GtkToggleButton *b, gpointer data) {
    (void)data;
    show_hidden = gtk_toggle_button_get_active(b);
    gchar *keep = g_strdup(current_dir);
    load_dir(keep);
    g_free(keep);
}

/* ---------- операции ---------- */

static gchar *selected_path(void) {
    GtkTreeSelection *sel =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    GtkTreeIter iter;
    GtkTreeModel *model;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter))
        return NULL;
    gchar *path;
    gtk_tree_model_get(model, &iter, COL_PATH, &path, -1);
    return path;
}

/* Диалог с одним текстовым полем; возвращает строку или NULL. */
static gchar *ask_string(const gchar *title, const gchar *initial) {
    GtkWidget *dlg = gtk_dialog_new_with_buttons(title, GTK_WINDOW(window),
        GTK_DIALOG_MODAL, "Отмена", GTK_RESPONSE_CANCEL,
        "ОК", GTK_RESPONSE_OK, NULL);
    GtkWidget *entry = gtk_entry_new();
    if (initial)
        gtk_entry_set_text(GTK_ENTRY(entry), initial);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(area), 10);
    gtk_container_add(GTK_CONTAINER(area), entry);
    gtk_widget_show_all(dlg);

    gchar *result = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        const gchar *t = gtk_entry_get_text(GTK_ENTRY(entry));
        if (t && *t)
            result = g_strdup(t);
    }
    gtk_widget_destroy(dlg);
    return result;
}

static void on_new_folder(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    gchar *name = ask_string("Новая папка", "Новая папка");
    if (!name)
        return;
    gchar *full = g_build_filename(current_dir, name, NULL);
    GFile *f = g_file_new_for_path(full);
    g_file_make_directory(f, NULL, NULL);
    g_object_unref(f);
    g_free(full);
    g_free(name);
    gchar *keep = g_strdup(current_dir);
    load_dir(keep);
    g_free(keep);
}

static void on_rename(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    gchar *path = selected_path();
    if (!path)
        return;
    gchar *base = g_path_get_basename(path);
    gchar *name = ask_string("Переименовать", base);
    if (name) {
        GFile *f = g_file_new_for_path(path);
        GFile *renamed = g_file_set_display_name(f, name, NULL, NULL);
        if (renamed)
            g_object_unref(renamed);
        g_object_unref(f);
        g_free(name);
        gchar *keep = g_strdup(current_dir);
        load_dir(keep);
        g_free(keep);
    }
    g_free(base);
    g_free(path);
}

static void on_delete(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    gchar *path = selected_path();
    if (!path)
        return;
    gchar *base = g_path_get_basename(path);
    GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(window),
        GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
        "Переместить «%s» в корзину?", base);
    gboolean yes = gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_YES;
    gtk_widget_destroy(dlg);
    if (yes) {
        GFile *f = g_file_new_for_path(path);
        g_file_trash(f, NULL, NULL);
        g_object_unref(f);
        gchar *keep = g_strdup(current_dir);
        load_dir(keep);
        g_free(keep);
    }
    g_free(base);
    g_free(path);
}

/* ---------- боковая панель мест ---------- */

static void on_place_selected(GtkListBox *lb, GtkListBoxRow *row,
        gpointer data) {
    (void)lb; (void)data;
    if (!row)
        return;
    const gchar *path = g_object_get_data(G_OBJECT(row), "path");
    if (path)
        load_dir(path);
}

static void add_place(GtkWidget *listbox, const gchar *label,
        const gchar *path) {
    if (!path || !g_file_test(path, G_FILE_TEST_IS_DIR))
        return;
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *l = gtk_label_new(label);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_widget_set_margin_start(l, 12);
    gtk_widget_set_margin_end(l, 12);
    gtk_widget_set_margin_top(l, 6);
    gtk_widget_set_margin_bottom(l, 6);
    gtk_container_add(GTK_CONTAINER(row), l);
    g_object_set_data_full(G_OBJECT(row), "path", g_strdup(path), g_free);
    gtk_container_add(GTK_CONTAINER(listbox), row);
}

/* ---------- main ---------- */

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Файлы — Shidik-Files");
    gtk_window_set_default_size(GTK_WINDOW(window), 780, 500);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* панель инструментов */
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(bar), 6);

    GtkWidget *back = gtk_button_new_with_label("←");
    g_signal_connect(back, "clicked", G_CALLBACK(on_back), NULL);
    GtkWidget *up = gtk_button_new_with_label("↑");
    g_signal_connect(up, "clicked", G_CALLBACK(on_up), NULL);
    GtkWidget *home = gtk_button_new_with_label("⌂");
    g_signal_connect(home, "clicked", G_CALLBACK(on_home), NULL);
    gtk_box_pack_start(GTK_BOX(bar), back, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), up, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), home, FALSE, FALSE, 0);

    path_entry = gtk_entry_new();
    g_signal_connect(path_entry, "activate",
        G_CALLBACK(on_path_activate), NULL);
    gtk_box_pack_start(GTK_BOX(bar), path_entry, TRUE, TRUE, 4);

    /* Подписи словами: эмодзи вроде 🗑 в DejaVu отсутствуют и рисуются
     * пустым квадратом. */
    GtkWidget *newdir = gtk_button_new_with_label("Новая папка");
    g_signal_connect(newdir, "clicked", G_CALLBACK(on_new_folder), NULL);
    GtkWidget *rename = gtk_button_new_with_label("Переименовать");
    g_signal_connect(rename, "clicked", G_CALLBACK(on_rename), NULL);
    GtkWidget *del = gtk_button_new_with_label("Удалить");
    g_signal_connect(del, "clicked", G_CALLBACK(on_delete), NULL);
    GtkWidget *hidden = gtk_toggle_button_new_with_label("Скрытые");
    gtk_widget_set_tooltip_text(hidden, "Показывать скрытые файлы");
    g_signal_connect(hidden, "toggled",
        G_CALLBACK(on_hidden_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(bar), newdir, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), rename, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), del, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), hidden, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), bar, FALSE, FALSE, 0);

    /* места + список файлов */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

    GtkWidget *places = gtk_list_box_new();
    g_signal_connect(places, "row-activated",
        G_CALLBACK(on_place_selected), NULL);
    add_place(places, "Домашняя", g_get_home_dir());
    add_place(places, "Документы",
        g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS));
    add_place(places, "Загрузки",
        g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD));
    add_place(places, "Изображения",
        g_get_user_special_dir(G_USER_DIRECTORY_PICTURES));
    add_place(places, "Музыка",
        g_get_user_special_dir(G_USER_DIRECTORY_MUSIC));
    add_place(places, "Видео",
        g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS));
    add_place(places, "Система", "/");
    gtk_widget_set_size_request(places, 150, -1);
    gtk_box_pack_start(GTK_BOX(hbox), places, FALSE, FALSE, 0);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(hbox), scrolled, TRUE, TRUE, 0);

    store = gtk_list_store_new(N_COLS, G_TYPE_ICON, G_TYPE_STRING,
        G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    g_signal_connect(treeview, "row-activated",
        G_CALLBACK(on_row_activated), NULL);

    GtkTreeViewColumn *col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, "Имя");
    GtkCellRenderer *pix = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, pix, FALSE);
    gtk_tree_view_column_add_attribute(col, pix, "gicon", COL_GICON);
    GtkCellRenderer *txt = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, txt, TRUE);
    gtk_tree_view_column_add_attribute(col, txt, "text", COL_NAME);
    gtk_tree_view_column_set_expand(col, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);

    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview),
        gtk_tree_view_column_new_with_attributes("Размер",
            gtk_cell_renderer_text_new(), "text", COL_SIZE, NULL));
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview),
        gtk_tree_view_column_new_with_attributes("Изменён",
            gtk_cell_renderer_text_new(), "text", COL_MTIME, NULL));

    gtk_container_add(GTK_CONTAINER(scrolled), treeview);

    load_dir(g_get_home_dir());
    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
