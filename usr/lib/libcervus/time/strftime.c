#include <time.h>
#include <stddef.h>

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
    if (!s || !fmt || !tm || max == 0) return 0;
    size_t i = 0;
    while (*fmt && i + 1 < max) {
        if (*fmt != '%') { s[i++] = *fmt++; continue; }
        fmt++;
        char tmp[16];
        int n = 0;
        switch (*fmt) {
            case 'Y': {
                int y = tm->tm_year + 1900;
                n = 4;
                tmp[0] = (char)('0' + (y/1000)%10);
                tmp[1] = (char)('0' + (y/100)%10);
                tmp[2] = (char)('0' + (y/10)%10);
                tmp[3] = (char)('0' + y%10);
                break;
            }
            case 'm': { int v=tm->tm_mon+1; tmp[0]=(char)('0'+v/10); tmp[1]=(char)('0'+v%10); n=2; break; }
            case 'd': { int v=tm->tm_mday;  tmp[0]=(char)('0'+v/10); tmp[1]=(char)('0'+v%10); n=2; break; }
            case 'H': { int v=tm->tm_hour;  tmp[0]=(char)('0'+v/10); tmp[1]=(char)('0'+v%10); n=2; break; }
            case 'M': { int v=tm->tm_min;   tmp[0]=(char)('0'+v/10); tmp[1]=(char)('0'+v%10); n=2; break; }
            case 'S': { int v=tm->tm_sec;   tmp[0]=(char)('0'+v/10); tmp[1]=(char)('0'+v%10); n=2; break; }
            case '%': tmp[0]='%'; n=1; break;
            default:  tmp[0]='%'; tmp[1]=*fmt; n = (*fmt ? 2 : 1); break;
        }
        for (int k = 0; k < n && i + 1 < max; k++) s[i++] = tmp[k];
        if (*fmt) fmt++;
    }
    s[i] = '\0';
    return i;
}
