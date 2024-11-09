#ifndef MD5_H
#define MD5_H
/*
 * LIC: GPL
 */

#include "config.h"

#ifdef HAVE_STDINT_H
#include <stdint.h>
typedef uint32_t uint32;
#else
#if SIZEOF_UNSIGNED_INT == 4
typedef unsigned int uint32;
#elif SIZEOF_UNSIGNED_LONG == 4
typedef unsigned long uint32;
#else
# error Could not find a 32-bit integer type
#endif
#endif

struct MD5Context {
        uint32 buf[4];
        uint32 bits[2];
        unsigned char in[64];
};

void MD5Init(struct MD5Context *context);
void MD5Update(struct MD5Context *context, unsigned char const *buf,
               unsigned len);
void MD5Final(unsigned char digest[16], struct MD5Context *context);
void MD5Transform(uint32 buf[4], uint32 const in[16]);

#endif /* !MD5_H */
