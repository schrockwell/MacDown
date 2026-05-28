#include "pstr.h"

void PStrCopy(StringPtr dst, ConstStr255Param src)
{
    short i;
    dst[0] = src[0];
    for (i = 1; i <= src[0]; i++) dst[i] = src[i];
}

Boolean PStrEq(ConstStr255Param a, ConstStr255Param b)
{
    short i;
    if (a[0] != b[0]) return false;
    for (i = 1; i <= a[0]; i++) if (a[i] != b[i]) return false;
    return true;
}
