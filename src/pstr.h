#ifndef MacDown_PSTR_H
#define MacDown_PSTR_H

#include <Types.h>

/* Pascal-string helpers. byte 0 = length, bytes 1..len = chars. */

/* dst := src.  dst must have room for src[0] + 1 bytes. */
void PStrCopy(StringPtr dst, ConstStr255Param src);

/* True iff a and b have the same length and byte contents. */
Boolean PStrEq(ConstStr255Param a, ConstStr255Param b);

#endif
