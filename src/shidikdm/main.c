/* ShidikDM — Display Manager дистрибутива ShidikudikOS.
 *
 * Два режима greeter'а:
 *
 *   ГРАФИЧЕСКИЙ (по умолчанию, если установлен shidikgreet):
 *     shidikdm поднимает выделенный композитор с экраном входа
 *     (seatd-launch shidikwm -s shidikgreet) и слушает unix-сокет.
 *     Greeter присылает "<user>\0<pass>\0", shidikdm проверяет пару через
 *     PAM (auth.c) и отвечает 'O'/'F'. После успеха greeter-композитор
 *     гасится, и на освободившемся seat запускается сессия пользователя.
 *     Схема как у greetd/SDDM: UI без привилегий, авторизация — у root.
 *
 *   КОНСОЛЬНЫЙ (fallback): классический текстовый вход на tty. Включается
 *     переменной SHIDIKDM_CONSOLE=1, отсутствием shidikgreet или если
 *     графический greeter упал — чтобы никогда не запереть машину.
 *
 * Работает под root (systemd, tty1); привилегии сбрасываются только в
 * форкнутом процессе сессии (session.c).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "auth.h"
#include "session.h"

#define DEFAULT_SESSION "/usr/local/bin/shidikde-session"
#define GREETER_BIN "/usr/local/bin/shidikgreet"
#define AUTOLOGIN_CONF "/etc/shidikudik/autologin"
#define MAX_ATTEMPTS 3

static const char *BANNER =
    "\033[2J\033[H"
    "\033[38;5;215m"
    "      \\ /       \\ /\n"
    "      (\\)  ___  (/)\n"
    "       \\\\ /o o\\ //\n"
    "        (   v   )        \033[1mShidikudikOS\033[0m\033[38;5;215m\n"
    "         \\ \\_/ /         маленький · быстрый · свой\n"
    "          '---'\n"
    "\033[0m\n";

/* ---------- консольный greeter ---------- */

static int read_password(char *buf, size_t len) {
    struct termios old, new;
    if (tcgetattr(STDIN_FILENO, &old) != 0)
        return -1;
    new = old;
    new.c_lflag &= ~(tcflag_t)ECHO;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &new);
    char *r = fgets(buf, len, stdin);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
    printf("\n");
    if (!r)
        return -1;
    buf[strcspn(buf, "\n")] = '\0';
    return 0;
}

static int read_line(const char *prompt, char *buf, size_t len) {
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, len, stdin))
        return -1;
    buf[strcspn(buf, "\n")] = '\0';
    return 0;
}

/* Консольный вход. 0 — auth заполнен, -1 — не удалось. */
static int console_greeter(struct sdm_auth *auth, const char *tty) {
    printf("%s", BANNER);
    char user[128] = {0}, pass[256] = {0};
    int attempts = 0;

    while (attempts < MAX_ATTEMPTS) {
        if (read_line("  логин: ", user, sizeof(user)) != 0 ||
            user[0] == '\0')
            continue;
        printf("  пароль: ");
        fflush(stdout);
        if (read_password(pass, sizeof(pass)) != 0)
            continue;

        int ok = (sdm_authenticate(auth, user, pass, tty) == 0);
        explicit_bzero(pass, sizeof(pass));
        if (ok)
            return 0;
        attempts++;
        printf("\n  \033[31mневерный логин или пароль\033[0m\n\n");
        sleep(1);
    }
    return -1;
}

/* ---------- графический greeter ---------- */

/* Читает из fd C-строку (до NUL). 0 — успех, -1 — EOF/ошибка. */
static int read_cstring(int fd, char *buf, size_t len) {
    size_t i = 0;
    while (i < len - 1) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0)
            return -1;
        buf[i++] = c;
        if (c == '\0')
            return 0;
    }
    buf[len - 1] = '\0';
    return 0;
}

static pid_t spawn_greeter(int *fd_out) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(sv[0]);
        close(sv[1]);
        return -1;
    }
    if (pid == 0) {
        /* конец сокета greeter'а закрепляем за fd 3 — shidikwm и /bin/sh
         * наследуют его до самого shidikgreet */
        if (dup2(sv[1], 3) < 0)
            _exit(127);
        close(sv[0]);
        if (sv[1] != 3)
            close(sv[1]);
        setenv("SHIDIKDM_FD", "3", 1);
        execl("/bin/sh", "/bin/sh", "-c",
            "exec seatd-launch shidikwm -s shidikgreet", (char *)NULL);
        _exit(127);
    }
    close(sv[1]);
    *fd_out = sv[0];
    return pid;
}

/* Графический вход. 0 — auth заполнен; -1 — greeter недоступен/упал
 * (вызывающий откатывается на консоль). */
static int graphical_greeter(struct sdm_auth *auth, const char *tty) {
    int fd = -1;
    pid_t pid = spawn_greeter(&fd);
    if (pid < 0)
        return -1;

    int result = -1;
    char user[128], pass[256];

    for (;;) {
        if (read_cstring(fd, user, sizeof(user)) != 0 ||
            read_cstring(fd, pass, sizeof(pass)) != 0)
            break; /* greeter умер или закрыл сокет */

        char resp = (sdm_authenticate(auth, user, pass, tty) == 0)
            ? 'O' : 'F';
        explicit_bzero(pass, sizeof(pass));

        if (write(fd, &resp, 1) < 0)
            break;
        if (resp == 'O') {
            result = 0;
            break;
        }
    }

    close(fd);
    /* гасим greeter-композитор, чтобы он освободил DRM/VT для сессии */
    kill(pid, SIGTERM);
    for (int i = 0; i < 50; i++) { /* до 5 секунд на корректный выход */
        if (waitpid(pid, NULL, WNOHANG) == pid)
            break;
        usleep(100000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, NULL, WNOHANG);
    return result;
}

/* ---------- автовход ---------- */

/* Читает имя пользователя из /etc/shidikudik/autologin (одна строка).
 * Возвращает malloc'нутую строку или NULL, если автовход не настроен. */
static char *autologin_user(void) {
    FILE *f = fopen(AUTOLOGIN_CONF, "r");
    if (!f)
        return NULL;

    char buf[128] = {0};
    char *line = fgets(buf, sizeof(buf), f);
    fclose(f);
    if (!line)
        return NULL;

    buf[strcspn(buf, "\r\n")] = '\0';
    if (buf[0] == '\0' || buf[0] == '#')
        return NULL;
    return strdup(buf);
}

/* ---------- main ---------- */

int main(int argc, char *argv[]) {
    const char *session_cmd = (argc > 1) ? argv[1] : DEFAULT_SESSION;
    const char *tty = ttyname(STDIN_FILENO);
    if (!tty)
        tty = "/dev/tty1";

    signal(SIGPIPE, SIG_IGN);

    int use_graphical = !getenv("SHIDIKDM_CONSOLE") &&
        access(GREETER_BIN, X_OK) == 0;

    /* Автовход срабатывает один раз — при старте системы. После выхода
     * из сессии показываем обычный greeter, иначе выйти было бы нельзя. */
    char *auto_user = autologin_user();

    for (;;) {
        struct sdm_auth auth;
        int authed = -1;

        if (auto_user) {
            printf("%s  вход без пароля: %s\n", BANNER, auto_user);
            authed = sdm_autologin(&auth, auto_user, tty);
            if (authed != 0)
                fprintf(stderr,
                    "shidikdm: автовход для %s не удался, спрашиваю пароль\n",
                    auto_user);
            free(auto_user);
            auto_user = NULL;
        }

        if (authed != 0 && use_graphical)
            authed = graphical_greeter(&auth, tty);
        if (authed != 0)
            authed = console_greeter(&auth, tty);
        if (authed != 0)
            continue; /* исчерпаны попытки — начать заново */

        if (sdm_open_session(&auth) != 0) {
            fprintf(stderr, "shidikdm: не удалось открыть сессию\n");
            sdm_end(&auth);
            sleep(2);
            continue;
        }

        sdm_run_session(&auth, session_cmd);
        sdm_end(&auth);
    }
}
