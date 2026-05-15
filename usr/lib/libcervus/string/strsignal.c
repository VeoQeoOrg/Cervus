#include <string.h>

char *strsignal(int sig)
{
    switch (sig) {
        case 1:  return "Hangup";
        case 2:  return "Interrupt";
        case 3:  return "Quit";
        case 4:  return "Illegal instruction";
        case 6:  return "Aborted";
        case 8:  return "Floating point exception";
        case 9:  return "Killed";
        case 11: return "Segmentation fault";
        case 13: return "Broken pipe";
        case 14: return "Alarm clock";
        case 15: return "Terminated";
        default: return "Unknown signal";
    }
}
