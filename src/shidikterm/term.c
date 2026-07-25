/* shidik-term — терминал ShidikDE (GTK3 + VTE).
 *
 * Нативный Wayland-терминал в фирменной тёмной палитре дистрибутива.
 * Умеет: свою цветовую схему, Ctrl+Shift+C/V, запуск команды через
 * `shidik-term -e <cmd...>` (используется Shidik-Control для nmtui/passwd).
 *
 * Зависимости: libgtk-3-dev libvte-2.91-dev
 */
#include <gtk/gtk.h>
#include <vte/vte.h>

/* Палитра в тон ShidikudikOS (tokyonight-подобная) */
static const char *PALETTE_HEX[16] = {
    "#15161e", "#f7768e", "#9ece6a", "#e0af68",
    "#7aa2f7", "#bb9af7", "#7dcfff", "#a9b1d6",
    "#414868", "#f7768e", "#9ece6a", "#e0af68",
    "#7aa2f7", "#bb9af7", "#7dcfff", "#c0caf5",
};
#define COLOR_BG "#1a1b26"
#define COLOR_FG "#c0caf5"

static void on_child_exited(VteTerminal *term, gint status, gpointer data) {
    (void)term; (void)status; (void)data;
    gtk_main_quit();
}

static gboolean on_key(GtkWidget *w, GdkEventKey *ev, gpointer term) {
    (void)w;
    guint mods = ev->state & gtk_accelerator_get_default_mod_mask();
    if (mods == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
        if (ev->keyval == GDK_KEY_C || ev->keyval == GDK_KEY_c) {
            vte_terminal_copy_clipboard_format(VTE_TERMINAL(term),
                VTE_FORMAT_TEXT);
            return TRUE;
        }
        if (ev->keyval == GDK_KEY_V || ev->keyval == GDK_KEY_v) {
            vte_terminal_paste_clipboard(VTE_TERMINAL(term));
            return TRUE;
        }
    }
    return FALSE;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    /* командная строка: shidik-term [-e cmd args...] */
    gchar **cmdv;
    if (argc >= 3 && g_strcmp0(argv[1], "-e") == 0) {
        cmdv = g_new0(gchar *, argc - 1);
        for (int i = 2; i < argc; i++)
            cmdv[i - 2] = g_strdup(argv[i]);
    } else {
        const gchar *shell = g_getenv("SHELL");
        if (!shell || !*shell)
            shell = "/bin/bash";
        cmdv = g_new0(gchar *, 2);
        cmdv[0] = g_strdup(shell);
    }

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Shidik-Term");
    gtk_window_set_default_size(GTK_WINDOW(window), 860, 520);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *term = vte_terminal_new();
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(term), 10000);
    vte_terminal_set_mouse_autohide(VTE_TERMINAL(term), TRUE);

    PangoFontDescription *font =
        pango_font_description_from_string("DejaVu Sans Mono 11");
    vte_terminal_set_font(VTE_TERMINAL(term), font);
    pango_font_description_free(font);

    GdkRGBA bg, fg, palette[16];
    gdk_rgba_parse(&bg, COLOR_BG);
    gdk_rgba_parse(&fg, COLOR_FG);
    for (int i = 0; i < 16; i++)
        gdk_rgba_parse(&palette[i], PALETTE_HEX[i]);
    vte_terminal_set_colors(VTE_TERMINAL(term), &fg, &bg, palette, 16);

    g_signal_connect(term, "child-exited",
        G_CALLBACK(on_child_exited), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key), term);

    vte_terminal_spawn_async(VTE_TERMINAL(term), VTE_PTY_DEFAULT,
        NULL,            /* рабочий каталог — унаследовать */
        cmdv, NULL,      /* argv, envv */
        G_SPAWN_SEARCH_PATH,
        NULL, NULL, NULL, /* child setup */
        -1, NULL, NULL, NULL);

    gtk_container_add(GTK_CONTAINER(window), term);
    gtk_widget_show_all(window);
    gtk_main();

    g_strfreev(cmdv);
    return 0;
}
