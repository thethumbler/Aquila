#ifndef AQ_APPLETS
#define AQ_APPLETS

int cmd_cat(int, char**);
int cmd_clear(int, char**);
int cmd_echo(int, char**);
int cmd_env(int, char**);
int cmd_ls(int, char**);
int cmd_mount(int, char**);
int cmd_pwd(int, char**);
int cmd_screenfetch(int, char**);
int cmd_sh(int, char**);
int cmd_stat(int, char**);
int cmd_uname(int, char**);

#define APPLET(name) {#name, cmd_##name}

struct applet {
    char *name;
    int (*f)(int, char **);
} applets[] = {
    APPLET(cat),
    APPLET(clear),
    APPLET(echo),
    APPLET(env),
    APPLET(ls),
    APPLET(mount),
    APPLET(pwd),
    APPLET(screenfetch),
    APPLET(sh),
    APPLET(stat),
    APPLET(uname),
};

#define APPLETS_NR (sizeof(applets)/sizeof(*applets))

#endif /* ! AQ_APPLETS */
