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
int cmd_screenfetch(int, char**);
int cmd_sh(int, char**);
int cmd_stat(int, char**);
int cmd_uname(int, char**);
int cmd_unlink(int, char**);

#define APPLET(name) {#name, cmd_##name}

struct applet {
    char *name;
    int (*f)(int, char **);
} applets[] = {
    APPLET(cat),
    APPLET(clear),
    APPLET(echo),
    APPLET(env),
    APPLET(getty),
    APPLET(login),
    APPLET(ls),
    APPLET(mkdir),
    APPLET(mknod),
    APPLET(mount),
    APPLET(ps),
    APPLET(pwd),
    APPLET(readmbr),
    APPLET(screenfetch),
    APPLET(sh),
    APPLET(stat),
    APPLET(uname),
    APPLET(unlink),
};

#define APPLETS_NR (sizeof(applets)/sizeof(*applets))

#endif /* ! AQ_APPLETS */
