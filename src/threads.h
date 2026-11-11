#ifndef HOME_THREADS_H
#define HOME_THREADS_H

/* Stack for any thread that runs curl + OpenSSL (a TLS handshake, certificate
 * checks, redirects). 32 KB was too tight for some of them; an overflow
 * corrupts whatever sits next to the stack and shows up later as a crash
 * somewhere else entirely (the "end frame" reports of 1.8.4). 128 KB each
 * costs under 1.5 MB in all. */
#define NET_STACK 0x20000

#endif
