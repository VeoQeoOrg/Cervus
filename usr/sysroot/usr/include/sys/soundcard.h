#ifndef _SYS_SOUNDCARD_H
#define _SYS_SOUNDCARD_H
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/ioctl.h>

#define SOUND_VERSION   0x040100
#define OSS_VERSION     0x040100
#define OPEN_SOUND_SYSTEM

typedef char oss_devname_t[64];
typedef char oss_longname_t[64];
typedef char oss_label_t[16];
typedef char oss_devnode_t[32];
typedef char oss_handle_t[32];
typedef char oss_cmd_t[64];
typedef char oss_id_t[16];

#define OSS_DEVNODE_SIZE   32
#define OSS_LABEL_SIZE     16
#define OSS_LONGNAME_SIZE  64
#define OSS_MAX_AUDIO_DEVS 64

#define _SIO(x, y)       _IO(x, y)
#define _SIOR(x, y, t)   _IOR(x, y, t)
#define _SIOW(x, y, t)   _IOW(x, y, t)
#define _SIOWR(x, y, t)  _IOWR(x, y, t)

#define AFMT_QUERY       0x00000000
#define AFMT_MU_LAW      0x00000001
#define AFMT_A_LAW       0x00000002
#define AFMT_IMA_ADPCM   0x00000004
#define AFMT_U8          0x00000008
#define AFMT_S16_LE      0x00000010
#define AFMT_S16_BE      0x00000020
#define AFMT_S8          0x00000040
#define AFMT_U16_LE      0x00000080
#define AFMT_U16_BE      0x00000100
#define AFMT_MPEG        0x00000200
#define AFMT_AC3         0x00000400
#define AFMT_VORBIS      0x00000800
#define AFMT_S32_LE      0x00001000
#define AFMT_S32_BE      0x00002000
#define AFMT_FLOAT       0x00004000
#define AFMT_S24_LE      0x00008000
#define AFMT_S24_BE      0x00010000
#define AFMT_SPDIF_RAW   0x00020000
#define AFMT_S24_PACKED  0x00040000
#define AFMT_S16_NE      AFMT_S16_LE
#define AFMT_U16_NE      AFMT_U16_LE
#define AFMT_S32_NE      AFMT_S32_LE
#define AFMT_S24_NE      AFMT_S24_LE

typedef struct audio_buf_info {
    int fragments;
    int fragstotal;
    int fragsize;
    int bytes;
} audio_buf_info;

typedef struct count_info {
    unsigned int bytes;
    int blocks;
    int ptr;
} count_info;

typedef struct buffmem_desc {
    unsigned *buffer;
    int size;
} buffmem_desc;

typedef struct audio_errinfo {
    int play_underruns;
    int rec_overruns;
    unsigned int play_ptradjust;
    unsigned int rec_ptradjust;
    int play_errorcount;
    int rec_errorcount;
    int play_lasterror;
    int rec_lasterror;
    int play_errorparm;
    int rec_errorparm;
    int filler[16];
} audio_errinfo;

#define SNDCTL_DSP_HALT          _SIO('P', 0)
#define SNDCTL_DSP_RESET         SNDCTL_DSP_HALT
#define SNDCTL_DSP_SYNC          _SIO('P', 1)
#define SNDCTL_DSP_SPEED         _SIOWR('P', 2, int)
#define SNDCTL_DSP_STEREO        _SIOWR('P', 3, int)
#define SNDCTL_DSP_GETBLKSIZE    _SIOWR('P', 4, int)
#define SNDCTL_DSP_SETFMT        _SIOWR('P', 5, int)
#define SNDCTL_DSP_SAMPLESIZE    SNDCTL_DSP_SETFMT
#define SNDCTL_DSP_CHANNELS      _SIOWR('P', 6, int)
#define SOUND_PCM_WRITE_CHANNELS SNDCTL_DSP_CHANNELS
#define SNDCTL_DSP_POST          _SIO('P', 8)
#define SNDCTL_DSP_SUBDIVIDE     _SIOWR('P', 9, int)
#define SNDCTL_DSP_SETFRAGMENT   _SIOWR('P', 10, int)
#define SNDCTL_DSP_GETFMTS       _SIOR('P', 11, int)
#define SNDCTL_DSP_GETOSPACE     _SIOR('P', 12, audio_buf_info)
#define SNDCTL_DSP_GETISPACE     _SIOR('P', 13, audio_buf_info)
#define SNDCTL_DSP_NONBLOCK      _SIO('P', 14)
#define SNDCTL_DSP_GETCAPS       _SIOR('P', 15, int)
#define SNDCTL_DSP_GETTRIGGER    _SIOR('P', 16, int)
#define SNDCTL_DSP_SETTRIGGER    _SIOW('P', 16, int)
#define SNDCTL_DSP_GETIPTR       _SIOR('P', 17, count_info)
#define SNDCTL_DSP_GETOPTR       _SIOR('P', 18, count_info)
#define SNDCTL_DSP_MAPINBUF      _SIOR('P', 19, buffmem_desc)
#define SNDCTL_DSP_MAPOUTBUF     _SIOR('P', 20, buffmem_desc)
#define SNDCTL_DSP_SETSYNCRO     _SIO('P', 21)
#define SNDCTL_DSP_SETDUPLEX     _SIO('P', 22)
#define SNDCTL_DSP_GETODELAY     _SIOR('P', 23, int)
#define SNDCTL_DSP_GETPLAYVOL    _SIOR('P', 24, int)
#define SNDCTL_DSP_SETPLAYVOL    _SIOWR('P', 24, int)
#define SNDCTL_DSP_GETERROR      _SIOR('P', 25, audio_errinfo)
#define SNDCTL_DSP_READCTL       _SIOWR('P', 26, oss_digital_control)
#define SNDCTL_DSP_WRITECTL      _SIOWR('P', 27, oss_digital_control)
#define SNDCTL_DSP_SYNCGROUP     _SIOWR('P', 28, oss_syncgroup)
#define SNDCTL_DSP_SYNCSTART     _SIOW('P', 29, int)
#define SNDCTL_DSP_COOKEDMODE    _SIOW('P', 30, int)
#define SNDCTL_DSP_SILENCE       _SIO('P', 31)
#define SNDCTL_DSP_SKIP          _SIO('P', 32)
#define SNDCTL_DSP_HALT_INPUT    _SIO('P', 33)
#define SNDCTL_DSP_RESET_INPUT   SNDCTL_DSP_HALT_INPUT
#define SNDCTL_DSP_HALT_OUTPUT   _SIO('P', 34)
#define SNDCTL_DSP_RESET_OUTPUT  SNDCTL_DSP_HALT_OUTPUT
#define SNDCTL_DSP_LOW_WATER     _SIOW('P', 34, int)
#define SNDCTL_DSP_CURRENT_OPTR  _SIOR('P', 35, oss_count_t)
#define SNDCTL_DSP_CURRENT_IPTR  _SIOR('P', 36, oss_count_t)
#define SNDCTL_DSP_GET_RECSRC_NAMES _SIOR('P', 37, oss_mixer_enuminfo)
#define SNDCTL_DSP_GET_RECSRC    _SIOR('P', 38, int)
#define SNDCTL_DSP_SET_RECSRC    _SIOWR('P', 38, int)
#define SNDCTL_DSP_GET_PLAYTGT_NAMES _SIOR('P', 39, oss_mixer_enuminfo)
#define SNDCTL_DSP_GET_PLAYTGT   _SIOR('P', 40, int)
#define SNDCTL_DSP_SET_PLAYTGT   _SIOWR('P', 40, int)
#define SNDCTL_DSP_GETRECVOL     _SIOR('P', 41, int)
#define SNDCTL_DSP_SETRECVOL     _SIOWR('P', 41, int)
#define SNDCTL_DSP_GET_CHNORDER  _SIOR('P', 42, unsigned long long)
#define SNDCTL_DSP_SET_CHNORDER  _SIOWR('P', 42, unsigned long long)
#define SNDCTL_DSP_GETIPEAKS     _SIOR('P', 43, oss_peaks_t)
#define SNDCTL_DSP_GETOPEAKS     _SIOR('P', 44, oss_peaks_t)
#define SNDCTL_DSP_POLICY        _SIOW('P', 45, int)
#define SNDCTL_DSP_GETCHANNELMASK _SIOWR('P', 64, int)
#define SNDCTL_DSP_BIND_CHANNEL  _SIOWR('P', 65, int)

#define SOUND_PCM_READ_RATE      _SIOR('P', 2, int)
#define SOUND_PCM_READ_CHANNELS  _SIOR('P', 6, int)
#define SOUND_PCM_READ_BITS      _SIOR('P', 5, int)

#define DSP_CAP_REVISION   0x000000ff
#define DSP_CAP_DUPLEX     0x00000100
#define DSP_CAP_REALTIME   0x00000200
#define DSP_CAP_BATCH      0x00000400
#define DSP_CAP_COPROC     0x00000800
#define DSP_CAP_TRIGGER    0x00001000
#define DSP_CAP_MMAP       0x00002000
#define DSP_CAP_MULTI      0x00004000
#define DSP_CAP_BIND       0x00008000
#define DSP_CAP_INPUT      0x00010000
#define DSP_CAP_OUTPUT     0x00020000
#define DSP_CAP_VIRTUAL    0x00040000
#define DSP_CAP_ANALOGOUT  0x00100000
#define DSP_CAP_ANALOGIN   0x00200000
#define DSP_CAP_DIGITALOUT 0x00400000
#define DSP_CAP_DIGITALIN  0x00800000
#define DSP_CAP_ADMASK     0x00f00000
#define DSP_CAP_SHADOW     0x01000000
#define DSP_CAP_FREERATE   0x10000000
#define DSP_CAP_HIDDEN     0x08000000

#define PCM_CAP_REVISION   DSP_CAP_REVISION
#define PCM_CAP_DUPLEX     DSP_CAP_DUPLEX
#define PCM_CAP_REALTIME   DSP_CAP_REALTIME
#define PCM_CAP_BATCH      DSP_CAP_BATCH
#define PCM_CAP_COPROC     DSP_CAP_COPROC
#define PCM_CAP_TRIGGER    DSP_CAP_TRIGGER
#define PCM_CAP_MMAP       DSP_CAP_MMAP
#define PCM_CAP_MULTI      DSP_CAP_MULTI
#define PCM_CAP_BIND       DSP_CAP_BIND
#define PCM_CAP_INPUT      DSP_CAP_INPUT
#define PCM_CAP_OUTPUT     DSP_CAP_OUTPUT
#define PCM_CAP_VIRTUAL    DSP_CAP_VIRTUAL
#define PCM_CAP_ANALOGOUT  DSP_CAP_ANALOGOUT
#define PCM_CAP_ANALOGIN   DSP_CAP_ANALOGIN
#define PCM_CAP_DIGITALOUT DSP_CAP_DIGITALOUT
#define PCM_CAP_DIGITALIN  DSP_CAP_DIGITALIN
#define PCM_CAP_ADMASK     DSP_CAP_ADMASK
#define PCM_CAP_SHADOW     DSP_CAP_SHADOW
#define PCM_CAP_FREERATE   DSP_CAP_FREERATE
#define PCM_CAP_HIDDEN     DSP_CAP_HIDDEN

#define PCM_ENABLE_INPUT   0x00000001
#define PCM_ENABLE_OUTPUT  0x00000002

#define DSP_BIND_QUERY     0x00000000
#define DSP_BIND_FRONT     0x00000001
#define DSP_BIND_SURR      0x00000002
#define DSP_BIND_CENTER_LFE 0x00000004

#define OPEN_READ          PCM_ENABLE_INPUT
#define OPEN_WRITE         PCM_ENABLE_OUTPUT
#define OPEN_READWRITE     (OPEN_READ | OPEN_WRITE)

typedef struct oss_digital_control {
    unsigned int caps;
    unsigned int valid;
    unsigned char cbitin[24];
    unsigned char ubitin[24];
    unsigned char cbitout[24];
    unsigned char ubitout[24];
    unsigned int outsel;
    int in_data;
    int in_locked;
    int in_quality;
    int in_vbit, out_vbit;
    unsigned int in_errors;
    int srate_in, srate_out;
    int filler[8];
} oss_digital_control;

typedef struct oss_syncgroup {
    int id;
    int mode;
    int filler[16];
} oss_syncgroup;

typedef struct {
    long long samples;
    int fifo_samples;
    int filler[32];
} oss_count_t;

typedef unsigned short oss_peaks_t[18];

typedef struct oss_mixer_enuminfo {
    int dev;
    int ctrl;
    int nvalues;
    int version;
    short strindex[255];
    char strings[3000];
} oss_mixer_enuminfo;

typedef struct oss_sysinfo {
    char product[32];
    char version[32];
    int versionnum;
    char options[128];
    int numaudios;
    int openedaudio[8];
    int numsynths;
    int nummidis;
    int numtimers;
    int nummixers;
    int openedmidi[8];
    int numcards;
    int numaudioengines;
    char license[16];
    char revision_info[256];
    int filler[172];
} oss_sysinfo;

#define OSS_MAX_SAMPLE_RATES 20

typedef struct oss_audioinfo {
    int dev;
    oss_devname_t name;
    int busy;
    int pid;
    int caps;
    int iformats, oformats;
    int magic;
    oss_cmd_t cmd;
    int card_number;
    int port_number;
    int mixer_dev;
    int legacy_device;
    int enabled;
    int flags;
    int min_rate, max_rate;
    int min_channels, max_channels;
    int binding;
    int rate_source;
    oss_handle_t handle;
    unsigned int nrates, rates[OSS_MAX_SAMPLE_RATES];
    oss_longname_t song_name;
    oss_label_t label;
    int latency;
    oss_devnode_t devnode;
    int next_play_engine;
    int next_rec_engine;
    int filler[184];
} oss_audioinfo;

typedef struct oss_mixerinfo {
    int dev;
    oss_id_t id;
    char name[32];
    int modify_counter;
    int card_number;
    int port_number;
    oss_handle_t handle;
    int magic;
    int enabled;
    int caps;
    int flags;
    int nrext;
    int priority;
    oss_devnode_t devnode;
    int legacy_device;
    int filler[245];
} oss_mixerinfo;

typedef struct oss_card_info {
    int card;
    char shortname[16];
    char longname[128];
    int flags;
    char hw_info[400];
    int intr_count, ack_count;
    int filler[154];
} oss_card_info;

#define MIXER_CAP_VIRTUAL  0x00000001
#define MIXER_CAP_LAYOUT_B 0x00000002
#define MIXER_CAP_NARROW   0x00000004

#define SNDCTL_SYSINFO      _SIOR('X', 1, oss_sysinfo)
#define OSS_SYSINFO         SNDCTL_SYSINFO
#define SNDCTL_MIX_NRMIX    _SIOR('X', 2, int)
#define SNDCTL_MIX_NREXT    _SIOWR('X', 3, int)
#define SNDCTL_AUDIOINFO    _SIOWR('X', 7, oss_audioinfo)
#define SNDCTL_AUDIOINFO_EX _SIOWR('X', 13, oss_audioinfo)
#define SNDCTL_CARDINFO     _SIOWR('X', 8, oss_card_info)
#define SNDCTL_ENGINEINFO   _SIOWR('X', 12, oss_audioinfo)
#define SNDCTL_MIXERINFO    _SIOWR('X', 10, oss_mixerinfo)
#define SNDCTL_MIDIINFO     _SIOWR('X', 9, oss_midi_info)
#define SNDCTL_SETSONG      _SIOW('Y', 2, oss_longname_t)
#define SNDCTL_GETSONG      _SIOR('Y', 2, oss_longname_t)
#define SNDCTL_SETNAME      _SIOW('Y', 3, oss_longname_t)
#define SNDCTL_SETLABEL     _SIOW('Y', 4, oss_label_t)
#define SNDCTL_GETLABEL     _SIOR('Y', 4, oss_label_t)

typedef struct oss_midi_info {
    int dev;
    oss_devname_t name;
    int busy;
    int pid;
    char cmd[64];
    int caps;
    int magic;
    int card_number;
    int port_number;
    int enabled;
    int flags;
    oss_handle_t handle;
    oss_longname_t song_name;
    oss_label_t label;
    int latency;
    oss_devnode_t devnode;
    int legacy_device;
    int filler[235];
} oss_midi_info;

struct synth_info {
    char name[30];
    int device;
    int synth_type;
    int synth_subtype;
    int perc_mode;
    int nr_voices;
    int nr_drums;
    int instr_bank_size;
    unsigned int capabilities;
    int dummies[19];
};

struct midi_info {
    char name[30];
    int device;
    unsigned int capabilities;
    int dev_type;
    int dummies[18];
};

#define SNDCTL_SEQ_RESET     _SIO('Q', 0)
#define SNDCTL_SEQ_SYNC      _SIO('Q', 1)
#define SNDCTL_SYNTH_INFO    _SIOWR('Q', 2, struct synth_info)
#define SNDCTL_SEQ_CTRLRATE  _SIOWR('Q', 3, int)
#define SNDCTL_SEQ_GETOUTCOUNT _SIOR('Q', 4, int)
#define SNDCTL_SEQ_GETINCOUNT _SIOR('Q', 5, int)
#define SNDCTL_SEQ_PERCMODE  _SIOW('Q', 6, int)
#define SNDCTL_SEQ_TESTMIDI  _SIOW('Q', 8, int)
#define SNDCTL_SEQ_RESETSAMPLES _SIOW('Q', 9, int)
#define SNDCTL_SEQ_NRSYNTHS  _SIOR('Q', 10, int)
#define SNDCTL_SEQ_NRMIDIS   _SIOR('Q', 11, int)
#define SNDCTL_MIDI_INFO     _SIOWR('Q', 12, struct midi_info)
#define SNDCTL_SEQ_THRESHOLD _SIOW('Q', 13, int)
#define SNDCTL_SYNTH_MEMAVL  _SIOWR('Q', 14, int)
#define SNDCTL_FM_4OP_ENABLE _SIOW('Q', 15, int)
#define SNDCTL_SEQ_PANIC     _SIO('Q', 17)
#define SNDCTL_SEQ_OUTOFBAND _SIOW('Q', 18, struct seq_event_rec)
#define SNDCTL_SEQ_GETTIME   _SIOR('Q', 19, int)

struct seq_event_rec {
    unsigned char arr[8];
};

#define SYNTH_TYPE_FM      0
#define SYNTH_TYPE_SAMPLE  1
#define SYNTH_TYPE_MIDI    2
#define FM_TYPE_ADLIB      0x00
#define FM_TYPE_OPL3       0x01
#define MIDI_TYPE_MPU401   0x401
#define SAMPLE_TYPE_BASIC  0x10
#define SAMPLE_TYPE_GUS    SAMPLE_TYPE_BASIC
#define SYNTH_CAP_PERCMODE 0x00000001
#define SYNTH_CAP_OPL3     0x00000002
#define SYNTH_CAP_INPUT    0x00000004

#define SOUND_MIXER_NRDEVICES 28
#define SOUND_MIXER_VOLUME    0
#define SOUND_MIXER_BASS      1
#define SOUND_MIXER_TREBLE    2
#define SOUND_MIXER_SYNTH     3
#define SOUND_MIXER_PCM       4
#define SOUND_MIXER_SPEAKER   5
#define SOUND_MIXER_LINE      6
#define SOUND_MIXER_MIC       7
#define SOUND_MIXER_CD        8
#define SOUND_MIXER_IMIX      9
#define SOUND_MIXER_ALTPCM    10
#define SOUND_MIXER_RECLEV    11
#define SOUND_MIXER_IGAIN     12
#define SOUND_MIXER_OGAIN     13
#define SOUND_MIXER_LINE1     14
#define SOUND_MIXER_LINE2     15
#define SOUND_MIXER_LINE3     16
#define SOUND_MIXER_DIGITAL1  17
#define SOUND_MIXER_DIGITAL2  18
#define SOUND_MIXER_DIGITAL3  19
#define SOUND_MIXER_PHONEIN   20
#define SOUND_MIXER_PHONEOUT  21
#define SOUND_MIXER_VIDEO     22
#define SOUND_MIXER_RADIO     23
#define SOUND_MIXER_MONITOR   24
#define SOUND_MIXER_DEPTH     25
#define SOUND_MIXER_REARVOL   26
#define SOUND_MIXER_CENTERVOL 27
#define SOUND_MIXER_SIDEVOL   28

#define SOUND_ONOFF_MIN       28
#define SOUND_ONOFF_MAX       30
#define SOUND_MIXER_NONE      31

#define SOUND_MIXER_RECSRC     0xff
#define SOUND_MIXER_DEVMASK    0xfe
#define SOUND_MIXER_RECMASK    0xfd
#define SOUND_MIXER_CAPS       0xfc
#define SOUND_CAP_EXCL_INPUT   0x00000001
#define SOUND_MIXER_STEREODEVS 0xfb
#define SOUND_MIXER_OUTSRC     0xfa
#define SOUND_MIXER_OUTMASK    0xf9

#define SOUND_MASK_VOLUME  (1 << SOUND_MIXER_VOLUME)
#define SOUND_MASK_BASS    (1 << SOUND_MIXER_BASS)
#define SOUND_MASK_TREBLE  (1 << SOUND_MIXER_TREBLE)
#define SOUND_MASK_SYNTH   (1 << SOUND_MIXER_SYNTH)
#define SOUND_MASK_PCM     (1 << SOUND_MIXER_PCM)
#define SOUND_MASK_SPEAKER (1 << SOUND_MIXER_SPEAKER)
#define SOUND_MASK_LINE    (1 << SOUND_MIXER_LINE)
#define SOUND_MASK_MIC     (1 << SOUND_MIXER_MIC)
#define SOUND_MASK_CD      (1 << SOUND_MIXER_CD)
#define SOUND_MASK_IMIX    (1 << SOUND_MIXER_IMIX)
#define SOUND_MASK_ALTPCM  (1 << SOUND_MIXER_ALTPCM)
#define SOUND_MASK_RECLEV  (1 << SOUND_MIXER_RECLEV)
#define SOUND_MASK_IGAIN   (1 << SOUND_MIXER_IGAIN)
#define SOUND_MASK_OGAIN   (1 << SOUND_MIXER_OGAIN)
#define SOUND_MASK_LINE1   (1 << SOUND_MIXER_LINE1)
#define SOUND_MASK_LINE2   (1 << SOUND_MIXER_LINE2)
#define SOUND_MASK_LINE3   (1 << SOUND_MIXER_LINE3)
#define SOUND_MASK_DIGITAL1 (1 << SOUND_MIXER_DIGITAL1)
#define SOUND_MASK_DIGITAL2 (1 << SOUND_MIXER_DIGITAL2)
#define SOUND_MASK_DIGITAL3 (1 << SOUND_MIXER_DIGITAL3)
#define SOUND_MASK_PHONEIN (1 << SOUND_MIXER_PHONEIN)
#define SOUND_MASK_PHONEOUT (1 << SOUND_MIXER_PHONEOUT)
#define SOUND_MASK_RADIO   (1 << SOUND_MIXER_RADIO)
#define SOUND_MASK_VIDEO   (1 << SOUND_MIXER_VIDEO)
#define SOUND_MASK_MONITOR (1 << SOUND_MIXER_MONITOR)
#define SOUND_MASK_DEPTH   (1 << SOUND_MIXER_DEPTH)
#define SOUND_MASK_REARVOL (1 << SOUND_MIXER_REARVOL)
#define SOUND_MASK_CENTERVOL (1 << SOUND_MIXER_CENTERVOL)
#define SOUND_MASK_SIDEVOL (1 << SOUND_MIXER_SIDEVOL)

#define SOUND_DEVICE_LABELS { "Vol  ", "Bass ", "Treble", "Synth", "Pcm  ", "Speaker ", "Line ", \
    "Mic  ", "CD   ", "Mix  ", "Pcm2 ", "Rec  ", "IGain", "OGain", "Aux1", "Aux2", "Aux3", \
    "Digital1", "Digital2", "Digital3", "PhoneIn", "PhoneOut", "Video", "Radio", "Monitor", \
    "Depth", "Rear", "Center", "Side" }

#define SOUND_DEVICE_NAMES { "vol", "bass", "treble", "synth", "pcm", "speaker", "line", \
    "mic", "cd", "mix", "pcm2", "rec", "igain", "ogain", "aux1", "aux2", "aux3", \
    "dig1", "dig2", "dig3", "phin", "phout", "video", "radio", "monitor", \
    "depth", "rear", "center", "side" }

#define MIXER_READ(dev)   _SIOR('M', dev, int)
#define MIXER_WRITE(dev)  _SIOWR('M', dev, int)

#define SOUND_MIXER_READ_VOLUME     MIXER_READ(SOUND_MIXER_VOLUME)
#define SOUND_MIXER_READ_BASS       MIXER_READ(SOUND_MIXER_BASS)
#define SOUND_MIXER_READ_TREBLE     MIXER_READ(SOUND_MIXER_TREBLE)
#define SOUND_MIXER_READ_SYNTH      MIXER_READ(SOUND_MIXER_SYNTH)
#define SOUND_MIXER_READ_PCM        MIXER_READ(SOUND_MIXER_PCM)
#define SOUND_MIXER_READ_SPEAKER    MIXER_READ(SOUND_MIXER_SPEAKER)
#define SOUND_MIXER_READ_LINE       MIXER_READ(SOUND_MIXER_LINE)
#define SOUND_MIXER_READ_MIC        MIXER_READ(SOUND_MIXER_MIC)
#define SOUND_MIXER_READ_CD         MIXER_READ(SOUND_MIXER_CD)
#define SOUND_MIXER_READ_IMIX       MIXER_READ(SOUND_MIXER_IMIX)
#define SOUND_MIXER_READ_ALTPCM     MIXER_READ(SOUND_MIXER_ALTPCM)
#define SOUND_MIXER_READ_RECLEV     MIXER_READ(SOUND_MIXER_RECLEV)
#define SOUND_MIXER_READ_IGAIN      MIXER_READ(SOUND_MIXER_IGAIN)
#define SOUND_MIXER_READ_OGAIN      MIXER_READ(SOUND_MIXER_OGAIN)
#define SOUND_MIXER_READ_LINE1      MIXER_READ(SOUND_MIXER_LINE1)
#define SOUND_MIXER_READ_LINE2      MIXER_READ(SOUND_MIXER_LINE2)
#define SOUND_MIXER_READ_LINE3      MIXER_READ(SOUND_MIXER_LINE3)
#define SOUND_MIXER_READ_RECSRC     MIXER_READ(SOUND_MIXER_RECSRC)
#define SOUND_MIXER_READ_RECMASK    MIXER_READ(SOUND_MIXER_RECMASK)
#define SOUND_MIXER_READ_DEVMASK    MIXER_READ(SOUND_MIXER_DEVMASK)
#define SOUND_MIXER_READ_STEREODEVS MIXER_READ(SOUND_MIXER_STEREODEVS)
#define SOUND_MIXER_READ_CAPS       MIXER_READ(SOUND_MIXER_CAPS)

#define SOUND_MIXER_WRITE_VOLUME    MIXER_WRITE(SOUND_MIXER_VOLUME)
#define SOUND_MIXER_WRITE_BASS      MIXER_WRITE(SOUND_MIXER_BASS)
#define SOUND_MIXER_WRITE_TREBLE    MIXER_WRITE(SOUND_MIXER_TREBLE)
#define SOUND_MIXER_WRITE_SYNTH     MIXER_WRITE(SOUND_MIXER_SYNTH)
#define SOUND_MIXER_WRITE_PCM       MIXER_WRITE(SOUND_MIXER_PCM)
#define SOUND_MIXER_WRITE_SPEAKER   MIXER_WRITE(SOUND_MIXER_SPEAKER)
#define SOUND_MIXER_WRITE_LINE      MIXER_WRITE(SOUND_MIXER_LINE)
#define SOUND_MIXER_WRITE_MIC       MIXER_WRITE(SOUND_MIXER_MIC)
#define SOUND_MIXER_WRITE_CD        MIXER_WRITE(SOUND_MIXER_CD)
#define SOUND_MIXER_WRITE_IMIX      MIXER_WRITE(SOUND_MIXER_IMIX)
#define SOUND_MIXER_WRITE_ALTPCM    MIXER_WRITE(SOUND_MIXER_ALTPCM)
#define SOUND_MIXER_WRITE_RECLEV    MIXER_WRITE(SOUND_MIXER_RECLEV)
#define SOUND_MIXER_WRITE_IGAIN     MIXER_WRITE(SOUND_MIXER_IGAIN)
#define SOUND_MIXER_WRITE_OGAIN     MIXER_WRITE(SOUND_MIXER_OGAIN)
#define SOUND_MIXER_WRITE_LINE1     MIXER_WRITE(SOUND_MIXER_LINE1)
#define SOUND_MIXER_WRITE_LINE2     MIXER_WRITE(SOUND_MIXER_LINE2)
#define SOUND_MIXER_WRITE_LINE3     MIXER_WRITE(SOUND_MIXER_LINE3)
#define SOUND_MIXER_WRITE_RECSRC    MIXER_WRITE(SOUND_MIXER_RECSRC)

typedef struct mixer_info {
    char id[16];
    char name[32];
    int modify_counter;
    int card_number;
    int port_number;
    char handle[32];
} mixer_info;

#define SOUND_MIXER_INFO     _SIOR('M', 101, mixer_info)
#define OSS_GETVERSION       _SIOR('M', 118, int)
#define SOUND_MIXER_ACCESS   _SIOWR('M', 102, int)
#define SOUND_MIXER_PRIVATE1 _SIOWR('M', 111, int)


#define FM_PATCH   0x01fd
#define OPL3_PATCH 0x03fd

typedef unsigned char sbi_instr_data[32];

struct sbi_instrument {
    unsigned short key;
    short device;
    int channel;
    sbi_instr_data operators;
};

#define MIDI_NOTEOFF       0x80
#define MIDI_NOTEON        0x90
#define MIDI_KEY_PRESSURE  0xA0
#define MIDI_CTL_CHANGE    0xB0
#define MIDI_PGM_CHANGE    0xC0
#define MIDI_CHN_PRESSURE  0xD0
#define MIDI_PITCH_BEND    0xE0
#define MIDI_SYSTEM_PREFIX 0xF0

#define CTL_BANK_SELECT          0x00
#define CTL_MODWHEEL             0x01
#define CTL_BREATH               0x02
#define CTL_FOOT                 0x04
#define CTL_PORTAMENTO_TIME      0x05
#define CTL_DATA_ENTRY           0x06
#define CTL_MAIN_VOLUME          0x07
#define CTL_BALANCE              0x08
#define CTL_PAN                  0x0a
#define CTL_EXPRESSION           0x0b
#define CTL_GENERAL_PURPOSE1     0x10
#define CTL_GENERAL_PURPOSE2     0x11
#define CTL_GENERAL_PURPOSE3     0x12
#define CTL_GENERAL_PURPOSE4     0x13
#define CTL_DAMPER_PEDAL         0x40
#define CTL_SUSTAIN              0x40
#define CTL_HOLD                 0x40
#define CTL_PORTAMENTO           0x41
#define CTL_SOSTENUTO            0x42
#define CTL_SOFT_PEDAL           0x43
#define CTL_HOLD2                0x45
#define CTL_GENERAL_PURPOSE5     0x50
#define CTL_GENERAL_PURPOSE6     0x51
#define CTL_GENERAL_PURPOSE7     0x52
#define CTL_GENERAL_PURPOSE8     0x53
#define CTL_EXT_EFF_DEPTH        0x5b
#define CTL_TREMOLO_DEPTH        0x5c
#define CTL_CHORUS_DEPTH         0x5d
#define CTL_DETUNE_DEPTH         0x5e
#define CTL_CELESTE_DEPTH        0x5e
#define CTL_PHASER_DEPTH         0x5f
#define CTL_DATA_INCREMENT       0x60
#define CTL_DATA_DECREMENT       0x61
#define CTL_NONREG_PARM_NUM_LSB  0x62
#define CTL_NONREG_PARM_NUM_MSB  0x63
#define CTL_REGIST_PARM_NUM_LSB  0x64
#define CTL_REGIST_PARM_NUM_MSB  0x65

#define CTRL_PITCH_BENDER        255
#define CTRL_PITCH_BENDER_RANGE  254
#define CTRL_EXPRESSION          253
#define CTRL_MAIN_VOLUME         252

#define SEQ_BALANCE        255
#define SEQ_VOLMODE        12
#define VOL_METHOD_ADAGIO  1
#define VOL_METHOD_LINEAR  2

#define TMR_WAIT_REL  1
#define TMR_WAIT_ABS  2
#define TMR_STOP      3
#define TMR_START     4
#define TMR_CONTINUE  5
#define TMR_TEMPO     6
#define TMR_ECHO      8
#define TMR_CLOCK     9
#define TMR_SPP       10
#define TMR_TIMESIG   11

#define SEQ_NOTEOFF      0
#define SEQ_FMNOTEOFF    SEQ_NOTEOFF
#define SEQ_NOTEON       1
#define SEQ_FMNOTEON     SEQ_NOTEON
#define SEQ_WAIT         TMR_WAIT_ABS
#define SEQ_PGMCHANGE    3
#define SEQ_FMPGMCHANGE  SEQ_PGMCHANGE
#define SEQ_SYNCTIMER    TMR_START
#define SEQ_MIDIPUTC     5
#define SEQ_DRUMON       6
#define SEQ_DRUMOFF      7
#define SEQ_ECHO         TMR_ECHO
#define SEQ_AFTERTOUCH   9
#define SEQ_CONTROLLER   10
#define SEQ_EXTENDED     255

#define EV_SEQ_LOCAL     0x80
#define EV_TIMING        0x81
#define EV_CHN_COMMON    0x92
#define EV_CHN_VOICE     0x93
#define EV_SYSEX         0x94

#define LOCL_STARTAUDIO  1

#define SEQ_DEFINEBUF(len) unsigned char _seqbuf[len]; int _seqbuflen = len; int _seqbufptr = 0
#define SEQ_USE_EXTBUF() extern unsigned char _seqbuf[]; extern int _seqbuflen; extern int _seqbufptr
#define SEQ_DECLAREBUF() SEQ_USE_EXTBUF()
#define SEQ_PM_DEFINES int __foo_bar___
#define _SEQ_NEEDBUF(len) if ((_seqbufptr + (len)) > _seqbuflen) seqbuf_dump()
#define _SEQ_ADVBUF(len) _seqbufptr += (len)
#define SEQ_DUMPBUF seqbuf_dump

#define _CHN_VOICE(dev, event, chn, note, parm) \
    { _SEQ_NEEDBUF(8); \
      _seqbuf[_seqbufptr] = EV_CHN_VOICE; \
      _seqbuf[_seqbufptr + 1] = (dev); \
      _seqbuf[_seqbufptr + 2] = (event); \
      _seqbuf[_seqbufptr + 3] = (chn); \
      _seqbuf[_seqbufptr + 4] = (note); \
      _seqbuf[_seqbufptr + 5] = (parm); \
      _seqbuf[_seqbufptr + 6] = 0; \
      _seqbuf[_seqbufptr + 7] = 0; \
      _SEQ_ADVBUF(8); }

#define SEQ_START_NOTE(dev, chn, note, vol)       _CHN_VOICE(dev, MIDI_NOTEON, chn, note, vol)
#define SEQ_STOP_NOTE(dev, chn, note, vol)        _CHN_VOICE(dev, MIDI_NOTEOFF, chn, note, vol)
#define SEQ_KEY_PRESSURE(dev, chn, note, pressure) _CHN_VOICE(dev, MIDI_KEY_PRESSURE, chn, note, pressure)

#define _CHN_COMMON(dev, event, chn, p1, p2, w14) \
    { _SEQ_NEEDBUF(8); \
      _seqbuf[_seqbufptr] = EV_CHN_COMMON; \
      _seqbuf[_seqbufptr + 1] = (dev); \
      _seqbuf[_seqbufptr + 2] = (event); \
      _seqbuf[_seqbufptr + 3] = (chn); \
      _seqbuf[_seqbufptr + 4] = (p1); \
      _seqbuf[_seqbufptr + 5] = (p2); \
      _seqbuf[_seqbufptr + 6] = (unsigned char)((w14) & 0xff); \
      _seqbuf[_seqbufptr + 7] = (unsigned char)(((w14) >> 8) & 0xff); \
      _SEQ_ADVBUF(8); }

#define SEQ_CHN_PRESSURE(dev, chn, pressure)   _CHN_COMMON(dev, MIDI_CHN_PRESSURE, chn, pressure, 0, 0)
#define SEQ_PGM_CHANGE(dev, chn, patch)        _CHN_COMMON(dev, MIDI_PGM_CHANGE, chn, patch, 0, 0)
#define SEQ_SET_PATCH                          SEQ_PGM_CHANGE
#define SEQ_CONTROL(dev, chn, controller, value) _CHN_COMMON(dev, MIDI_CTL_CHANGE, chn, controller, 0, value)
#define SEQ_BENDER(dev, chn, value)            _CHN_COMMON(dev, MIDI_PITCH_BEND, chn, 0, 0, value)

#define SEQ_V2_X_CONTROL(dev, voice, controller, value) \
    { _SEQ_NEEDBUF(8); \
      _seqbuf[_seqbufptr] = SEQ_EXTENDED; \
      _seqbuf[_seqbufptr + 1] = SEQ_CONTROLLER; \
      _seqbuf[_seqbufptr + 2] = (dev); \
      _seqbuf[_seqbufptr + 3] = (voice); \
      _seqbuf[_seqbufptr + 4] = (controller); \
      _seqbuf[_seqbufptr + 5] = (unsigned char)((value) & 0xff); \
      _seqbuf[_seqbufptr + 6] = (unsigned char)(((value) >> 8) & 0xff); \
      _seqbuf[_seqbufptr + 7] = 0; \
      _SEQ_ADVBUF(8); }

#define SEQ_PITCHBEND(dev, voice, value)       SEQ_V2_X_CONTROL(dev, voice, CTRL_PITCH_BENDER, value)
#define SEQ_BENDER_RANGE(dev, voice, value)    SEQ_V2_X_CONTROL(dev, voice, CTRL_PITCH_BENDER_RANGE, value)
#define SEQ_EXPRESSION(dev, voice, value)      SEQ_CONTROL(dev, voice, CTL_EXPRESSION, (value) * 128)
#define SEQ_MAIN_VOLUME(dev, voice, value)     SEQ_CONTROL(dev, voice, CTL_MAIN_VOLUME, ((value) * 16383) / 100)
#define SEQ_PANNING(dev, voice, pos)           SEQ_CONTROL(dev, voice, CTL_PAN, ((pos) + 128) / 2)

#define _TIMER_EVENT(ev, parm) \
    { _SEQ_NEEDBUF(8); \
      _seqbuf[_seqbufptr] = EV_TIMING; \
      _seqbuf[_seqbufptr + 1] = (ev); \
      _seqbuf[_seqbufptr + 2] = 0; \
      _seqbuf[_seqbufptr + 3] = 0; \
      _seqbuf[_seqbufptr + 4] = (unsigned char)((parm) & 0xff); \
      _seqbuf[_seqbufptr + 5] = (unsigned char)(((parm) >> 8) & 0xff); \
      _seqbuf[_seqbufptr + 6] = (unsigned char)(((parm) >> 16) & 0xff); \
      _seqbuf[_seqbufptr + 7] = (unsigned char)(((parm) >> 24) & 0xff); \
      _SEQ_ADVBUF(8); }

#define SEQ_START_TIMER()      _TIMER_EVENT(TMR_START, 0)
#define SEQ_STOP_TIMER()       _TIMER_EVENT(TMR_STOP, 0)
#define SEQ_CONTINUE_TIMER()   _TIMER_EVENT(TMR_CONTINUE, 0)
#define SEQ_WAIT_TIME(ticks)   _TIMER_EVENT(TMR_WAIT_ABS, ticks)
#define SEQ_DELTA_TIME(ticks)  _TIMER_EVENT(TMR_WAIT_REL, ticks)
#define SEQ_ECHO_BACK(key)     _TIMER_EVENT(TMR_ECHO, key)
#define SEQ_SET_TEMPO(value)   _TIMER_EVENT(TMR_TEMPO, value)

#define SEQ_MIDIOUT(device, byte) \
    { _SEQ_NEEDBUF(4); \
      _seqbuf[_seqbufptr] = SEQ_MIDIPUTC; \
      _seqbuf[_seqbufptr + 1] = (byte); \
      _seqbuf[_seqbufptr + 2] = (device); \
      _seqbuf[_seqbufptr + 3] = 0; \
      _SEQ_ADVBUF(4); }

#ifdef __cplusplus
}
#endif
#endif
