#ifndef _ARPA_TELNET_H
#define _ARPA_TELNET_H

/* the telnet protocol's commands (RFC 854) and a few options */

#define IAC     255     /* interpret as command */
#define DONT    254
#define DO      253
#define WONT    252
#define WILL    251
#define SB      250     /* subnegotiation */
#define GA      249
#define EL      248
#define EC      247
#define AYT     246
#define AO      245
#define IP      244
#define BREAK   243
#define DM      242
#define NOP     241
#define SE      240
#define EOR     239

#define TELOPT_BINARY   0
#define TELOPT_ECHO     1
#define TELOPT_SGA      3
#define TELOPT_STATUS   5
#define TELOPT_TM       6
#define TELOPT_TTYPE    24
#define TELOPT_NAWS     31
#define TELOPT_LINEMODE 34

#endif /* ! _ARPA_TELNET_H */
