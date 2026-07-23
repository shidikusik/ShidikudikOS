/* shidik-session-ctl — менеджер сессии ShidikDE.
 *
 * Выключение/перезагрузка/сон идут через systemd-logind по D-Bus
 * (libsystemd sd-bus). logind сам проверяет polkit-права, поэтому
 * обычный пользователь активной локальной сессии может выключать
 * машину без root.
 *
 * logout реализован завершением собственной logind-сессии: logind
 * убивает все процессы сессии, включая композитор, и ShidikDM
 * возвращается к экрану входа.
 *
 * Использование: shidik-session-ctl {logout|poweroff|reboot|suspend}
 *
 * Зависимости: libsystemd-dev
 */
#include <stdio.h>
#include <string.h>
#include <systemd/sd-bus.h>

#define LOGIND_DEST "org.freedesktop.login1"
#define LOGIND_PATH "/org/freedesktop/login1"
#define LOGIND_MGR  "org.freedesktop.login1.Manager"

/* PowerOff/Reboot/Suspend: один булев аргумент "interactive"
 * (true = разрешить polkit спросить пароль при необходимости). */
static int call_power_method(const char *method) {
    sd_bus *bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r = sd_bus_default_system(&bus);
    if (r < 0) {
        fprintf(stderr, "sd_bus_default_system: %s\n", strerror(-r));
        return 1;
    }

    r = sd_bus_call_method(bus, LOGIND_DEST, LOGIND_PATH, LOGIND_MGR,
        method, &error, NULL, "b", 1);
    if (r < 0)
        fprintf(stderr, "%s: %s\n", method,
            error.message ? error.message : strerror(-r));

    sd_bus_error_free(&error);
    sd_bus_unref(bus);
    return r < 0 ? 1 : 0;
}

static int terminate_own_session(void) {
    sd_bus *bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r = sd_bus_default_system(&bus);
    if (r < 0) {
        fprintf(stderr, "sd_bus_default_system: %s\n", strerror(-r));
        return 1;
    }

    /* "" = сессия вызывающего процесса (logind определяет по cgroup) */
    r = sd_bus_call_method(bus, LOGIND_DEST, LOGIND_PATH, LOGIND_MGR,
        "TerminateSession", &error, NULL, "s", "");
    if (r < 0)
        fprintf(stderr, "TerminateSession: %s\n",
            error.message ? error.message : strerror(-r));

    sd_bus_error_free(&error);
    sd_bus_unref(bus);
    return r < 0 ? 1 : 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr,
            "usage: %s {logout|poweroff|reboot|suspend}\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "poweroff") == 0)
        return call_power_method("PowerOff");
    if (strcmp(argv[1], "reboot") == 0)
        return call_power_method("Reboot");
    if (strcmp(argv[1], "suspend") == 0)
        return call_power_method("Suspend");
    if (strcmp(argv[1], "logout") == 0)
        return terminate_own_session();

    fprintf(stderr, "неизвестная команда: %s\n", argv[1]);
    return 2;
}
