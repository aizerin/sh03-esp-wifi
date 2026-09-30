#include <stddef.h>
/* Small freestanding runtime required by aggregate initialization in control.c. */
void *memset(void *dst,int value,size_t length)
{
    unsigned char *p=dst;
    for(size_t i=0;i<length;i++)p[i]=(unsigned char)value;
    return dst;
}
void *memcpy(void *destination,const void *source,size_t length)
{
    unsigned char *out=destination;const unsigned char *in=source;
    for (size_t i=0;i<length;++i) out[i]=in[i];
    return destination;
}
void *memmove(void *destination,const void *source,size_t length)
{
    unsigned char *out=destination;const unsigned char *in=source;
    if (out<in) for (size_t i=0;i<length;++i) out[i]=in[i];
    else while (length) { --length;out[length]=in[length]; }
    return destination;
}
size_t strlen(const char *text) { size_t n=0;while (text[n]) ++n;return n; }
int strcmp(const char *a,const char *b)
{
    while (*a && *a==*b) { ++a;++b; }
    return (unsigned char)*a-(unsigned char)*b;
}
