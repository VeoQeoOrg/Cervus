#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/cervus.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include <minimp3.h>

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static int stream_stereo(const int16_t *st, size_t nframes, unsigned rate) {
    if (cervus_audio_open(rate) < 0) {
        fprintf(stderr, "play: no audio device\n");
        return 1;
    }
    const uint8_t *bytes = (const uint8_t *)st;
    size_t total = nframes * 4;
    size_t off = 0;
    while (off < total) {
        size_t chunk = total - off;
        if (chunk > 65536) chunk = 65536;
        long w = cervus_audio_write(bytes + off, chunk);
        if (w <= 0) break;
        off += (size_t)w;
    }
    cervus_audio_close();
    return 0;
}

static int play_tone(unsigned freq, unsigned secs) {
    unsigned rate = 44100;
    if (freq < 20) freq = 20;
    if (secs == 0) secs = 1;
    size_t nframes = (size_t)rate * secs;
    int16_t *st = malloc(nframes * 2 * sizeof(int16_t));
    if (!st) { fprintf(stderr, "play: out of memory\n"); return 1; }

    unsigned period = rate / freq;
    if (period < 2) period = 2;
    unsigned half = period / 2;
    int amp = 8000;
    for (size_t i = 0; i < nframes; i++) {
        unsigned pos = (unsigned)(i % period);
        int v;
        if (pos < half) v = -amp + (int)((2 * amp * pos) / half);
        else            v =  amp - (int)((2 * amp * (pos - half)) / half);
        st[i * 2 + 0] = (int16_t)v;
        st[i * 2 + 1] = (int16_t)v;
    }
    printf("playing %u Hz tone for %u s...\n", freq, secs);
    int r = stream_stereo(st, nframes, rate);
    free(st);
    return r;
}

static int play_wav(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "play: cannot open %s\n", path); return 1; }

    off_t end = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (end < 44) { fprintf(stderr, "play: %s not a WAV\n", path); close(fd); return 1; }

    uint8_t *f = malloc((size_t)end);
    if (!f) { fprintf(stderr, "play: out of memory\n"); close(fd); return 1; }
    size_t got = 0;
    while (got < (size_t)end) {
        long n = read(fd, f + got, (size_t)end - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fd);

    if (memcmp(f, "RIFF", 4) || memcmp(f + 8, "WAVE", 4)) {
        fprintf(stderr, "play: %s not a RIFF/WAVE file\n", path);
        free(f); return 1;
    }

    unsigned channels = 0, rate = 0, bits = 0, fmt = 0;
    const uint8_t *data = 0; size_t dsize = 0;
    size_t pos = 12;
    while (pos + 8 <= got) {
        const uint8_t *ck = f + pos;
        uint32_t clen = rd32(ck + 4);
        const uint8_t *body = ck + 8;
        if (pos + 8 + clen > got) clen = (uint32_t)(got - pos - 8);
        if (!memcmp(ck, "fmt ", 4) && clen >= 16) {
            fmt      = rd16(body + 0);
            channels = rd16(body + 2);
            rate     = rd32(body + 4);
            bits     = rd16(body + 14);
        } else if (!memcmp(ck, "data", 4)) {
            data = body; dsize = clen;
        }
        pos += 8 + clen + (clen & 1);
    }

    if (!data || !channels || !rate || (bits != 8 && bits != 16) || (fmt != 1)) {
        fprintf(stderr, "play: unsupported WAV (fmt=%u ch=%u bits=%u)\n", fmt, channels, bits);
        free(f); return 1;
    }

    unsigned frame = channels * (bits / 8);
    size_t nframes = dsize / frame;
    int16_t *st = malloc(nframes * 2 * sizeof(int16_t));
    if (!st) { fprintf(stderr, "play: out of memory\n"); free(f); return 1; }

    for (size_t i = 0; i < nframes; i++) {
        const uint8_t *fr = data + i * frame;
        int16_t l, r;
        if (bits == 8) {
            l = (int16_t)(((int)fr[0] - 128) << 8);
            r = (channels > 1) ? (int16_t)(((int)fr[1] - 128) << 8) : l;
        } else {
            l = (int16_t)rd16(fr);
            r = (channels > 1) ? (int16_t)rd16(fr + 2) : l;
        }
        st[i * 2 + 0] = l;
        st[i * 2 + 1] = r;
    }

    printf("playing %s: %u Hz, %u ch, %u-bit, %zu frames\n", path, rate, channels, bits, nframes);
    int rc = stream_stereo(st, nframes, rate);
    free(st);
    free(f);
    return rc;
}

static uint8_t *read_all(const char *path, size_t *outlen) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "play: cannot open %s\n", path); return 0; }
    off_t end = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (end <= 0) { close(fd); fprintf(stderr, "play: empty file\n"); return 0; }
    uint8_t *b = malloc((size_t)end);
    if (!b) { close(fd); fprintf(stderr, "play: out of memory\n"); return 0; }
    size_t got = 0; long n;
    while (got < (size_t)end && (n = read(fd, b + got, (size_t)end - got)) > 0) got += (size_t)n;
    close(fd);
    *outlen = got;
    return b;
}

static int play_mp3(const char *path) {
    size_t len = 0;
    uint8_t *buf = read_all(path, &len);
    if (!buf) return 1;

    mp3dec_t dec;
    mp3dec_init(&dec);
    mp3dec_frame_info_t info;
    short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int16_t st[1152 * 2];

    size_t pos = 0;
    int opened = 0;
    long total = 0;
    while (pos < len) {
        int samples = mp3dec_decode_frame(&dec, buf + pos, (int)(len - pos), pcm, &info);
        if (info.frame_bytes == 0) break;
        pos += (size_t)info.frame_bytes;
        if (samples <= 0) continue;
        if (!opened) {
            if (cervus_audio_open((unsigned)info.hz) < 0) {
                fprintf(stderr, "play: no audio device\n");
                free(buf); return 1;
            }
            printf("playing %s: MP3 %d Hz, %d ch, %d kbps\n",
                   path, info.hz, info.channels, info.bitrate_kbps);
            opened = 1;
        }
        int ch = info.channels;
        for (int i = 0; i < samples; i++) {
            int16_t l = pcm[i * ch];
            int16_t r = (ch > 1) ? pcm[i * ch + 1] : l;
            st[i * 2] = l;
            st[i * 2 + 1] = r;
        }
        cervus_audio_write(st, (unsigned long)samples * 4);
        total += samples;
    }
    if (opened) cervus_audio_close();
    else fprintf(stderr, "play: no MP3 frames found in %s\n", path);
    free(buf);
    return opened ? 0 : 1;
}

static int looks_like_mp3(const char *path) {
    size_t n = strlen(path);
    if (n >= 4 && !strcasecmp(path + n - 4, ".mp3")) return 1;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    uint8_t h[3] = {0};
    read(fd, h, 3);
    close(fd);
    if (h[0] == 'I' && h[1] == 'D' && h[2] == '3') return 1;
    if (h[0] == 0xFF && (h[1] & 0xE0) == 0xE0) return 1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && (!strcmp(argv[1], "-t") || !strcmp(argv[1], "--tone"))) {
        unsigned freq = (argc >= 3) ? (unsigned)atoi(argv[2]) : 440;
        unsigned secs = (argc >= 4) ? (unsigned)atoi(argv[3]) : 2;
        return play_tone(freq, secs);
    }
    if (argc < 2) {
        printf("usage: play file.wav | file.mp3\n");
        printf("       play -t [freq] [secs]\n");
        return 1;
    }
    if (looks_like_mp3(argv[1])) return play_mp3(argv[1]);
    return play_wav(argv[1]);
}
