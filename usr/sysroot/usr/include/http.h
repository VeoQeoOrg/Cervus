#ifndef _CERVUS_HTTP_H
#define _CERVUS_HTTP_H

#define HTTP_MAX_HEADERS 32

typedef struct {
    const char *method;
    const char *data;
    long        data_len;
    const char *content_type;
    const char *user_agent;
    const char *referer;
    const char *userpwd;
    const char *range;
    const char *headers[HTTP_MAX_HEADERS];
    int         nheaders;
    int         insecure;
    int         head_only;
    int         include_headers;
    int         header_fd;
    int         follow;
    int         max_redirs;
    int         verbose;
    int         fail_on_error;
    int         silent;
    int        *out_status;
} http_opts;

int http_request(const char *url, int out_fd, const http_opts *opts);
int http_fetch(const char *url, int out_fd, int insecure, int head_only, int verbose);

#endif
