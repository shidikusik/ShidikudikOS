/* shidik-install-gui — графический установщик ShidikudikOS.
 *
 * Мастер (GtkAssistant) из пяти страниц: приветствие → выбор диска →
 * пользователь и пароль → подтверждение → установка с прогрессом.
 *
 * Сам процесс установки не дублируется: GUI собирает ответы и запускает
 * `pkexec shidik-install --unattended ...`, передавая пароль первой
 * строкой stdin, а вывод (строки PROGRESS:N и текст) превращает в
 * прогресс-бар и лог.
 *
 * Зависимости: libgtk-3-dev
 */
#include <gtk/gtk.h>
#include <string.h>

static GtkWidget *assistant;
static GtkWidget *disk_combo, *disk_warning;
static GtkWidget *host_entry, *tz_combo, *user_entry, *pass_entry,
                 *pass2_entry, *user_hint;
static GtkWidget *summary_label, *progress_bar, *log_view, *reboot_button;
static GtkWidget *page_disk, *page_user, *page_confirm, *page_install;
static GPid child_pid;
static gboolean install_done;

/* ---------- страница «Диск» ---------- */

static void fill_disks(void) {
    gchar *out = NULL;
    if (!g_spawn_command_line_sync("lsblk -dnro NAME,SIZE,MODEL,TYPE",
            &out, NULL, NULL, NULL) || !out)
        return;

    gchar **lines = g_strsplit(out, "\n", -1);
    for (gchar **l = lines; *l; l++) {
        if (**l == '\0')
            continue;
        gchar **f = g_strsplit(*l, " ", 4);
        guint n = g_strv_length(f);
        /* формат: NAME SIZE [MODEL] TYPE — тип всегда последний */
        if (n >= 3 && g_strcmp0(f[n - 1], "disk") == 0) {
            gchar *label = g_strdup_printf("/dev/%s — %s%s%s", f[0], f[1],
                (n == 4) ? "  " : "", (n == 4) ? f[2] : "");
            gchar *id = g_strdup_printf("/dev/%s", f[0]);
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(disk_combo),
                id, label);
            g_free(label);
            g_free(id);
        }
        g_strfreev(f);
    }
    g_strfreev(lines);
    g_free(out);
    gtk_combo_box_set_active(GTK_COMBO_BOX(disk_combo), 0);
}

static void on_disk_changed(GtkComboBox *c, gpointer data) {
    (void)data;
    const gchar *id = gtk_combo_box_get_active_id(c);
    gtk_assistant_set_page_complete(GTK_ASSISTANT(assistant),
        page_disk, id != NULL);
    if (id) {
        gchar *msg = g_strdup_printf(
            "Все данные на %s будут удалены безвозвратно.", id);
        gtk_label_set_text(GTK_LABEL(disk_warning), msg);
        g_free(msg);
    }
}

/* ---------- страница «Пользователь» ---------- */

static gboolean valid_username(const gchar *u) {
    if (!u || !*u || !g_ascii_islower(u[0]))
        return FALSE;
    for (const gchar *p = u; *p; p++)
        if (!g_ascii_islower(*p) && !g_ascii_isdigit(*p) &&
            *p != '-' && *p != '_')
            return FALSE;
    return TRUE;
}

static void validate_user_page(GtkWidget *w, gpointer data) {
    (void)w; (void)data;
    const gchar *user = gtk_entry_get_text(GTK_ENTRY(user_entry));
    const gchar *p1 = gtk_entry_get_text(GTK_ENTRY(pass_entry));
    const gchar *p2 = gtk_entry_get_text(GTK_ENTRY(pass2_entry));
    const gchar *host = gtk_entry_get_text(GTK_ENTRY(host_entry));

    const gchar *problem = NULL;
    if (!valid_username(user))
        problem = "Имя пользователя: строчная латиница, цифры, «-», «_»;"
                  " начинается с буквы.";
    else if (!*host)
        problem = "Укажите имя компьютера.";
    else if (strlen(p1) < 4)
        problem = "Пароль слишком короткий (минимум 4 символа).";
    else if (g_strcmp0(p1, p2) != 0)
        problem = "Пароли не совпадают.";

    gtk_label_set_text(GTK_LABEL(user_hint), problem ? problem : "");
    gtk_assistant_set_page_complete(GTK_ASSISTANT(assistant),
        page_user, problem == NULL);
}

/* ---------- установка ---------- */

static void log_append(const gchar *text) {
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_insert(buf, &end, text, -1);
    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(log_view), &end,
        0.0, FALSE, 0, 0);
}

static gboolean on_child_output(GIOChannel *ch, GIOCondition cond,
        gpointer data) {
    (void)data;
    if (cond & (G_IO_HUP | G_IO_ERR))
        return FALSE;

    gchar *line = NULL;
    gsize len;
    if (g_io_channel_read_line(ch, &line, &len, NULL, NULL) !=
            G_IO_STATUS_NORMAL || !line)
        return FALSE;

    if (g_str_has_prefix(line, "PROGRESS:")) {
        int pct = atoi(line + 9);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar),
            CLAMP(pct / 100.0, 0.0, 1.0));
    } else {
        log_append(line);
        if (g_str_has_prefix(line, "== ")) {
            gchar *t = g_strchomp(g_strdup(line + 3));
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_bar), t);
            g_free(t);
        }
    }
    g_free(line);
    return TRUE;
}

static void on_child_exit(GPid pid, gint status, gpointer data) {
    (void)data;
    g_spawn_close_pid(pid);
    child_pid = 0;
    install_done = TRUE;

    gboolean ok = g_spawn_check_wait_status(status, NULL);
    if (ok) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 1.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_bar),
            "Готово — можно перезагружаться");
        log_append("\nУстановка успешно завершена.\n"
                   "После перезагрузки войдите под своим логином.\n");
        gtk_widget_show(reboot_button);
    } else {
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_bar),
            "Установка прервана");
        log_append("\nУстановка НЕ завершена — подробности в логе выше.\n");
    }
    /* страница завершена в любом случае: даёт закрыть мастер */
    gtk_assistant_set_page_complete(GTK_ASSISTANT(assistant),
        page_install, TRUE);
}

static void start_install(void) {
    const gchar *disk =
        gtk_combo_box_get_active_id(GTK_COMBO_BOX(disk_combo));
    const gchar *host = gtk_entry_get_text(GTK_ENTRY(host_entry));
    const gchar *user = gtk_entry_get_text(GTK_ENTRY(user_entry));
    const gchar *pass = gtk_entry_get_text(GTK_ENTRY(pass_entry));
    gchar *tz = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(tz_combo));

    gchar *argv[] = {
        "pkexec", "/usr/local/bin/shidik-install", "--unattended",
        "--disk", (gchar *)disk, "--hostname", (gchar *)host,
        "--user", (gchar *)user, "--timezone", tz ? tz : "Europe/Moscow",
        "--password-stdin", NULL
    };

    gint in_fd, out_fd;
    GError *err = NULL;
    if (!g_spawn_async_with_pipes(NULL, argv, NULL,
            G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
            NULL, NULL, &child_pid, &in_fd, &out_fd, NULL, &err)) {
        gchar *msg = g_strdup_printf("Не удалось запустить установщик: %s\n",
            err->message);
        log_append(msg);
        g_free(msg);
        g_error_free(err);
        gtk_assistant_set_page_complete(GTK_ASSISTANT(assistant),
            page_install, TRUE);
        g_free(tz);
        return;
    }
    g_free(tz);

    /* пароль — первой строкой stdin, затем закрываем поток */
    gchar *pw_line = g_strdup_printf("%s\n", pass);
    if (write(in_fd, pw_line, strlen(pw_line)) < 0)
        log_append("Предупреждение: не удалось передать пароль\n");
    g_free(pw_line);
    close(in_fd);

    GIOChannel *ch = g_io_channel_unix_new(out_fd);
    g_io_channel_set_encoding(ch, NULL, NULL);
    g_io_channel_set_flags(ch, G_IO_FLAG_NONBLOCK, NULL);
    g_io_add_watch(ch, G_IO_IN | G_IO_HUP | G_IO_ERR,
        on_child_output, NULL);
    g_io_channel_unref(ch);

    g_child_watch_add(child_pid, on_child_exit, NULL);
}

static void on_reboot(GtkButton *b, gpointer data) {
    (void)b; (void)data;
    g_spawn_command_line_async("pkexec systemctl reboot", NULL);
}

/* ---------- переходы мастера ---------- */

static void on_prepare(GtkAssistant *a, GtkWidget *page, gpointer data) {
    (void)data;
    if (page == page_confirm) {
        gchar *tz = gtk_combo_box_text_get_active_text(
            GTK_COMBO_BOX_TEXT(tz_combo));
        gchar *text = g_strdup_printf(
            "Диск:\t\t%s  (будет полностью очищен)\n"
            "Компьютер:\t%s\n"
            "Часовой пояс:\t%s\n"
            "Пользователь:\t%s\n\n"
            "Нажмите «Установить», чтобы начать. Прервать установку "
            "после начала записи на диск будет нельзя.",
            gtk_combo_box_get_active_id(GTK_COMBO_BOX(disk_combo)),
            gtk_entry_get_text(GTK_ENTRY(host_entry)),
            tz ? tz : "Europe/Moscow",
            gtk_entry_get_text(GTK_ENTRY(user_entry)));
        gtk_label_set_text(GTK_LABEL(summary_label), text);
        g_free(text);
        g_free(tz);
    } else if (page == page_install && !install_done && child_pid == 0) {
        gtk_assistant_commit(a); /* назад дороги нет */
        start_install();
    }
}

static void on_cancel_or_close(GtkAssistant *a, gpointer data) {
    (void)a; (void)data;
    if (child_pid != 0) {
        GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(assistant),
            GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
            "Установка ещё идёт. Прервать её сейчас может оставить диск "
            "в нерабочем состоянии.\n\nВсё равно выйти?");
        gboolean quit = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_YES;
        gtk_widget_destroy(d);
        if (!quit)
            return;
    }
    gtk_main_quit();
}

/* ---------- страницы ---------- */

static GtkWidget *make_page(GtkAssistant *a, GtkAssistantPageType type,
        const gchar *title) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 18);
    gtk_assistant_append_page(a, box);
    gtk_assistant_set_page_type(a, box, type);
    gtk_assistant_set_page_title(a, box, title);
    return box;
}

static GtkWidget *labeled(GtkWidget *box, const gchar *text,
        GtkWidget *widget) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *l = gtk_label_new(text);
    gtk_widget_set_size_request(l, 150, -1);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(row), l, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), widget, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    return row;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    assistant = gtk_assistant_new();
    gtk_window_set_title(GTK_WINDOW(assistant), "Установка ShidikudikOS");
    gtk_window_set_default_size(GTK_WINDOW(assistant), 640, 480);
    g_signal_connect(assistant, "cancel",
        G_CALLBACK(on_cancel_or_close), NULL);
    g_signal_connect(assistant, "close",
        G_CALLBACK(on_cancel_or_close), NULL);
    g_signal_connect(assistant, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(assistant, "prepare", G_CALLBACK(on_prepare), NULL);

    /* 1. Приветствие */
    GtkWidget *intro = make_page(GTK_ASSISTANT(assistant),
        GTK_ASSISTANT_PAGE_INTRO, "Добро пожаловать");
    if (g_file_test("/usr/share/shidikudik/logo.svg", G_FILE_TEST_EXISTS)) {
        GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(
            "/usr/share/shidikudik/logo.svg", 128, 128, NULL);
        if (pb) {
            GtkWidget *img = gtk_image_new_from_pixbuf(pb);
            g_object_unref(pb);
            gtk_box_pack_start(GTK_BOX(intro), img, FALSE, FALSE, 0);
        }
    }
    GtkWidget *hello = gtk_label_new(
        "Этот мастер установит ShidikudikOS на жёсткий диск.\n\n"
        "Вы выберете диск, укажете имя компьютера и создадите\n"
        "своего пользователя с собственным паролем.\n\n"
        "Live-система продолжит работать до перезагрузки.");
    gtk_label_set_justify(GTK_LABEL(hello), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(intro), hello, TRUE, TRUE, 0);
    gtk_assistant_set_page_complete(GTK_ASSISTANT(assistant), intro, TRUE);

    /* 2. Диск */
    page_disk = make_page(GTK_ASSISTANT(assistant),
        GTK_ASSISTANT_PAGE_CONTENT, "Диск для установки");
    disk_combo = gtk_combo_box_text_new();
    g_signal_connect(disk_combo, "changed",
        G_CALLBACK(on_disk_changed), NULL);
    labeled(page_disk, "Установить на:", disk_combo);
    disk_warning = gtk_label_new("");
    gtk_widget_set_halign(disk_warning, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(page_disk), disk_warning, FALSE, FALSE, 6);
    GtkWidget *note = gtk_label_new(
        "Если система запущена с флешки — не выбирайте её.\n"
        "Разметка будет создана заново (GPT, автоматически UEFI или BIOS).");
    gtk_widget_set_halign(note, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(note),
        "dim-label");
    gtk_box_pack_start(GTK_BOX(page_disk), note, FALSE, FALSE, 0);
    fill_disks();

    /* 3. Пользователь */
    page_user = make_page(GTK_ASSISTANT(assistant),
        GTK_ASSISTANT_PAGE_CONTENT, "Пользователь и система");
    host_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(host_entry), "shidikudik");
    labeled(page_user, "Имя компьютера:", host_entry);

    tz_combo = gtk_combo_box_text_new_with_entry();
    const gchar *zones[] = {
        "Europe/Moscow", "Europe/Kaliningrad", "Europe/Samara",
        "Asia/Yekaterinburg", "Asia/Omsk", "Asia/Novosibirsk",
        "Asia/Krasnoyarsk", "Asia/Irkutsk", "Asia/Yakutsk",
        "Asia/Vladivostok", "Asia/Almaty", "Asia/Tashkent",
        "Europe/Minsk", "Europe/Kyiv", "Europe/Berlin", "Europe/London",
        "America/New_York", "UTC",
    };
    for (gsize i = 0; i < G_N_ELEMENTS(zones); i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(tz_combo),
            zones[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(tz_combo), 0);
    labeled(page_user, "Часовой пояс:", tz_combo);

    user_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(user_entry), "например ivan");
    g_signal_connect(user_entry, "changed",
        G_CALLBACK(validate_user_page), NULL);
    labeled(page_user, "Имя пользователя:", user_entry);

    pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(pass_entry), FALSE);
    g_signal_connect(pass_entry, "changed",
        G_CALLBACK(validate_user_page), NULL);
    labeled(page_user, "Пароль:", pass_entry);

    pass2_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(pass2_entry), FALSE);
    g_signal_connect(pass2_entry, "changed",
        G_CALLBACK(validate_user_page), NULL);
    labeled(page_user, "Пароль ещё раз:", pass2_entry);

    user_hint = gtk_label_new("");
    gtk_widget_set_halign(user_hint, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(page_user), user_hint, FALSE, FALSE, 6);
    g_signal_connect(host_entry, "changed",
        G_CALLBACK(validate_user_page), NULL);
    validate_user_page(NULL, NULL);

    /* 4. Подтверждение */
    page_confirm = make_page(GTK_ASSISTANT(assistant),
        GTK_ASSISTANT_PAGE_CONFIRM, "Проверьте параметры");
    summary_label = gtk_label_new("");
    gtk_widget_set_halign(summary_label, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(summary_label), TRUE);
    gtk_box_pack_start(GTK_BOX(page_confirm), summary_label, TRUE, TRUE, 0);
    gtk_assistant_set_page_complete(GTK_ASSISTANT(assistant),
        page_confirm, TRUE);

    /* 5. Установка */
    page_install = make_page(GTK_ASSISTANT(assistant),
        GTK_ASSISTANT_PAGE_PROGRESS, "Установка");
    progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress_bar), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_bar),
        "Подготовка…");
    gtk_box_pack_start(GTK_BOX(page_install), progress_bar, FALSE, FALSE, 0);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_view), TRUE);
    gtk_container_add(GTK_CONTAINER(scrolled), log_view);
    gtk_box_pack_start(GTK_BOX(page_install), scrolled, TRUE, TRUE, 0);

    reboot_button = gtk_button_new_with_label("Перезагрузить сейчас");
    gtk_widget_set_halign(reboot_button, GTK_ALIGN_END);
    g_signal_connect(reboot_button, "clicked", G_CALLBACK(on_reboot), NULL);
    gtk_box_pack_start(GTK_BOX(page_install), reboot_button, FALSE, FALSE, 0);

    gtk_widget_show_all(assistant);
    gtk_widget_hide(reboot_button); /* появится после успеха */
    gtk_main();
    return 0;
}
