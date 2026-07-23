/* ShidikDM — Display Manager дистрибутива ShidikudikOS.
 *
 * Минималистичный консольный greeter в духе greetd/ly:
 *   1. рисует баннер на выделенном VT (tty1, выдаётся systemd-юнитом);
 *   2. спрашивает логин/пароль;
 *   3. авторизует через PAM (auth.c);
 *   4. открывает logind-сессию и запускает shidikde-session (session.c);
 *   5. после завершения сессии возвращается к шагу 1.
 *
 * Работает под root (запускается systemd), привилегии сбрасываются
 * только в форкнутом процессе сессии.
 */
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "auth.h"
#include "session.h"

#define DEFAULT_SESSION "/usr/local/bin/shidikde-session"
#define MAX_ATTEMPTS 3

static const char *BANNER =
    "\033[2J\033[H"           /* очистить экран, курсор в начало */
    "\033[38;5;215m"
    "      \\ /       \\ /\n"
    "      (\\)  ___  (/)\n"
    "       \\\\ /o o\\ //\n"
    "        (   v   )        \033[1mShidikudikOS\033[0m\033[38;5;215m\n"
    "         \\ \\_/ /         маленький · быстрый · свой\n"
    "          '---'\n"
    "\033[0m\n";

/* Читает строку без эха (для пароля). */
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

int main(int argc, char *argv[]) {
    const char *session_cmd =
        (argc > 1) ? argv[1] : DEFAULT_SESSION;
    const char *tty = ttyname(STDIN_FILENO);
    if (!tty)
        tty = "/dev/tty1";

    /* Сессия умирает — DM должен жить. */
    signal(SIGPIPE, SIG_IGN);

    for (;;) {
        printf("%s", BANNER);

        char user[128] = {0}, pass[256] = {0};
        int attempts = 0;
        struct sdm_auth auth;
        int authed = 0;

        while (attempts < MAX_ATTEMPTS && !authed) {
            if (read_line("  логин: ", user, sizeof(user)) != 0 ||
                user[0] == '\0')
                continue;
            printf("  пароль: ");
            fflush(stdout);
            if (read_password(pass, sizeof(pass)) != 0)
                continue;

            if (sdm_authenticate(&auth, user, pass, tty) == 0) {
                authed = 1;
            } else {
                attempts++;
                printf("\n  \033[31mневерный логин или пароль\033[0m\n\n");
                sleep(1); /* лёгкая защита от перебора */
            }
            explicit_bzero(pass, sizeof(pass));
        }

        if (!authed)
            continue; /* перерисовать баннер и начать заново */

        if (sdm_open_session(&auth) != 0) {
            fprintf(stderr, "shidikdm: не удалось открыть сессию\n");
            sdm_end(&auth);
            sleep(2);
            continue;
        }

        printf("\n  запускаю ShidikDE...\n");
        sdm_run_session(&auth, session_cmd);

        sdm_end(&auth); /* закрыть logind-сессию */
    }
}
