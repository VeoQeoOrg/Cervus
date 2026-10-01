#include <langinfo.h>

static const char *const g_abday[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *const g_day[] = { "Sunday", "Monday", "Tuesday", "Wednesday",
                                     "Thursday", "Friday", "Saturday" };
static const char *const g_abmon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char *const g_mon[] = { "January", "February", "March", "April", "May", "June",
                                     "July", "August", "September", "October", "November",
                                     "December" };

char *nl_langinfo(nl_item item)
{
    const char *r = "";
    if (item >= ABDAY_1 && item <= ABDAY_7) r = g_abday[item - ABDAY_1];
    else if (item >= DAY_1 && item <= DAY_7) r = g_day[item - DAY_1];
    else if (item >= ABMON_1 && item <= ABMON_12) r = g_abmon[item - ABMON_1];
    else if (item >= MON_1 && item <= MON_12) r = g_mon[item - MON_1];
    else switch (item) {
    case CODESET:    r = "UTF-8"; break;
    case D_T_FMT:    r = "%a %b %e %H:%M:%S %Y"; break;
    case D_FMT:      r = "%m/%d/%y"; break;
    case T_FMT:      r = "%H:%M:%S"; break;
    case T_FMT_AMPM: r = "%I:%M:%S %p"; break;
    case AM_STR:     r = "AM"; break;
    case PM_STR:     r = "PM"; break;
    case RADIXCHAR:  r = "."; break;
    case YESEXPR:    r = "^[yY]"; break;
    case NOEXPR:     r = "^[nN]"; break;
    case CRNCYSTR:   r = "-"; break;
    default: break;
    }
    return (char *)r;
}
