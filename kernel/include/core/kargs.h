#ifndef _KARGS_H
#define _KARGS_H

int kargs_parse(const char *_cmdline);
int kargs_get(const char *key, const char **value);

#endif /* ! _KARGS_H */
