/* /etc/shadow access */

#include <shadow.h>
#include <stdlib.h>
#include <string.h>

static FILE *shadow;
static char line[1024];
static struct spwd entry;

/* parse a numeric field, empty means -1 */
static long field(char *s)
{
    return *s ? strtol(s, NULL, 10) : -1;
}

/* split "name:hash:lstchg:min:max:warn:inact:expire:flag" in place */
static struct spwd *parse(char *s)
{
    char *f[9];
    int n = 0;

    s[strcspn(s, "\n")] = '\0';

    for (f[n++] = s; *s && n < 9; ++s) {
        if (*s == ':') {
            *s = '\0';
            f[n++] = s + 1;
        }
    }

    if (n < 2)
        return NULL;

    while (n < 9)
        f[n++] = "";

    entry.sp_namp   = f[0];
    entry.sp_pwdp   = f[1];
    entry.sp_lstchg = field(f[2]);
    entry.sp_min    = field(f[3]);
    entry.sp_max    = field(f[4]);
    entry.sp_warn   = field(f[5]);
    entry.sp_inact  = field(f[6]);
    entry.sp_expire = field(f[7]);
    entry.sp_flag   = *f[8] ? strtoul(f[8], NULL, 10) : 0;

    return &entry;
}

void setspent(void)
{
    if (shadow)
        rewind(shadow);
}

void endspent(void)
{
    if (shadow) {
        fclose(shadow);
        shadow = NULL;
    }
}

struct spwd *getspent(void)
{
    if (!shadow && !(shadow = fopen(SHADOW, "r")))
        return NULL;

    while (fgets(line, sizeof(line), shadow)) {
        struct spwd *sp = parse(line);
        if (sp)
            return sp;
    }

    return NULL;
}

struct spwd *getspnam(const char *name)
{
    struct spwd *sp;

    setspent();

    while ((sp = getspent())) {
        if (!strcmp(sp->sp_namp, name))
            break;
    }

    endspent();
    return sp;
}
