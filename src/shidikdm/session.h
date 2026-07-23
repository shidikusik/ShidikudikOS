/* ShidikDM — запуск пользовательской сессии. */
#ifndef SHIDIKDM_SESSION_H
#define SHIDIKDM_SESSION_H

#include "auth.h"

/* Форкается, сбрасывает привилегии до пользователя и запускает команду
 * сессии (по умолчанию /usr/local/bin/shidikde-session). Блокируется до
 * завершения сессии. Возвращает exit-код сессии или -1 при ошибке. */
int sdm_run_session(struct sdm_auth *a, const char *session_cmd);

#endif
