#ifndef AQ_APPLETS
#define AQ_APPLETS

int cmd_cat(int, char**);
int cmd_clear(int, char**);
int cmd_echo(int, char**);
int cmd_env(int, char**);
int cmd_getty(int, char**);
int cmd_login(int, char**);
int cmd_ls(int, char**);
int cmd_mkdir(int, char**);
int cmd_mknod(int, char**);
int cmd_mount(int, char**);
int cmd_ps(int, char**);
int cmd_pwd(int, char**);
int cmd_readmbr(int, char**);
int cmd_sh(int, char**);
int cmd_stat(int, char**);
int cmd_touch(int, char**);
int cmd_uname(int, char**);
int cmd_unlink(int, char**);
int cmd_kill(int, char**);
int cmd_reboot(int, char**);
int cmd_bim(int, char**);
int cmd_truncate(int, char**);
int cmd_mktemp(int, char**);
int cmd_vmstat(int, char**);
int cmd_date(int, char**);

#define APPLET(name) {#name, cmd_##name}

struct applet {
    char *name;
    int (*f)(int, char **);
} applets[] = {
    APPLET(bim),
    APPLET(cat),
    APPLET(clear),
    APPLET(date),
    APPLET(echo),
    APPLET(env),
    APPLET(getty),
    APPLET(kill),
    APPLET(login),
    APPLET(ls),
    APPLET(mkdir),
    APPLET(mknod),
    APPLET(mktemp),
    APPLET(mount),
    APPLET(ps),
    APPLET(pwd),
    APPLET(readmbr),
    APPLET(reboot),
    APPLET(sh),
    APPLET(stat),
    APPLET(touch),
    APPLET(truncate),
    APPLET(uname),
    APPLET(unlink),
    APPLET(vmstat),
};

#define APPLETS_NR (sizeof(applets)/sizeof(*applets))

#endif /* ! AQ_APPLETS */
