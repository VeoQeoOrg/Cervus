#ifndef _CERVUS_HTTP_H
#define _CERVUS_HTTP_H

int http_fetch(const char *url, int out_fd, int insecure, int head_only, int verbose);

#endif
