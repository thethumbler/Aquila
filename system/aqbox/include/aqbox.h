#ifndef _AQBOX_H
#define _AQBOX_H

#ifndef _AQ_STANDALONE
#define AQBOX_APPLET(n) int cmd_##n
#else
#define AQBOX_APPLET(n) int main
#endif

int (*aqbox_get_applet(char *name))();

#endif /* ! _AQBOX_H */
