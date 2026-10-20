#ifndef AQ_APPLETS
#define AQ_APPLETS

int cmd_clear(int, char**);
int cmd_getty(int, char**);
int cmd_login(int, char**);
int cmd_mount(int, char**);
int cmd_poweroff(int, char**);
int cmd_ps(int, char**);
int cmd_readmbr(int, char**);
int cmd_stat(int, char**);
int cmd_reboot(int, char**);
int cmd_truncate(int, char**);
int cmd_vmstat(int, char**);

#define APPLET(name) {#name, cmd_##name}

struct applet {
    char *name;
    int (*f)(int, char **);
} applets[] = {
    APPLET(clear),
    APPLET(getty),
    APPLET(login),
    APPLET(mount),
    APPLET(poweroff),
    APPLET(ps),
    APPLET(readmbr),
    APPLET(reboot),
    APPLET(stat),
    APPLET(truncate),
    APPLET(vmstat),
};

#define APPLETS_NR (sizeof(applets)/sizeof(*applets))

#endif /* ! AQ_APPLETS */
