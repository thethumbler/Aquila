#ifndef _CRYPT_H
#define _CRYPT_H

/* only SHA-512 ("$6$") hashes are supported */
char *crypt(const char *key, const char *setting);

#endif /* ! _CRYPT_H */
