/*
 * /etc/group: name:password:gid:member,member,...
 */

#include <sys/types.h>
#include <errno.h>
#include <grp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MEMBERS 64

static FILE *group_file;
static char group_line[1024];
static char *group_members[MAX_MEMBERS + 1];
static struct group group_entry;

/* split `line' into `entry', the strings stay in line */
static int group_parse(char *line, struct group *entry, char **members, size_t nmembers)
{
    char *fields[4];
    char *p = line;

    line[strcspn(line, "\n")] = '\0';

    for (int i = 0; i < 4; ++i) {
        fields[i] = p;
        p = strchr(p, ':');

        if (!p && i < 3)
            return -1;

        if (p)
            *p++ = '\0';
    }

    char *end;
    entry->gr_name   = fields[0];
    entry->gr_passwd = fields[1];
    entry->gr_gid    = strtoul(fields[2], &end, 10);

    if (!*fields[2] || *end)
        return -1;

    size_t n = 0;
    for (char *m = fields[3]; *m && n < nmembers; ) {
        members[n++] = m;
        m += strcspn(m, ",");

        if (*m)
            *m++ = '\0';
    }

    members[n] = NULL;
    entry->gr_mem = members;
    return 0;
}

void setgrent(void)
{
    if (group_file)
        rewind(group_file);
    else
        group_file = fopen("/etc/group", "r");
}

void endgrent(void)
{
    if (group_file)
        fclose(group_file);

    group_file = NULL;
}

struct group *getgrent(void)
{
    if (!group_file)
        setgrent();

    if (!group_file)
        return NULL;

    while (fgets(group_line, sizeof(group_line), group_file)) {
        if (!group_parse(group_line, &group_entry, group_members, MAX_MEMBERS))
            return &group_entry;
    }

    return NULL;
}

/* the first entry for which match() is true */
static struct group *group_find(int (*match)(struct group *, const void *), const void *key)
{
    struct group *entry;

    setgrent();

    while ((entry = getgrent()) && !match(entry, key))
        ;

    endgrent();
    return entry;
}

static int match_name(struct group *entry, const void *name)
{
    return !strcmp(entry->gr_name, name);
}

static int match_gid(struct group *entry, const void *gid)
{
    return entry->gr_gid == *(const gid_t *) gid;
}

struct group *getgrnam(const char *name)
{
    return group_find(match_name, name);
}

struct group *getgrgid(gid_t gid)
{
    return group_find(match_gid, &gid);
}

/* copy `entry' into `buf' for the *_r() functions */
static int group_copy(struct group *entry, struct group *grp, char *buf, size_t size, struct group **result)
{
    size_t nmem = 0;

    *result = NULL;

    if (!entry)
        return 0;

    while (entry->gr_mem[nmem])
        ++nmem;

    /* the member pointers go first, aligned, then the strings */
    size_t ptrs = (nmem + 1) * sizeof(char *);
    size_t pad = -(uintptr_t) buf & (sizeof(char *) - 1);

    if (size < pad + ptrs)
        return ERANGE;

    char **mem = (char **) (buf + pad);
    char *p = buf + pad + ptrs;
    char *end = buf + size;

#define COPY(dst, src) do { \
        size_t len = strlen(src) + 1; \
        if ((size_t) (end - p) < len) return ERANGE; \
        dst = memcpy(p, src, len); \
        p += len; \
    } while (0)

    COPY(grp->gr_name, entry->gr_name);
    COPY(grp->gr_passwd, entry->gr_passwd);

    for (size_t i = 0; i < nmem; ++i)
        COPY(mem[i], entry->gr_mem[i]);

#undef COPY

    mem[nmem] = NULL;
    grp->gr_gid = entry->gr_gid;
    grp->gr_mem = mem;

    *result = grp;
    return 0;
}

int getgrnam_r(const char *name, struct group *grp, char *buf, size_t size, struct group **result)
{
    return group_copy(getgrnam(name), grp, buf, size, result);
}

int getgrgid_r(gid_t gid, struct group *grp, char *buf, size_t size, struct group **result)
{
    return group_copy(getgrgid(gid), grp, buf, size, result);
}
