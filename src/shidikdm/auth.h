/* ShidikDM — модуль авторизации (PAM).
 *
 * Обёртка над libpam: аутентификация пользователя, открытие/закрытие
 * PAM-сессии. Через common-session Debian сюда автоматически подключается
 * pam_systemd, который регистрирует сессию в systemd-logind и создаёт
 * XDG_RUNTIME_DIR — это критично для запуска Wayland-композитора.
 */
#ifndef SHIDIKDM_AUTH_H
#define SHIDIKDM_AUTH_H

#include <security/pam_appl.h>

struct sdm_auth {
    pam_handle_t *pamh;
    char username[128];
};

/* Аутентификация. Возвращает 0 при успехе, иначе код ошибки PAM. */
int sdm_authenticate(struct sdm_auth *a, const char *user,
                     const char *password, const char *tty);

/* Автовход без пароля (live-режим). Использует PAM-сервис
 * shidikdm-autologin, где auth заменён на pam_permit; проверка аккаунта
 * (pam_acct_mgmt) выполняется как обычно. 0 при успехе. */
int sdm_autologin(struct sdm_auth *a, const char *user, const char *tty);

/* Открывает PAM-сессию (pam_setcred + pam_open_session). 0 при успехе. */
int sdm_open_session(struct sdm_auth *a);

/* Закрывает сессию и освобождает PAM-хэндл. */
void sdm_end(struct sdm_auth *a);

#endif
