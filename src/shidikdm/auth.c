#define _POSIX_C_SOURCE 200809L /* strdup при -std=c11 */
#include "auth.h"

#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDM_PAM_SERVICE "shidikdm" /* -> /etc/pam.d/shidikdm */
#define SDM_PAM_AUTOLOGIN_SERVICE "shidikdm-autologin"

/* PAM-конверсация: PAM задаёт вопросы (логин/пароль/инфо), мы отвечаем.
 * Пароль уже собран UI-слоем и передаётся через appdata_ptr, поэтому
 * на PAM_PROMPT_ECHO_OFF просто отдаём его копию (PAM сам делает free). */
static int sdm_conv(int num_msg, const struct pam_message **msg,
                    struct pam_response **resp, void *appdata_ptr) {
    const char *password = appdata_ptr;

    struct pam_response *replies = calloc(num_msg, sizeof(*replies));
    if (!replies)
        return PAM_BUF_ERR;

    for (int i = 0; i < num_msg; i++) {
        switch (msg[i]->msg_style) {
        case PAM_PROMPT_ECHO_OFF: /* запрос пароля */
            replies[i].resp = strdup(password ? password : "");
            break;
        case PAM_PROMPT_ECHO_ON:  /* запрос логина (обычно не приходит) */
            replies[i].resp = strdup("");
            break;
        case PAM_ERROR_MSG:
            fprintf(stderr, "pam: %s\n", msg[i]->msg);
            break;
        case PAM_TEXT_INFO:
            printf("pam: %s\n", msg[i]->msg);
            break;
        }
    }
    *resp = replies;
    return PAM_SUCCESS;
}

/* Общая часть обоих режимов: pam_start + item'ы окружения сессии. */
static int sdm_begin(struct sdm_auth *a, const char *service,
                     const char *user, const char *password,
                     const char *tty) {
    struct pam_conv conv = { sdm_conv, (void *)password };

    memset(a, 0, sizeof(*a));
    snprintf(a->username, sizeof(a->username), "%s", user);

    int r = pam_start(service, user, &conv, &a->pamh);
    if (r != PAM_SUCCESS)
        return r;

    /* PAM_TTY нужен pam_systemd/pam_securetty, XDG_SEAT — logind'у */
    pam_set_item(a->pamh, PAM_TTY, tty);
    pam_putenv(a->pamh, "XDG_SEAT=seat0");
    pam_putenv(a->pamh, "XDG_SESSION_CLASS=user");
    pam_putenv(a->pamh, "XDG_SESSION_TYPE=wayland");
    pam_putenv(a->pamh, "XDG_SESSION_DESKTOP=shidikde");
    return PAM_SUCCESS;
}

int sdm_authenticate(struct sdm_auth *a, const char *user,
                     const char *password, const char *tty) {
    int r = sdm_begin(a, SDM_PAM_SERVICE, user, password, tty);
    if (r != PAM_SUCCESS)
        return r;

    r = pam_authenticate(a->pamh, 0);          /* проверка пароля */
    if (r == PAM_SUCCESS)
        r = pam_acct_mgmt(a->pamh, 0);          /* аккаунт не заблокирован? */

    if (r != PAM_SUCCESS) {
        pam_end(a->pamh, r);
        a->pamh = NULL;
    }
    return r;
}

int sdm_autologin(struct sdm_auth *a, const char *user, const char *tty) {
    /* Пароль не спрашиваем: в сервисе shidikdm-autologin стадия auth —
     * pam_permit. Аккаунт всё равно проверяем (не истёк, не заблокирован). */
    int r = sdm_begin(a, SDM_PAM_AUTOLOGIN_SERVICE, user, "", tty);
    if (r != PAM_SUCCESS)
        return r;

    r = pam_acct_mgmt(a->pamh, 0);
    if (r != PAM_SUCCESS) {
        pam_end(a->pamh, r);
        a->pamh = NULL;
    }
    return r;
}

int sdm_open_session(struct sdm_auth *a) {
    int r = pam_setcred(a->pamh, PAM_ESTABLISH_CRED);
    if (r != PAM_SUCCESS)
        return r;

    r = pam_open_session(a->pamh, 0);
    if (r != PAM_SUCCESS)
        pam_setcred(a->pamh, PAM_DELETE_CRED);
    return r;
}

void sdm_end(struct sdm_auth *a) {
    if (!a->pamh)
        return;
    pam_close_session(a->pamh, 0);
    pam_setcred(a->pamh, PAM_DELETE_CRED);
    pam_end(a->pamh, PAM_SUCCESS);
    a->pamh = NULL;
}
