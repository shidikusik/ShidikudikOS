#define _GNU_SOURCE
#include "session.h"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Собирает окружение дочернего процесса: базовые переменные пользователя
 * плюс всё, что накопил PAM-стек (в т.ч. XDG_RUNTIME_DIR от pam_systemd). */
static void build_env(struct sdm_auth *a, struct passwd *pw) {
    clearenv();
    setenv("HOME", pw->pw_dir, 1);
    setenv("USER", pw->pw_name, 1);
    setenv("LOGNAME", pw->pw_name, 1);
    setenv("SHELL", pw->pw_shell, 1);
    setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
    setenv("XDG_CURRENT_DESKTOP", "ShidikDE", 1);
    setenv("XDG_SESSION_TYPE", "wayland", 1);

    char **pam_env = pam_getenvlist(a->pamh);
    if (pam_env) {
        for (char **e = pam_env; *e; e++) {
            putenv(*e); /* строки становятся частью environ — не free */
        }
        free(pam_env);
    }
}

int sdm_run_session(struct sdm_auth *a, const char *session_cmd) {
    struct passwd *pw = getpwnam(a->username);
    if (!pw) {
        fprintf(stderr, "shidikdm: пользователь %s не найден\n", a->username);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        /* --- дочерний процесс: становимся пользователем --- */
        if (setsid() < 0)
            _exit(1);

        /* Порядок сброса привилегий важен: группы -> gid -> uid */
        if (initgroups(pw->pw_name, pw->pw_gid) != 0 ||
            setgid(pw->pw_gid) != 0 ||
            setuid(pw->pw_uid) != 0) {
            perror("drop privileges");
            _exit(1);
        }

        build_env(a, pw);

        if (chdir(pw->pw_dir) != 0 && chdir("/") != 0)
            _exit(1);

        /* Через login-shell пользователя, чтобы подхватить ~/.profile */
        execl(pw->pw_shell, pw->pw_shell, "-l", "-c", session_cmd, (char *)NULL);
        perror("exec session");
        _exit(1);
    }

    /* --- родитель (root): ждём конца сессии --- */
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
