#ifndef _CONFIG_H
#define _CONFIG_H

/* Defaults */
#define DEFAULT_WALLPAPER  "/etc/wallpaper.jpg"
#define DEFAULT_FONT       "/etc/font.tf"
#define DEFAULT_SHELL      "/bin/aqbox"

#define FB_PATH            "/dev/fb0"
#define KBD_PATH           "/dev/kbd"

int debug(int level, const char *fmt, ...);

enum {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

#endif /* !_CONFIG_H */
