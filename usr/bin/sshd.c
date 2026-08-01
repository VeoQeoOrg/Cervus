#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <crypto.h>
#include <pwutil.h>

extern char **environ;

#ifdef SSHD_HOST_TEST
static long syscall2(long n, unsigned long a, unsigned long b){(void)n;(void)a;(void)b;return 0;}
#ifndef SYS_AUTH
#define SYS_AUTH 570
#endif
void crypto_random(void *b, size_t n){ FILE*f=fopen("/dev/urandom","rb"); if(f){size_t r=fread(b,1,n,f);(void)r;fclose(f);} }
#endif

#define RXSZ 40000
#define MAXPKT 20000
#define SBUF 17000
#define HOSTKEY_PATH "/etc/ssh_host_ed25519.key"

enum {
    MSG_DISCONNECT=1, MSG_IGNORE=2, MSG_DEBUG=4,
    MSG_SERVICE_REQUEST=5, MSG_SERVICE_ACCEPT=6,
    MSG_KEXINIT=20, MSG_NEWKEYS=21,
    MSG_KEX_ECDH_INIT=30, MSG_KEX_ECDH_REPLY=31,
    MSG_USERAUTH_REQUEST=50, MSG_USERAUTH_FAILURE=51, MSG_USERAUTH_SUCCESS=52,
    MSG_GLOBAL_REQUEST=80, MSG_REQUEST_FAILURE=82,
    MSG_CHANNEL_OPEN=90, MSG_CHANNEL_OPEN_CONFIRMATION=91,
    MSG_CHANNEL_WINDOW_ADJUST=93, MSG_CHANNEL_DATA=94,
    MSG_CHANNEL_EOF=96, MSG_CHANNEL_CLOSE=97, MSG_CHANNEL_REQUEST=98,
    MSG_CHANNEL_SUCCESS=99, MSG_CHANNEL_FAILURE=100
};

typedef struct {
    int fd;
    uint32_t send_seq, recv_seq;
    int enc_send, enc_recv;
    uint8_t key_c2s[64], key_s2c[64];
    uint8_t session_id[32];
    uint8_t rx[RXSZ]; size_t head, tail;
    uint8_t pkt[MAXPKT]; size_t pkt_len;
    uint8_t sbody[SBUF], senc[SBUF];
    char err[160];
} ssh_t;

static void wr_u32(uint8_t *p, uint32_t v){ p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v; }
static uint32_t rd_u32(const uint8_t *p){ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void p8(uint8_t *b,size_t *i,uint8_t v){ b[(*i)++]=v; }
static void p32(uint8_t *b,size_t *i,uint32_t v){ wr_u32(b+*i,v); *i+=4; }
static void pbytes(uint8_t *b,size_t *i,const void *d,size_t n){ memcpy(b+*i,d,n); *i+=n; }
static void pstr(uint8_t *b,size_t *i,const void *d,size_t n){ p32(b,i,(uint32_t)n); pbytes(b,i,d,n); }
static void pcstr(uint8_t *b,size_t *i,const char *s){ pstr(b,i,s,strlen(s)); }

static void chacha20_ssh(const uint8_t key[32], uint64_t seq, uint32_t counter,
                         const uint8_t *in, uint8_t *out, size_t len) {
    uint8_t nonce[12];
    nonce[0]=nonce[1]=nonce[2]=nonce[3]=0;
    nonce[4]=(uint8_t)(seq>>56); nonce[5]=(uint8_t)(seq>>48); nonce[6]=(uint8_t)(seq>>40); nonce[7]=(uint8_t)(seq>>32);
    nonce[8]=(uint8_t)(seq>>24); nonce[9]=(uint8_t)(seq>>16); nonce[10]=(uint8_t)(seq>>8); nonce[11]=(uint8_t)seq;
    chacha20(key, nonce, counter, in, out, len);
}

static int io_write_full(int fd, const uint8_t *b, size_t n) {
    size_t off=0; int spin=0;
    while (off<n) { long r=send(fd,b+off,n-off,0); if(r>0){off+=r;spin=0;} else { if(++spin>200000) return -1; usleep(200);} }
    return 0;
}

static int ssh_send(ssh_t *s, const uint8_t *payload, size_t plen) {
    size_t bs=8, padlen;
    if (s->enc_send) padlen = bs - ((1+plen)%bs); else padlen = bs - ((5+plen)%bs);
    if (padlen<4) padlen+=bs;
    uint32_t length=(uint32_t)(1+plen+padlen);
    if (length+4>SBUF) return -1;
    size_t bi=0; s->sbody[bi++]=(uint8_t)padlen;
    memcpy(s->sbody+bi,payload,plen); bi+=plen;
    crypto_random(s->sbody+bi,padlen);
    if (!s->enc_send) {
        uint8_t hdr[4]; wr_u32(hdr,length);
        if (io_write_full(s->fd,hdr,4)) return -1;
        if (io_write_full(s->fd,s->sbody,length)) return -1;
    } else {
        const uint8_t *mk=s->key_s2c, *hk=s->key_s2c+32;
        uint8_t enclen[4], lenbytes[4]; wr_u32(lenbytes,length);
        chacha20_ssh(hk,s->send_seq,0,lenbytes,enclen,4);
        uint8_t polykey[32],zeros[32]; memset(zeros,0,32);
        chacha20_ssh(mk,s->send_seq,0,zeros,polykey,32);
        chacha20_ssh(mk,s->send_seq,1,s->sbody,s->senc,length);
        poly1305_ctx pc; poly1305_init(&pc,polykey);
        poly1305_update(&pc,enclen,4); poly1305_update(&pc,s->senc,length);
        uint8_t mac[16]; poly1305_final(&pc,mac);
        if (io_write_full(s->fd,enclen,4)) return -1;
        if (io_write_full(s->fd,s->senc,length)) return -1;
        if (io_write_full(s->fd,mac,16)) return -1;
    }
    s->send_seq++;
    return 0;
}

static int rx_pull(ssh_t *s, int blocking) {
    if (s->head>0) { memmove(s->rx,s->rx+s->head,s->tail-s->head); s->tail-=s->head; s->head=0; }
    if (s->tail>=RXSZ) return -1;
    long n=recv(s->fd,s->rx+s->tail,RXSZ-s->tail,0);
    if (n>0){ s->tail+=n; return (int)n; }
    if (n==0) return -1;
    return blocking?-1:0;
}

static int ssh_recv(ssh_t *s, int blocking) {
    for (;;) {
        size_t avail=s->tail-s->head;
        if (avail>=4) {
            uint32_t length;
            if (s->enc_recv){ uint8_t dl[4]; chacha20_ssh(s->key_c2s+32,s->recv_seq,0,s->rx+s->head,dl,4); length=rd_u32(dl); }
            else length=rd_u32(s->rx+s->head);
            if (length<1||length>MAXPKT-64) return -2;
            size_t total = s->enc_recv?(4+length+16):(4+length);
            if (avail>=total) {
                if (s->enc_recv) {
                    uint8_t polykey[32],zeros[32]; memset(zeros,0,32);
                    chacha20_ssh(s->key_c2s,s->recv_seq,0,zeros,polykey,32);
                    poly1305_ctx pc; poly1305_init(&pc,polykey);
                    poly1305_update(&pc,s->rx+s->head,4);
                    poly1305_update(&pc,s->rx+s->head+4,length);
                    uint8_t mac[16]; poly1305_final(&pc,mac);
                    if (memcmp(mac,s->rx+s->head+4+length,16)!=0) return -3;
                    chacha20_ssh(s->key_c2s,s->recv_seq,1,s->rx+s->head+4,s->pkt,length);
                } else memcpy(s->pkt,s->rx+s->head+4,length);
                s->head+=total; s->recv_seq++;
                uint8_t padlen=s->pkt[0];
                if ((size_t)padlen+1>length) return -2;
                s->pkt_len=length-1-padlen;
                memmove(s->pkt,s->pkt+1,s->pkt_len);
                return 1;
            }
        }
        int n=rx_pull(s,blocking);
        if (n<0) return -1;
        if (n==0 && !blocking) return 0;
    }
}

static int wait_msg(ssh_t *s, int want) {
    for (;;) {
        if (ssh_recv(s,1)!=1) return -1;
        uint8_t t=s->pkt[0];
        if (t==MSG_IGNORE||t==MSG_DEBUG) continue;
        if (t==MSG_DISCONNECT) return -1;
        if (want>=0 && t!=want) return -1;
        return t;
    }
}

static void hash_str(sha256_ctx *c,const uint8_t *d,uint32_t n){ uint8_t l[4]; wr_u32(l,n); sha256_update(c,l,4); sha256_update(c,d,n); }

static int version_exchange(ssh_t *s, char *peer, size_t cap) {
    const char *v="SSH-2.0-Cervus_sshd_1.0\r\n";
    if (io_write_full(s->fd,(const uint8_t*)v,strlen(v))) return -1;
    for (;;) {
        size_t li=0;
        for (;;) {
            if (s->head>=s->tail){ if (rx_pull(s,1)<0) return -1; }
            uint8_t ch=s->rx[s->head++];
            if (ch=='\n') break;
            if (ch!='\r' && li<cap-1) peer[li++]=(char)ch;
        }
        peer[li]=0;
        if (li>=4 && !memcmp(peer,"SSH-",4)) break;
    }
    return 0;
}

static void derive_key(ssh_t *s, char letter, const uint8_t *kmp, size_t kl, const uint8_t H[32], uint8_t out[64]) {
    uint8_t k1[32],k2[32];
    sha256_ctx d; sha256_init(&d);
    sha256_update(&d,kmp,kl); sha256_update(&d,H,32);
    uint8_t c=(uint8_t)letter; sha256_update(&d,&c,1);
    sha256_update(&d,s->session_id,32); sha256_final(&d,k1);
    sha256_init(&d); sha256_update(&d,kmp,kl); sha256_update(&d,H,32);
    sha256_update(&d,k1,32); sha256_final(&d,k2);
    memcpy(out,k1,32); memcpy(out+32,k2,32);
}

static int b64v(int c){
    if (c>='A'&&c<='Z') return c-'A';
    if (c>='a'&&c<='z') return c-'a'+26;
    if (c>='0'&&c<='9') return c-'0'+52;
    if (c=='+') return 62;
    if (c=='/') return 63;
    return -1;
}

static int pubkey_authorized(const char *home, const uint8_t *blob, size_t bloblen) {
    char path[256];
    snprintf(path, sizeof path, "%s/.ssh/authorized_keys", home);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    static char buf[8192];
    int n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = 0;
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *sp = strchr(line, ' ');
        if (sp) {
            char *b = sp + 1;
            char *sp2 = strchr(b, ' ');
            if (sp2) *sp2 = 0;
            uint8_t dec[128]; int acc = 0, nb = 0, o = 0;
            for (char *p = b; *p; p++) {
                int v = b64v((unsigned char)*p);
                if (v < 0) continue;
                acc = (acc << 6) | v; nb += 6;
                if (nb >= 8) { nb -= 8; if (o < 128) dec[o++] = (uint8_t)((acc >> nb) & 0xff); }
            }
            if ((size_t)o == bloblen && memcmp(dec, blob, bloblen) == 0) return 1;
        }
        line = nl ? nl + 1 : 0;
    }
    return 0;
}

static int authenticate(ssh_t *s, char *user_out, uint32_t *uid_out, uint32_t *gid_out,
                        char *home_out, char *shell_out) {
    for (;;) {
        if (wait_msg(s, MSG_USERAUTH_REQUEST) < 0) return -1;
        size_t i=1;
        uint32_t ul=rd_u32(s->pkt+i); i+=4;
        char user[128]; if (ul>127) ul=127; memcpy(user,s->pkt+i,ul); user[ul]=0; i+=ul;
        uint32_t svl=rd_u32(s->pkt+i); i+=4+svl;
        uint32_t ml=rd_u32(s->pkt+i); i+=4;
        char method[32]; if (ml>31) ml=31; memcpy(method,s->pkt+i,ml); method[ml]=0; i+=rd_u32(s->pkt+i-4);

        int ok=0;
#ifdef SSHD_HOST_TEST
        if (!strcmp(method,"none")) {
            strcpy(user_out,user[0]?user:"tester"); *uid_out=0; *gid_out=0;
            strcpy(home_out,"."); strcpy(shell_out,"/bin/sh");
            uint8_t p=MSG_USERAUTH_SUCCESS; ssh_send(s,&p,1); return 0;
        }
#endif
        if (!strcmp(method,"password")) {
            i+=1;
            uint32_t pl=rd_u32(s->pkt+i); i+=4;
            char pass[256]; if (pl>255) pl=255; memcpy(pass,s->pkt+i,pl); pass[pl]=0;
            uint32_t uid=0,gid=0; char home[128]="/",shell[128]="/bin/csh";
            if (pw_lookup_name(user,&uid,&gid,home,sizeof home,shell,sizeof shell)==0) {
                if (syscall2(SYS_AUTH,(uint64_t)uid,(uint64_t)(uintptr_t)pass)==0) {
                    ok=1;
                    strcpy(user_out,user); *uid_out=uid; *gid_out=gid;
                    strcpy(home_out,home[0]?home:"/"); strcpy(shell_out,shell[0]?shell:"/bin/csh");
                }
            }
            memset(pass,0,sizeof pass);
        } else if (!strcmp(method,"publickey")) {
            uint8_t has_sig=s->pkt[i]; i+=1;
            uint32_t algl=rd_u32(s->pkt+i); i+=4+algl;
            uint32_t pkl=rd_u32(s->pkt+i); i+=4;
            const uint8_t *pkblob=s->pkt+i;
            size_t sig_off=i+pkl;
            i+=pkl;
            if (has_sig && pkl>=51) {
                uint32_t uid=0,gid=0; char home[128]="/",shell[128]="/bin/csh";
                if (pw_lookup_name(user,&uid,&gid,home,sizeof home,shell,sizeof shell)==0
                    && pubkey_authorized(home,pkblob,pkl)) {
                    const uint8_t *pub=pkblob+19;
                    const uint8_t *sblob=s->pkt+i+4;
                    const uint8_t *sig=sblob+19;
                    uint8_t signed_data[900]; size_t di=0;
                    wr_u32(signed_data,32); memcpy(signed_data+4,s->session_id,32); di=36;
                    if (sig_off<sizeof signed_data-di) { memcpy(signed_data+di,s->pkt,sig_off); di+=sig_off;
                        if (ed25519_verify(sig,signed_data,di,pub)==0) {
                            ok=1; strcpy(user_out,user); *uid_out=uid; *gid_out=gid;
                            strcpy(home_out,home[0]?home:"/"); strcpy(shell_out,shell[0]?shell:"/bin/csh");
                        }
                    }
                }
            }
        }
        if (ok) { uint8_t p=MSG_USERAUTH_SUCCESS; ssh_send(s,&p,1); return 0; }
        uint8_t p[64]; size_t pi=0;
        p8(p,&pi,MSG_USERAUTH_FAILURE);
        pcstr(p,&pi,"password");
        p8(p,&pi,0);
        ssh_send(s,p,pi);
    }
}

static int run_shell(ssh_t *s, uint32_t client_chan, uint32_t cli_window,
                     uint32_t uid, uint32_t gid, const char *home, const char *shell, const char *cmd) {
    int inpipe[2], outpipe[2];
    if (pipe(inpipe) || pipe(outpipe)) return -1;
    pid_t pid = fork();
    if (pid == 0) {
        dup2(inpipe[0],0); dup2(outpipe[1],1); dup2(outpipe[1],2);
        close(inpipe[0]); close(inpipe[1]); close(outpipe[0]); close(outpipe[1]);
        close(s->fd);
        setsid();
        if (gid) setgid(gid);
        if (uid) setuid(uid);
        if (home[0]) chdir(home);
        setenv("HOME", home[0]?home:"/", 1);
        setenv("SHELL", shell, 1);
        setenv("TERM", "xterm", 1);
        if (cmd) { char *av[]={(char*)shell,"-c",(char*)cmd,NULL}; execve(shell,av,environ); }
        else { char *av[]={(char*)shell,NULL}; execve(shell,av,environ); }
        _exit(127);
    }
    close(inpipe[0]); close(outpipe[1]);
    int inw=inpipe[1], outr=outpipe[0];

    long fl=fcntl(s->fd,F_GETFL,0); fcntl(s->fd,F_SETFL,fl|O_NONBLOCK);
    fl=fcntl(outr,F_GETFL,0); fcntl(outr,F_SETFL,fl|O_NONBLOCK);

    uint32_t local_window=2000000;
    int exit_status=0, done=0;
    while (!done) {
        int progress=0;
        int r=ssh_recv(s,0);
        if (r==1) {
            progress=1;
            uint8_t t=s->pkt[0];
            if (t==MSG_CHANNEL_DATA) {
                uint32_t dl=rd_u32(s->pkt+5);
                if (inw>=0) write(inw,s->pkt+9,dl);
                local_window-=dl;
                if (local_window<1000000) {
                    uint8_t p[16]; size_t pi=0;
                    p8(p,&pi,MSG_CHANNEL_WINDOW_ADJUST); p32(p,&pi,client_chan); p32(p,&pi,2000000);
                    ssh_send(s,p,pi); local_window+=2000000;
                }
            } else if (t==MSG_CHANNEL_WINDOW_ADJUST) {
                cli_window+=rd_u32(s->pkt+5);
            } else if (t==MSG_CHANNEL_EOF) {
                if (inw>=0){ close(inw); inw=-1; }
            } else if (t==MSG_CHANNEL_CLOSE) {
                done=1;
            }
        } else if (r<0) break;

        uint8_t buf[8192];
        long n=read(outr,buf,sizeof buf);
        if (n>0) {
            progress=1;
            size_t off=0;
            while (off<(size_t)n) {
                size_t chunk=(size_t)n-off; if (chunk>16384) chunk=16384;
                uint8_t p[16384+16]; size_t pi=0;
                p8(p,&pi,MSG_CHANNEL_DATA); p32(p,&pi,client_chan); pstr(p,&pi,buf+off,chunk);
                ssh_send(s,p,pi); off+=chunk;
            }
        }
        (void)cli_window;

        int st;
        pid_t w=waitpid(pid,&st,WNOHANG);
        if (w==pid) {
            for (;;) { long m=read(outr,buf,sizeof buf); if (m<=0) break;
                uint8_t p[16384+16]; size_t pi=0; p8(p,&pi,MSG_CHANNEL_DATA); p32(p,&pi,client_chan); pstr(p,&pi,buf,(size_t)m); ssh_send(s,p,pi); }
            exit_status=WEXITSTATUS(st);
            uint8_t p[64]; size_t pi=0;
            p8(p,&pi,MSG_CHANNEL_REQUEST); p32(p,&pi,client_chan); pcstr(p,&pi,"exit-status"); p8(p,&pi,0); p32(p,&pi,(uint32_t)exit_status);
            ssh_send(s,p,pi);
            pi=0; p8(p,&pi,MSG_CHANNEL_EOF); p32(p,&pi,client_chan); ssh_send(s,p,pi);
            pi=0; p8(p,&pi,MSG_CHANNEL_CLOSE); p32(p,&pi,client_chan); ssh_send(s,p,pi);
            done=1;
        }
        if (!progress) usleep(2000);
    }
    if (inw>=0) close(inw);
    close(outr);
    return exit_status;
}

static int handle_client(int fd, const uint8_t *hostpriv, const uint8_t *hostpub) {
    ssh_t *s=calloc(1,sizeof(ssh_t));
    if (!s) { close(fd); return -1; }
    s->fd=fd;
    char cver[256];
    if (version_exchange(s,cver,sizeof cver)) { free(s); close(fd); return -1; }

    uint8_t is[600]; size_t isl=0;
    p8(is,&isl,MSG_KEXINIT);
    crypto_random(is+isl,16); isl+=16;
    pcstr(is,&isl,"curve25519-sha256");
    pcstr(is,&isl,"ssh-ed25519");
    pcstr(is,&isl,"chacha20-poly1305@openssh.com");
    pcstr(is,&isl,"chacha20-poly1305@openssh.com");
    pcstr(is,&isl,"hmac-sha2-256");
    pcstr(is,&isl,"hmac-sha2-256");
    pcstr(is,&isl,"none");
    pcstr(is,&isl,"none");
    pcstr(is,&isl,"");
    pcstr(is,&isl,"");
    p8(is,&isl,0); p32(is,&isl,0);
    if (ssh_send(s,is,isl)) { free(s); close(fd); return -1; }

    if (wait_msg(s,MSG_KEXINIT)<0) { free(s); close(fd); return -1; }
    uint8_t ic[2048]; size_t icl=s->pkt_len;
    if (icl>sizeof ic) { free(s); close(fd); return -1; }
    memcpy(ic,s->pkt,icl);

    if (wait_msg(s,MSG_KEX_ECDH_INIT)<0) { free(s); close(fd); return -1; }
    uint8_t qc[32];
    if (rd_u32(s->pkt+1)!=32) { free(s); close(fd); return -1; }
    memcpy(qc,s->pkt+5,32);

    uint8_t epriv[32],epub[32],shared[32];
    crypto_random(epriv,32); x25519_base(epub,epriv); x25519(shared,epriv,qc);

    uint8_t ks[128]; size_t ksl=0;
    pcstr(ks,&ksl,"ssh-ed25519"); pstr(ks,&ksl,hostpub,32);

    uint8_t kmp[36]; size_t z=0;
    while (z<32 && shared[z]==0) z++;
    int lead=(z<32 && (shared[z]&0x80))?1:0;
    uint32_t maglen=(uint32_t)((32-z)+lead);
    wr_u32(kmp,maglen); size_t kl=4;
    if (lead) kmp[kl++]=0;
    memcpy(kmp+kl,shared+z,32-z); kl+=32-z;

    sha256_ctx hc; sha256_init(&hc);
    hash_str(&hc,(const uint8_t*)cver,(uint32_t)strlen(cver));
    hash_str(&hc,(const uint8_t*)"SSH-2.0-Cervus_sshd_1.0",23);
    hash_str(&hc,ic,(uint32_t)icl);
    hash_str(&hc,is,(uint32_t)isl);
    hash_str(&hc,ks,(uint32_t)ksl);
    hash_str(&hc,qc,32);
    hash_str(&hc,epub,32);
    sha256_update(&hc,kmp,kl);
    uint8_t H[32]; sha256_final(&hc,H);

    uint8_t sig[64]; ed25519_sign(sig,H,32,hostpriv);
    uint8_t sb[128]; size_t sbl=0;
    pcstr(sb,&sbl,"ssh-ed25519"); pstr(sb,&sbl,sig,64);

    uint8_t rep[300]; size_t rl=0;
    p8(rep,&rl,MSG_KEX_ECDH_REPLY);
    pstr(rep,&rl,ks,ksl); pstr(rep,&rl,epub,32); pstr(rep,&rl,sb,sbl);
    if (ssh_send(s,rep,rl)) { free(s); close(fd); return -1; }

    memcpy(s->session_id,H,32);
    derive_key(s,'C',kmp,kl,H,s->key_c2s);
    derive_key(s,'D',kmp,kl,H,s->key_s2c);

    uint8_t nk=MSG_NEWKEYS;
    if (ssh_send(s,&nk,1)) { free(s); close(fd); return -1; }
    s->enc_send=1;
    if (wait_msg(s,MSG_NEWKEYS)<0) { free(s); close(fd); return -1; }
    s->enc_recv=1;

    if (wait_msg(s,MSG_SERVICE_REQUEST)<0) { free(s); close(fd); return -1; }
    { uint8_t p[64]; size_t pi=0; p8(p,&pi,MSG_SERVICE_ACCEPT); pcstr(p,&pi,"ssh-userauth"); ssh_send(s,p,pi); }

    char user[128]; uint32_t uid=0,gid=0; char home[128],shell[128];
    if (authenticate(s,user,&uid,&gid,home,shell)) { free(s); close(fd); return -1; }

    if (wait_msg(s,MSG_CHANNEL_OPEN)<0) { free(s); close(fd); return -1; }
    uint32_t client_chan, cli_window;
    { size_t i=1; uint32_t tl=rd_u32(s->pkt+i); i+=4+tl; client_chan=rd_u32(s->pkt+i); i+=4; cli_window=rd_u32(s->pkt+i); i+=4; }
    { uint8_t p[64]; size_t pi=0;
      p8(p,&pi,MSG_CHANNEL_OPEN_CONFIRMATION); p32(p,&pi,client_chan); p32(p,&pi,0); p32(p,&pi,2000000); p32(p,&pi,16384);
      ssh_send(s,p,pi); }

    char cmdbuf[1024]; const char *cmd=0;
    for (;;) {
        if (wait_msg(s,-1)<0) { free(s); close(fd); return -1; }
        uint8_t t=s->pkt[0];
        if (t==MSG_CHANNEL_REQUEST) {
            size_t i=1; i+=4;
            uint32_t rlen=rd_u32(s->pkt+i); i+=4;
            char req[32]; if (rlen>31) rlen=31; memcpy(req,s->pkt+i,rlen); req[rlen]=0; i+=rd_u32(s->pkt+i-4);
            uint8_t want_reply=s->pkt[i]; i+=1;
            if (!strcmp(req,"exec")) {
                uint32_t cl=rd_u32(s->pkt+i); i+=4; if (cl>1023) cl=1023; memcpy(cmdbuf,s->pkt+i,cl); cmdbuf[cl]=0; cmd=cmdbuf;
                if (want_reply) { uint8_t p[8]; size_t pi=0; p8(p,&pi,MSG_CHANNEL_SUCCESS); p32(p,&pi,client_chan); ssh_send(s,p,pi); }
                break;
            } else if (!strcmp(req,"shell")) {
                if (want_reply) { uint8_t p[8]; size_t pi=0; p8(p,&pi,MSG_CHANNEL_SUCCESS); p32(p,&pi,client_chan); ssh_send(s,p,pi); }
                break;
            } else {
                if (want_reply) { uint8_t p[8]; size_t pi=0; p8(p,&pi,MSG_CHANNEL_SUCCESS); p32(p,&pi,client_chan); ssh_send(s,p,pi); }
            }
        } else if (t==MSG_CHANNEL_CLOSE) { free(s); close(fd); return 0; }
    }

    run_shell(s, client_chan, cli_window, uid, gid, home, shell, cmd);
    free(s); close(fd);
    return 0;
}

static int load_host_key(uint8_t priv[64], uint8_t pub[32]) {
    uint8_t seed[32];
    int fd=open(HOSTKEY_PATH,O_RDONLY);
    if (fd>=0) {
        int r=read(fd,seed,32); close(fd);
        if (r==32) { ed25519_keypair(pub,priv,seed); return 0; }
    }
    crypto_random(seed,32);
    ed25519_keypair(pub,priv,seed);
    fd=open(HOSTKEY_PATH,O_WRONLY|O_CREAT|O_TRUNC,0600);
    if (fd>=0) { write(fd,seed,32); close(fd); }
    return 0;
}

int main(int argc, char **argv) {
    int port=22, foreground=0;
    for (int a=1;a<argc;a++) {
        if (!strcmp(argv[a],"-p") && a+1<argc) port=atoi(argv[++a]);
        else if (!strcmp(argv[a],"-f")) foreground=1;
    }

    uint8_t hostpriv[64], hostpub[32];
    load_host_key(hostpriv,hostpub);
    uint8_t fp[32]; sha256(hostpub,32,fp);
    printf("sshd: host key ssh-ed25519 SHA256:");
    for (int i=0;i<32;i++) printf("%02x",fp[i]);
    printf("\n");

    int ls=socket(AF_INET,SOCK_STREAM,0);
    if (ls<0) { printf("sshd: socket failed\n"); return 1; }
    struct sockaddr_in a; memset(&a,0,sizeof a);
    a.sin_family=AF_INET; a.sin_port=htons((uint16_t)port); a.sin_addr.s_addr=INADDR_ANY;
    if (bind(ls,(struct sockaddr*)&a,sizeof a)<0) { printf("sshd: bind :%d failed\n",port); return 1; }
    if (listen(ls,4)<0) { printf("sshd: listen failed\n"); return 1; }
    printf("sshd: listening on port %d\n", port);
    (void)foreground;

    for (;;) {
        struct sockaddr_in cli; socklen_t cl=sizeof cli;
        int c=accept(ls,(struct sockaddr*)&cli,&cl);
        if (c<0) { usleep(10000); continue; }
        pid_t pid=fork();
        if (pid==0) { close(ls); handle_client(c,hostpriv,hostpub); _exit(0); }
        close(c);
        int st; while (waitpid(-1,&st,WNOHANG)>0) {}
    }
    return 0;
}
