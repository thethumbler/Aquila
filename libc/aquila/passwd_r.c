/*
 * getpwnam_r() and getpwuid_r(): newlib's getpwnam() and getpwuid(), the
 * entry copied into the caller's buffer.
 */

#include <errno.h>
#include <pwd.h>
#include <string.h>

/* `s' at *p in the buffer, NULL if it doesn't fit */
static char *copy(char **p, char *end, const char *s)
{
    size_t len = strlen(s ? s : "") + 1;
    char *dst = *p;

    if ((size_t) (end - *p) < len)
        return NULL;

    memcpy(dst, s ? s : "", len);
    *p += len;
    return dst;
}

static int passwd_copy(struct passwd *pw, struct passwd *pwd, char *buf,
        size_t size, struct passwd **result)
{
    char *p = buf, *end = buf + size;

    *result = NULL;

    if (!pw)
        return 0;   /* not found: no error */

    *pwd = *pw;

    if (!(pwd->pw_name    = copy(&p, end, pw->pw_name)) ||
        !(pwd->pw_passwd  = copy(&p, end, pw->pw_passwd)) ||
        !(pwd->pw_comment = copy(&p, end, pw->pw_comment)) ||
        !(pwd->pw_gecos   = copy(&p, end, pw->pw_gecos)) ||
        !(pwd->pw_dir     = copy(&p, end, pw->pw_dir)) ||
        !(pwd->pw_shell   = copy(&p, end, pw->pw_shell)))
        return ERANGE;

    *result = pwd;
    return 0;
}

int getpwnam_r(const char *name, struct passwd *pwd, char *buf, size_t size,
        struct passwd **result)
{
    return passwd_copy(getpwnam(name), pwd, buf, size, result);
}

int getpwuid_r(uid_t uid, struct passwd *pwd, char *buf, size_t size,
        struct passwd **result)
{
    return passwd_copy(getpwuid(uid), pwd, buf, size, result);
}
