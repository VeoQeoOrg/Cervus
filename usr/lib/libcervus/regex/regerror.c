#include <regex.h>
#include <string.h>
#include <stddef.h>

size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t buflen) {
    (void)preg;
    const char *msg = NULL;
    switch (errcode) {
        case 0:            msg = "No error"; break;
        case REG_NOMATCH:  msg = "No match"; break;
        case REG_BADPAT:   msg = "Invalid regular expression"; break;
        case REG_ECOLLATE: msg = "Invalid collation element"; break;
        case REG_ECTYPE:   msg = "Invalid character class"; break;
        case REG_EESCAPE:  msg = "Trailing backslash"; break;
        case REG_ESUBREG:  msg = "Invalid back reference"; break;
        case REG_EBRACK:   msg = "Unmatched [, [^, [:, [., or [="; break;
        case REG_EPAREN:   msg = "Unmatched ( or \\("; break;
        case REG_EBRACE:   msg = "Unmatched \\{"; break;
        case REG_BADBR:    msg = "Invalid content of \\{\\}"; break;
        case REG_ERANGE:   msg = "Invalid range end"; break;
        case REG_ESPACE:   msg = "Out of memory"; break;
        case REG_BADRPT:   msg = "Repetition not preceded by valid expression"; break;
        default:           msg = "Unknown regex error"; break;
    }
    size_t len = strlen(msg) + 1;
    if (errbuf && buflen > 0) {
        size_t n = (len > buflen) ? buflen - 1 : len - 1;
        memcpy(errbuf, msg, n);
        errbuf[n] = '\0';
    }
    return len;
}
