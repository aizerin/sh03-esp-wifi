/* The application uses %s and %d only. A bounded freestanding formatter avoids
 * carrying the vendor's unrelated floating-point printf runtime into the image.
 * Output is capped at 255 bytes, as in the original 256-byte local buffer. */
#include <stdarg.h>
#include "sh03_runtime.h"

int sh03_debug_printf(const char *format,...)
{
    char buffer[256];unsigned length=0;
    va_list args;va_start(args,format);
    while (*format && length<255) {
        if (*format!='%') { buffer[length++]=*format++;continue; }
        ++format;
        if (*format=='s') {
            const char *text=va_arg(args,const char *);
            while (*text && length<255) buffer[length++]=*text++;
        } else if (*format=='d') {
            int32_t value=va_arg(args,int32_t);
            uint32_t magnitude=value<0?0u-(uint32_t)value:(uint32_t)value;
            char digits[10];unsigned count=0;
            do { digits[count++]='0'+magnitude%10;magnitude/=10; } while (magnitude);
            if (value<0 && length<255) buffer[length++]='-';
            while (count && length<255) buffer[length++]=digits[--count];
        } else if (*format=='%') buffer[length++]='%';
        else { buffer[length++]='%';if (*format && length<255) buffer[length++]=*format; }
        if (*format) ++format;
    }
    va_end(args);buffer[length]=0;
    SH03_FN(0x0800ae48,void,const char *)(buffer);
    return (int)length;
}
