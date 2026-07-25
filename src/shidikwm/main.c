/* shidikwm — Wayland-композитор ShidikDE (дистрибутив ShidikusikOS).
 *
 * Каркас основан на tinywl из проекта wlroots (лицензия CC0) и рассчитан
 * на wlroots 0.18 (пакет libwlroots-0.18-dev в Debian 13 "trixie").
 *
 * Что уже умеет:
 *   - вывод на все мониторы (wlr_output_layout + wlr_scene);
 *   - окна xdg-shell: отображение, фокус, перемещение (Super+ЛКМ),
 *     изменение размера (Super+ПКМ), Win+Tab, закрытие;
 *   - клавиатура/мышь через libinput, раскладка через xkbcommon;
 *   - запуск автостарт-скрипта (панель, обои) через параметр -s.
 *
 * Чего сознательно нет (точки расширения ShidikDE):
 *   - layer-shell (нужен панели: wlr_layer_shell_v1);
 *   - foreign-toplevel (список окон для таскбара);
 *   - XWayland, workspaces, декорации.
 */
#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

enum swm_cursor_mode {
    SWM_CURSOR_PASSTHROUGH,
    SWM_CURSOR_MOVE,
    SWM_CURSOR_RESIZE,
};

struct swm_server {
    struct wl_display *wl_display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;

    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct wl_list toplevels;

    /* layer-shell: панель и прочие «прибитые» к краю поверхности */
    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_listener new_layer_surface;

    /* Слои сцены, снизу вверх. Порядок создания задаёт z-order, поэтому
     * окна приложений всегда между bottom и top, а панель — выше них. */
    struct wlr_scene_tree *layer_background;
    struct wlr_scene_tree *layer_bottom;
    struct wlr_scene_tree *layer_normal;   /* обычные окна */
    struct wlr_scene_tree *layer_top;
    struct wlr_scene_tree *layer_overlay;

    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;

    struct wlr_seat *seat;
    struct wl_listener new_input;
    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;
    struct wl_list keyboards;

    enum swm_cursor_mode cursor_mode;
    struct swm_toplevel *grabbed_toplevel;
    double grab_x, grab_y;
    struct wlr_box grab_geobox;
    uint32_t resize_edges;

    struct wlr_output_layout *output_layout;
    struct wl_list outputs;
    struct wl_listener new_output;

    /* Тайлинг: окна раскладываются мастер/стек без наложения.
     * Переключается Win+T, доля мастера — Win+[ и Win+]. */
    bool tiling;
    double master_ratio;
    int usable_top;   /* сколько занимает панель сверху (exclusive zone) */
};

struct swm_output {
    struct wl_list link;
    struct swm_server *server;
    struct wlr_output *wlr_output;
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

struct swm_toplevel {
    struct wl_list link;
    struct swm_server *server;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree *scene_tree;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
};

struct swm_layer_surface {
    struct swm_server *server;
    struct wlr_layer_surface_v1 *layer_surface;
    struct wlr_scene_layer_surface_v1 *scene;
    struct wl_listener map;
    struct wl_listener commit;
    struct wl_listener destroy;
};

struct swm_keyboard {
    struct wl_list link;
    struct swm_server *server;
    struct wlr_keyboard *wlr_keyboard;
    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

void arrange_tiling(struct swm_server *server);

/* ---------- фокус ---------- */

static void focus_toplevel(struct swm_toplevel *toplevel) {
    if (toplevel == NULL)
        return;
    struct swm_server *server = toplevel->server;
    struct wlr_seat *seat = server->seat;
    struct wlr_surface *prev = seat->keyboard_state.focused_surface;
    struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
    if (prev == surface)
        return;
    if (prev != NULL) {
        /* сообщаем прежнему окну, что оно потеряло фокус */
        struct wlr_xdg_toplevel *prev_tl =
            wlr_xdg_toplevel_try_from_wlr_surface(prev);
        if (prev_tl != NULL)
            wlr_xdg_toplevel_set_activated(prev_tl, false);
    }
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    wl_list_remove(&toplevel->link);
    wl_list_insert(&server->toplevels, &toplevel->link);
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
    arrange_tiling(server); /* окно с фокусом становится мастером */

    struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat);
    if (kb != NULL) {
        wlr_seat_keyboard_notify_enter(seat, surface, kb->keycodes,
            kb->num_keycodes, &kb->modifiers);
    }
}

/* ---------- тайлинг ---------- */

/* Раскладка «мастер/стек»: первое окно занимает левую часть экрана,
 * остальные делят правую по вертикали. Классика dwm/sway, но без
 * настроек — одна предсказуемая схема. */
void arrange_tiling(struct swm_server *server) {
    if (!server->tiling)
        return;

    int count = wl_list_length(&server->toplevels);
    if (count == 0)
        return;

    struct wlr_box screen;
    wlr_output_layout_get_box(server->output_layout, NULL, &screen);
    if (screen.width <= 0 || screen.height <= 0)
        return;

    /* не залезаем под панель */
    int top = screen.y + server->usable_top;
    int height = screen.height - server->usable_top;
    const int gap = 8;

    int master_w = (count == 1)
        ? screen.width - 2 * gap
        : (int)((screen.width - 3 * gap) * server->master_ratio);

    int i = 0;
    struct swm_toplevel *tl;
    /* toplevels: голова списка — окно с фокусом, оно и есть мастер */
    wl_list_for_each(tl, &server->toplevels, link) {
        struct wlr_box geo;
        wlr_xdg_surface_get_geometry(tl->xdg_toplevel->base, &geo);
        int x, y, w, h;

        if (i == 0) {
            x = screen.x + gap;
            y = top + gap;
            w = master_w;
            h = height - 2 * gap;
        } else {
            int stack_count = count - 1;
            int stack_h = (height - gap * (stack_count + 1)) / stack_count;
            x = screen.x + master_w + 2 * gap;
            y = top + gap + (i - 1) * (stack_h + gap);
            w = screen.width - master_w - 3 * gap;
            h = stack_h;
        }
        if (w < 100) w = 100;
        if (h < 80) h = 80;

        wlr_scene_node_set_position(&tl->scene_tree->node,
            x - geo.x, y - geo.y);
        wlr_xdg_toplevel_set_size(tl->xdg_toplevel, w, h);
        i++;
    }
}

/* ---------- клавиатура ---------- */

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
    (void)data;
    struct swm_keyboard *kb = wl_container_of(listener, kb, modifiers);
    wlr_seat_set_keyboard(kb->server->seat, kb->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(kb->server->seat,
        &kb->wlr_keyboard->modifiers);
}

/* Хоткеи композитора (модификатор — Win/Super, как в Windows и GNOME).
 * Возвращает true, если клавиша обработана и не должна уйти клиенту. */
static bool handle_keybinding(struct swm_server *server, uint32_t mods,
        xkb_keysym_t sym) {
    if (!(mods & WLR_MODIFIER_LOGO))
        return false;
    switch (sym) {
    case XKB_KEY_Escape: /* Win+Esc — выйти из сессии */
        wl_display_terminate(server->wl_display);
        break;
    case XKB_KEY_Tab: {  /* Win+Tab — следующее окно */
        if (wl_list_length(&server->toplevels) < 2)
            break;
        struct swm_toplevel *next =
            wl_container_of(server->toplevels.prev, next, link);
        focus_toplevel(next);
        break;
    }
    case XKB_KEY_Return: /* Win+Enter — терминал */
        if (fork() == 0) {
            execl("/bin/sh", "/bin/sh", "-c",
                "shidik-term || foot", (char *)NULL);
            _exit(1);
        }
        break;
    case XKB_KEY_q:      /* Win+Q — закрыть активное окно */
    case XKB_KEY_Q: {
        if (wl_list_empty(&server->toplevels))
            break;
        struct swm_toplevel *tl =
            wl_container_of(server->toplevels.next, tl, link);
        wlr_xdg_toplevel_send_close(tl->xdg_toplevel);
        break;
    }
    case XKB_KEY_d:      /* Win+D — меню приложений */
    case XKB_KEY_D:
        if (fork() == 0) {
            execl("/bin/sh", "/bin/sh", "-c", "shidiklaunch", (char *)NULL);
            _exit(1);
        }
        break;
    case XKB_KEY_n:      /* Win+N — шторка уведомлений и настроек */
    case XKB_KEY_N:
        if (fork() == 0) {
            execl("/bin/sh", "/bin/sh", "-c", "shidikshade --toggle",
                (char *)NULL);
            _exit(1);
        }
        break;
    case XKB_KEY_l:      /* Win+L — заблокировать экран */
    case XKB_KEY_L:
        if (fork() == 0) {
            execl("/bin/sh", "/bin/sh", "-c", "shidiklock", (char *)NULL);
            _exit(1);
        }
        break;
    case XKB_KEY_t:      /* Win+T — тайлинг вкл/выкл */
    case XKB_KEY_T:
        server->tiling = !server->tiling;
        arrange_tiling(server);
        break;
    case XKB_KEY_bracketleft:  /* Win+[ — мастер уже */
        if (server->master_ratio > 0.25) {
            server->master_ratio -= 0.05;
            arrange_tiling(server);
        }
        break;
    case XKB_KEY_bracketright: /* Win+] — мастер шире */
        if (server->master_ratio < 0.85) {
            server->master_ratio += 0.05;
            arrange_tiling(server);
        }
        break;
    default:
        return false;
    }
    return true;
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
    struct swm_keyboard *kb = wl_container_of(listener, kb, key);
    struct swm_server *server = kb->server;
    struct wlr_keyboard_key_event *event = data;

    /* libinput keycode -> xkb keycode */
    uint32_t keycode = event->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(kb->wlr_keyboard->xkb_state,
        keycode, &syms);

    bool handled = false;
    uint32_t mods = wlr_keyboard_get_modifiers(kb->wlr_keyboard);
    if ((mods & WLR_MODIFIER_LOGO) &&
            event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++)
            handled = handle_keybinding(server, mods, syms[i]) || handled;
    }

    if (!handled) {
        wlr_seat_set_keyboard(server->seat, kb->wlr_keyboard);
        wlr_seat_keyboard_notify_key(server->seat, event->time_msec,
            event->keycode, event->state);
    }
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct swm_keyboard *kb = wl_container_of(listener, kb, destroy);
    wl_list_remove(&kb->modifiers.link);
    wl_list_remove(&kb->key.link);
    wl_list_remove(&kb->destroy.link);
    wl_list_remove(&kb->link);
    free(kb);
}

static void server_new_keyboard(struct swm_server *server,
        struct wlr_input_device *device) {
    struct wlr_keyboard *wlr_keyboard =
        wlr_keyboard_from_input_device(device);
    struct swm_keyboard *kb = calloc(1, sizeof(*kb));
    kb->server = server;
    kb->wlr_keyboard = wlr_keyboard;

    /* Раскладка берётся из XKB_DEFAULT_LAYOUT и т.п. (env) */
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(ctx, NULL,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);
    wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 400);

    kb->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &kb->modifiers);
    kb->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &kb->key);
    kb->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&device->events.destroy, &kb->destroy);

    wlr_seat_set_keyboard(server->seat, wlr_keyboard);
    wl_list_insert(&server->keyboards, &kb->link);
}

static void server_new_pointer(struct swm_server *server,
        struct wlr_input_device *device) {
    wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_input(struct wl_listener *listener, void *data) {
    struct swm_server *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        server_new_keyboard(server, device);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        server_new_pointer(server, device);
        break;
    default:
        break;
    }
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&server->keyboards))
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(server->seat, caps);
}

/* ---------- курсор и интерактив (move/resize) ---------- */

static void seat_request_cursor(struct wl_listener *listener, void *data) {
    struct swm_server *server =
        wl_container_of(listener, server, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    struct wlr_seat_client *focused =
        server->seat->pointer_state.focused_client;
    /* курсор клиенту меняем, только если у него фокус указателя */
    if (focused == event->seat_client) {
        wlr_cursor_set_surface(server->cursor, event->surface,
            event->hotspot_x, event->hotspot_y);
    }
}

static void seat_request_set_selection(struct wl_listener *listener,
        void *data) {
    struct swm_server *server =
        wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

/* Ищет окно под точкой (lx, ly) в сцене. */
static struct swm_toplevel *desktop_toplevel_at(struct swm_server *server,
        double lx, double ly, struct wlr_surface **surface,
        double *sx, double *sy) {
    struct wlr_scene_node *node =
        wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER)
        return NULL;
    struct wlr_scene_buffer *scene_buffer =
        wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *scene_surface =
        wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface)
        return NULL;
    *surface = scene_surface->surface;

    /* поднимаемся до корневого scene_tree окна, где лежит swm_toplevel */
    struct wlr_scene_tree *tree = node->parent;
    while (tree != NULL && tree->node.data == NULL)
        tree = tree->node.parent;
    return tree == NULL ? NULL : tree->node.data;
}

static void reset_cursor_mode(struct swm_server *server) {
    server->cursor_mode = SWM_CURSOR_PASSTHROUGH;
    server->grabbed_toplevel = NULL;
}

static void process_cursor_move(struct swm_server *server) {
    struct swm_toplevel *tl = server->grabbed_toplevel;
    wlr_scene_node_set_position(&tl->scene_tree->node,
        server->cursor->x - server->grab_x,
        server->cursor->y - server->grab_y);
}

static void process_cursor_resize(struct swm_server *server) {
    struct swm_toplevel *tl = server->grabbed_toplevel;
    double border_x = server->cursor->x - server->grab_x;
    double border_y = server->cursor->y - server->grab_y;
    int new_left = server->grab_geobox.x;
    int new_right = server->grab_geobox.x + server->grab_geobox.width;
    int new_top = server->grab_geobox.y;
    int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

    if (server->resize_edges & WLR_EDGE_TOP) {
        new_top = border_y;
        if (new_top >= new_bottom)
            new_top = new_bottom - 1;
    } else if (server->resize_edges & WLR_EDGE_BOTTOM) {
        new_bottom = border_y;
        if (new_bottom <= new_top)
            new_bottom = new_top + 1;
    }
    if (server->resize_edges & WLR_EDGE_LEFT) {
        new_left = border_x;
        if (new_left >= new_right)
            new_left = new_right - 1;
    } else if (server->resize_edges & WLR_EDGE_RIGHT) {
        new_right = border_x;
        if (new_right <= new_left)
            new_right = new_left + 1;
    }

    struct wlr_box geo;
    wlr_xdg_surface_get_geometry(tl->xdg_toplevel->base, &geo);
    wlr_scene_node_set_position(&tl->scene_tree->node,
        new_left - geo.x, new_top - geo.y);
    wlr_xdg_toplevel_set_size(tl->xdg_toplevel,
        new_right - new_left, new_bottom - new_top);
}

static void process_cursor_motion(struct swm_server *server, uint32_t time) {
    if (server->cursor_mode == SWM_CURSOR_MOVE) {
        process_cursor_move(server);
        return;
    } else if (server->cursor_mode == SWM_CURSOR_RESIZE) {
        process_cursor_resize(server);
        return;
    }

    /* passthrough: отдать событие окну под курсором */
    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct swm_toplevel *tl = desktop_toplevel_at(server,
        server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    if (!tl) {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
            "default");
    }
    if (surface) {
        wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(server->seat);
    }
}

static void server_cursor_motion(struct wl_listener *listener, void *data) {
    struct swm_server *server =
        wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    wlr_cursor_move(server->cursor, &event->pointer->base,
        event->delta_x, event->delta_y);
    process_cursor_motion(server, event->time_msec);
}

static void server_cursor_motion_absolute(struct wl_listener *listener,
        void *data) {
    struct swm_server *server =
        wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base,
        event->x, event->y);
    process_cursor_motion(server, event->time_msec);
}

static void server_cursor_button(struct wl_listener *listener, void *data) {
    struct swm_server *server =
        wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;
    wlr_seat_pointer_notify_button(server->seat, event->time_msec,
        event->button, event->state);

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        reset_cursor_mode(server);
    } else {
        /* клик — фокус окну под курсором */
        double sx, sy;
        struct wlr_surface *surface = NULL;
        struct swm_toplevel *tl = desktop_toplevel_at(server,
            server->cursor->x, server->cursor->y, &surface, &sx, &sy);
        focus_toplevel(tl);
    }
}

static void server_cursor_axis(struct wl_listener *listener, void *data) {
    struct swm_server *server =
        wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
        event->orientation, event->delta, event->delta_discrete,
        event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
    (void)data;
    struct swm_server *server =
        wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}

/* ---------- вывод (мониторы) ---------- */

static void output_frame(struct wl_listener *listener, void *data) {
    (void)data;
    struct swm_output *output = wl_container_of(listener, output, frame);
    struct wlr_scene *scene = output->server->scene;
    struct wlr_scene_output *scene_output =
        wlr_scene_get_scene_output(scene, output->wlr_output);

    wlr_scene_output_commit(scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data) {
    struct swm_output *output =
        wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;
    wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct swm_output *output = wl_container_of(listener, output, destroy);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    free(output);
}

static void server_new_output(struct wl_listener *listener, void *data) {
    struct swm_server *server =
        wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    /* включаем монитор в предпочтительном режиме */
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode != NULL)
        wlr_output_state_set_mode(&state, mode);
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    struct swm_output *output = calloc(1, sizeof(*output));
    output->wlr_output = wlr_output;
    output->server = server;
    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);
    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);
    wl_list_insert(&server->outputs, &output->link);

    struct wlr_output_layout_output *l_output =
        wlr_output_layout_add_auto(server->output_layout, wlr_output);
    struct wlr_scene_output *scene_output =
        wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_layout, l_output,
        scene_output);
}

/* ---------- окна xdg-shell ---------- */

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct swm_toplevel *tl = wl_container_of(listener, tl, map);
    wl_list_insert(&tl->server->toplevels, &tl->link);

    if (tl->server->tiling) {
        /* в тайлинге позицию и размер задаёт раскладка */
        arrange_tiling(tl->server);
        focus_toplevel(tl);
        return;
    }

    /* Новое окно — по центру экрана. Без этого все окна открываются в
     * левом верхнем углу друг на друге. Каждое следующее чуть смещаем,
     * чтобы одинаковые окна не сливались в одно. */
    struct wlr_box screen;
    wlr_output_layout_get_box(tl->server->output_layout, NULL, &screen);
    if (screen.width > 0 && screen.height > 0) {
        struct wlr_box geo;
        wlr_xdg_surface_get_geometry(tl->xdg_toplevel->base, &geo);
        if (geo.width > 0 && geo.height > 0) {
            static int cascade;
            int offset = (cascade % 5) * 28;
            cascade++;

            int x = screen.x + (screen.width - geo.width) / 2 + offset;
            int y = screen.y + (screen.height - geo.height) / 2 + offset;
            /* не даём окну уехать за пределы вывода */
            int max_x = screen.x + screen.width - geo.width;
            int max_y = screen.y + screen.height - geo.height;
            if (x > max_x) x = max_x;
            if (y > max_y) y = max_y;
            if (x < screen.x) x = screen.x;
            if (y < screen.y) y = screen.y;
            wlr_scene_node_set_position(&tl->scene_tree->node,
                x - geo.x, y - geo.y);
        }
    }

    focus_toplevel(tl);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct swm_toplevel *tl = wl_container_of(listener, tl, unmap);
    if (tl == tl->server->grabbed_toplevel)
        reset_cursor_mode(tl->server);
    wl_list_remove(&tl->link);
    arrange_tiling(tl->server); /* оставшиеся окна перезаполняют экран */
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct swm_toplevel *tl = wl_container_of(listener, tl, commit);
    if (tl->xdg_toplevel->base->initial_commit) {
        /* 0x0 = "выбери размер сам" — обязательный ответ на первый commit */
        wlr_xdg_toplevel_set_size(tl->xdg_toplevel, 0, 0);
    }
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct swm_toplevel *tl = wl_container_of(listener, tl, destroy);
    wl_list_remove(&tl->map.link);
    wl_list_remove(&tl->unmap.link);
    wl_list_remove(&tl->commit.link);
    wl_list_remove(&tl->destroy.link);
    wl_list_remove(&tl->request_move.link);
    wl_list_remove(&tl->request_resize.link);
    wl_list_remove(&tl->request_maximize.link);
    wl_list_remove(&tl->request_fullscreen.link);
    free(tl);
}

/* Начало интерактивного перемещения/ресайза (по запросу клиента или
 * по Super+кнопка — здесь упрощённо доверяем клиенту). */
static void begin_interactive(struct swm_toplevel *tl,
        enum swm_cursor_mode mode, uint32_t edges) {
    struct swm_server *server = tl->server;
    server->grabbed_toplevel = tl;
    server->cursor_mode = mode;

    if (mode == SWM_CURSOR_MOVE) {
        server->grab_x = server->cursor->x - tl->scene_tree->node.x;
        server->grab_y = server->cursor->y - tl->scene_tree->node.y;
    } else {
        struct wlr_box geo;
        wlr_xdg_surface_get_geometry(tl->xdg_toplevel->base, &geo);
        double border_x = (tl->scene_tree->node.x + geo.x) +
            ((edges & WLR_EDGE_RIGHT) ? geo.width : 0);
        double border_y = (tl->scene_tree->node.y + geo.y) +
            ((edges & WLR_EDGE_BOTTOM) ? geo.height : 0);
        server->grab_x = server->cursor->x - border_x;
        server->grab_y = server->cursor->y - border_y;
        server->grab_geobox = geo;
        server->grab_geobox.x += tl->scene_tree->node.x;
        server->grab_geobox.y += tl->scene_tree->node.y;
        server->resize_edges = edges;
    }
}

static void xdg_toplevel_request_move(struct wl_listener *listener,
        void *data) {
    (void)data;
    struct swm_toplevel *tl = wl_container_of(listener, tl, request_move);
    begin_interactive(tl, SWM_CURSOR_MOVE, 0);
}

static void xdg_toplevel_request_resize(struct wl_listener *listener,
        void *data) {
    struct wlr_xdg_toplevel_resize_event *event = data;
    struct swm_toplevel *tl = wl_container_of(listener, tl, request_resize);
    begin_interactive(tl, SWM_CURSOR_RESIZE, event->edges);
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener,
        void *data) {
    (void)data;
    /* Максимизацию пока не поддерживаем, но по протоколу обязаны
     * ответить configure. */
    struct swm_toplevel *tl =
        wl_container_of(listener, tl, request_maximize);
    if (tl->xdg_toplevel->base->initialized)
        wlr_xdg_surface_schedule_configure(tl->xdg_toplevel->base);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener,
        void *data) {
    (void)data;
    /* Растягиваем окно на первый вывод (нужно greeter'у и видео). */
    struct swm_toplevel *tl =
        wl_container_of(listener, tl, request_fullscreen);
    if (!tl->xdg_toplevel->base->initialized)
        return;
    bool want = tl->xdg_toplevel->requested.fullscreen;
    if (want) {
        struct wlr_box box;
        wlr_output_layout_get_box(tl->server->output_layout, NULL, &box);
        wlr_scene_node_set_position(&tl->scene_tree->node, box.x, box.y);
        wlr_xdg_toplevel_set_size(tl->xdg_toplevel, box.width, box.height);
    }
    wlr_xdg_toplevel_set_fullscreen(tl->xdg_toplevel, want);
}

static void server_new_xdg_toplevel(struct wl_listener *listener,
        void *data) {
    struct swm_server *server =
        wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *xdg_toplevel = data;

    struct swm_toplevel *tl = calloc(1, sizeof(*tl));
    tl->server = server;
    tl->xdg_toplevel = xdg_toplevel;
    tl->scene_tree = wlr_scene_xdg_surface_create(server->layer_normal,
        xdg_toplevel->base);
    tl->scene_tree->node.data = tl;          /* для desktop_toplevel_at */
    xdg_toplevel->base->data = tl->scene_tree; /* для попапов */

    tl->map.notify = xdg_toplevel_map;
    wl_signal_add(&xdg_toplevel->base->surface->events.map, &tl->map);
    tl->unmap.notify = xdg_toplevel_unmap;
    wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &tl->unmap);
    tl->commit.notify = xdg_toplevel_commit;
    wl_signal_add(&xdg_toplevel->base->surface->events.commit, &tl->commit);
    tl->destroy.notify = xdg_toplevel_destroy;
    wl_signal_add(&xdg_toplevel->events.destroy, &tl->destroy);
    tl->request_move.notify = xdg_toplevel_request_move;
    wl_signal_add(&xdg_toplevel->events.request_move, &tl->request_move);
    tl->request_resize.notify = xdg_toplevel_request_resize;
    wl_signal_add(&xdg_toplevel->events.request_resize, &tl->request_resize);
    tl->request_maximize.notify = xdg_toplevel_request_maximize;
    wl_signal_add(&xdg_toplevel->events.request_maximize,
        &tl->request_maximize);
    tl->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
    wl_signal_add(&xdg_toplevel->events.request_fullscreen,
        &tl->request_fullscreen);
}

/* ---------- layer-shell (панель) ---------- */

/* Пересчитывает положение и размер layer-поверхности по её якорям.
 * Всю арифметику делает helper wlr_scene_layer_surface_v1_configure. */
static void layer_surface_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct swm_layer_surface *ls = wl_container_of(listener, ls, commit);
    struct wlr_layer_surface_v1 *surface = ls->layer_surface;
    if (!surface->initialized)
        return;

    struct wlr_box full = {0};
    wlr_output_layout_get_box(ls->server->output_layout,
        surface->output, &full);
    struct wlr_box usable = full; /* exclusive zone вычитается helper'ом */
    wlr_scene_layer_surface_v1_configure(ls->scene, &full, &usable);

    /* Запоминаем, сколько «съела» панель сверху — тайлинг не должен
     * раскладывать окна под ней. */
    int top = usable.y - full.y;
    if (top != ls->server->usable_top) {
        ls->server->usable_top = top;
        arrange_tiling(ls->server);
    }
}

/* Поверхность просит клавиатуру (экран блокировки, меню) — отдаём фокус.
 * Без этого в shidiklock нельзя было бы ввести пароль. */
static void layer_surface_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct swm_layer_surface *ls = wl_container_of(listener, ls, map);
    struct wlr_layer_surface_v1 *surface = ls->layer_surface;
    if (surface->current.keyboard_interactive ==
            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE)
        return;

    struct wlr_keyboard *kb = wlr_seat_get_keyboard(ls->server->seat);
    if (kb != NULL) {
        wlr_seat_keyboard_notify_enter(ls->server->seat, surface->surface,
            kb->keycodes, kb->num_keycodes, &kb->modifiers);
    }
}

static void layer_surface_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct swm_layer_surface *ls = wl_container_of(listener, ls, destroy);
    struct swm_server *server = ls->server;

    wl_list_remove(&ls->map.link);
    wl_list_remove(&ls->commit.link);
    wl_list_remove(&ls->destroy.link);
    free(ls);

    /* Блокировка снялась — вернуть клавиатуру верхнему окну. */
    if (!wl_list_empty(&server->toplevels)) {
        struct swm_toplevel *tl =
            wl_container_of(server->toplevels.next, tl, link);
        struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
        if (kb != NULL) {
            wlr_seat_keyboard_notify_enter(server->seat,
                tl->xdg_toplevel->base->surface,
                kb->keycodes, kb->num_keycodes, &kb->modifiers);
        }
    }
}

static void server_new_layer_surface(struct wl_listener *listener,
        void *data) {
    struct swm_server *server =
        wl_container_of(listener, server, new_layer_surface);
    struct wlr_layer_surface_v1 *surface = data;

    /* Клиент может не указать вывод — тогда берём первый доступный. */
    if (surface->output == NULL) {
        if (wl_list_empty(&server->outputs)) {
            wlr_layer_surface_v1_destroy(surface);
            return;
        }
        struct swm_output *output =
            wl_container_of(server->outputs.next, output, link);
        surface->output = output->wlr_output;
    }

    struct wlr_scene_tree *parent;
    switch (surface->pending.layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
        parent = server->layer_background;
        break;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
        parent = server->layer_bottom;
        break;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
        parent = server->layer_overlay;
        break;
    default:
        parent = server->layer_top;
        break;
    }

    struct swm_layer_surface *ls = calloc(1, sizeof(*ls));
    ls->server = server;
    ls->layer_surface = surface;
    ls->scene = wlr_scene_layer_surface_v1_create(parent, surface);

    ls->map.notify = layer_surface_map;
    wl_signal_add(&surface->surface->events.map, &ls->map);
    ls->commit.notify = layer_surface_commit;
    wl_signal_add(&surface->surface->events.commit, &ls->commit);
    ls->destroy.notify = layer_surface_destroy;
    wl_signal_add(&surface->events.destroy, &ls->destroy);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_xdg_popup *xdg_popup = data;
    /* Родитель попапа — xdg_surface; его scene_tree мы сохранили в data. */
    struct wlr_xdg_surface *parent =
        wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    assert(parent != NULL);
    struct wlr_scene_tree *parent_tree = parent->data;
    xdg_popup->base->data =
        wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);
}

/* ---------- main ---------- */

int main(int argc, char *argv[]) {
    wlr_log_init(WLR_INFO, NULL);
    char *startup_cmd = NULL;

    int c;
    while ((c = getopt(argc, argv, "s:h")) != -1) {
        switch (c) {
        case 's':
            startup_cmd = optarg;
            break;
        default:
            printf("Usage: %s [-s startup command]\n", argv[0]);
            return 0;
        }
    }

    struct swm_server server = {0};
    server.master_ratio = 0.58; /* мастер чуть больше половины экрана */
    server.wl_display = wl_display_create();
    server.backend = wlr_backend_autocreate(
        wl_display_get_event_loop(server.wl_display), NULL);
    if (server.backend == NULL) {
        wlr_log(WLR_ERROR, "не удалось создать wlr_backend");
        return 1;
    }

    server.renderer = wlr_renderer_autocreate(server.backend);
    wlr_renderer_init_wl_display(server.renderer, server.wl_display);
    server.allocator = wlr_allocator_autocreate(server.backend,
        server.renderer);

    /* базовые wayland-глобалы */
    wlr_compositor_create(server.wl_display, 5, server.renderer);
    wlr_subcompositor_create(server.wl_display);
    wlr_data_device_manager_create(server.wl_display);

    server.output_layout = wlr_output_layout_create(server.wl_display);
    wl_list_init(&server.outputs);
    server.new_output.notify = server_new_output;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);

    server.scene = wlr_scene_create();
    server.scene_layout = wlr_scene_attach_output_layout(server.scene,
        server.output_layout);

    /* Фон: тёмная заливка в тон логотипа (создаётся первым — лежит под
     * всем остальным). До полноценных обоев через layer-shell. */
    wlr_scene_rect_create(&server.scene->tree, 8192, 8192,
        (float[4]){0.102f, 0.106f, 0.149f, 1.0f});

    /* Слои сцены. Порядок создания = порядок наложения: обычные окна
     * всегда ниже панели (layer_top), но выше обоев. */
    server.layer_background = wlr_scene_tree_create(&server.scene->tree);
    server.layer_bottom = wlr_scene_tree_create(&server.scene->tree);
    server.layer_normal = wlr_scene_tree_create(&server.scene->tree);
    server.layer_top = wlr_scene_tree_create(&server.scene->tree);
    server.layer_overlay = wlr_scene_tree_create(&server.scene->tree);

    wl_list_init(&server.toplevels);
    server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
    server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
    wl_signal_add(&server.xdg_shell->events.new_toplevel,
        &server.new_xdg_toplevel);
    server.new_xdg_popup.notify = server_new_xdg_popup;
    wl_signal_add(&server.xdg_shell->events.new_popup,
        &server.new_xdg_popup);

    /* layer-shell: без него панель становится обычным окном посреди
     * экрана вместо полосы у края. */
    server.layer_shell = wlr_layer_shell_v1_create(server.wl_display, 4);
    server.new_layer_surface.notify = server_new_layer_surface;
    wl_signal_add(&server.layer_shell->events.new_surface,
        &server.new_layer_surface);

    server.cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
    server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    server.cursor_mode = SWM_CURSOR_PASSTHROUGH;
    server.cursor_motion.notify = server_cursor_motion;
    wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
    server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
    wl_signal_add(&server.cursor->events.motion_absolute,
        &server.cursor_motion_absolute);
    server.cursor_button.notify = server_cursor_button;
    wl_signal_add(&server.cursor->events.button, &server.cursor_button);
    server.cursor_axis.notify = server_cursor_axis;
    wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
    server.cursor_frame.notify = server_cursor_frame;
    wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

    wl_list_init(&server.keyboards);
    server.new_input.notify = server_new_input;
    wl_signal_add(&server.backend->events.new_input, &server.new_input);
    server.seat = wlr_seat_create(server.wl_display, "seat0");
    server.request_cursor.notify = seat_request_cursor;
    wl_signal_add(&server.seat->events.request_set_cursor,
        &server.request_cursor);
    server.request_set_selection.notify = seat_request_set_selection;
    wl_signal_add(&server.seat->events.request_set_selection,
        &server.request_set_selection);

    const char *socket = wl_display_add_socket_auto(server.wl_display);
    if (!socket) {
        wlr_backend_destroy(server.backend);
        return 1;
    }
    if (!wlr_backend_start(server.backend)) {
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }

    setenv("WAYLAND_DISPLAY", socket, true);
    if (startup_cmd) {
        if (fork() == 0) {
            execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (char *)NULL);
            _exit(1);
        }
    }

    wlr_log(WLR_INFO, "shidikwm запущен на WAYLAND_DISPLAY=%s", socket);
    wl_display_run(server.wl_display);

    /* завершение */
    wl_display_destroy_clients(server.wl_display);
    wlr_scene_node_destroy(&server.scene->tree.node);
    wlr_xcursor_manager_destroy(server.cursor_mgr);
    wlr_cursor_destroy(server.cursor);
    wlr_allocator_destroy(server.allocator);
    wlr_renderer_destroy(server.renderer);
    wlr_backend_destroy(server.backend);
    wl_display_destroy(server.wl_display);
    return 0;
}
