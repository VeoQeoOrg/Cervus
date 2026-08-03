#ifndef _CERVUS_FTP_H
#define _CERVUS_FTP_H

#include <stdint.h>

typedef struct {
    int      ctl;
    int      verbose;
    uint32_t serv_ip;
    unsigned char rb[1024];
    int      roff, rlen;
    char     reply[512];
    int      code;
} ftp_session;

int  ftp_connect(ftp_session *s, const char *host, int port, int verbose);
int  ftp_login(ftp_session *s, const char *user, const char *pass);
int  ftp_type(ftp_session *s, int binary);
int  ftp_pasv_open(ftp_session *s);
int  ftp_retr(ftp_session *s, const char *path, int out_fd);
int  ftp_stor(ftp_session *s, const char *path, int in_fd);
int  ftp_list(ftp_session *s, const char *path, int out_fd);
int  ftp_cwd(ftp_session *s, const char *dir);
int  ftp_pwd(ftp_session *s, char *out, int cap);
int  ftp_cmd(ftp_session *s, const char *verb, const char *arg);
void ftp_quit(ftp_session *s);

int  ftp_fetch(const char *url, int out_fd, int verbose);

#endif
