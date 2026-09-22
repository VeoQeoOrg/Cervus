#ifndef CERVUS_DRM_UAPI_H
#define CERVUS_DRM_UAPI_H

#include <stdint.h>

#define DRM_IOC_NONE  0u
#define DRM_IOC_WRITE 1u
#define DRM_IOC_READ  2u

#define DRM_IOC(dir, nr, size) \
    ((uint32_t)(((dir) << 30) | ((size) << 16) | (0x64u << 8) | (nr)))

#define DRM_IO(nr)          DRM_IOC(DRM_IOC_NONE, nr, 0)
#define DRM_IOW(nr, type)   DRM_IOC(DRM_IOC_WRITE, nr, sizeof(type))
#define DRM_IOR(nr, type)   DRM_IOC(DRM_IOC_READ, nr, sizeof(type))
#define DRM_IOWR(nr, type)  DRM_IOC(DRM_IOC_READ | DRM_IOC_WRITE, nr, sizeof(type))

struct drm_version {
    int32_t  version_major;
    int32_t  version_minor;
    int32_t  version_patchlevel;
    uint64_t name_len;
    uint64_t name;
    uint64_t date_len;
    uint64_t date;
    uint64_t desc_len;
    uint64_t desc;
};

struct drm_unique {
    uint64_t unique_len;
    uint64_t unique;
};

struct drm_set_version {
    int32_t drm_di_major;
    int32_t drm_di_minor;
    int32_t drm_dd_major;
    int32_t drm_dd_minor;
};

struct drm_get_cap {
    uint64_t capability;
    uint64_t value;
};

struct drm_set_client_cap {
    uint64_t capability;
    uint64_t value;
};

#define DRM_CAP_DUMB_BUFFER            0x1
#define DRM_CAP_VBLANK_HIGH_CRTC       0x2
#define DRM_CAP_DUMB_PREFERRED_DEPTH   0x3
#define DRM_CAP_DUMB_PREFER_SHADOW     0x4
#define DRM_CAP_PRIME                  0x5
#define DRM_CAP_TIMESTAMP_MONOTONIC    0x6
#define DRM_CAP_ASYNC_PAGE_FLIP        0x7
#define DRM_CAP_CURSOR_WIDTH           0x8
#define DRM_CAP_CURSOR_HEIGHT          0x9
#define DRM_CAP_ADDFB2_MODIFIERS       0x10
#define DRM_CAP_PAGE_FLIP_TARGET       0x11
#define DRM_CAP_CRTC_IN_VBLANK_EVENT   0x12
#define DRM_CAP_SYNCOBJ                0x13
#define DRM_CAP_SYNCOBJ_TIMELINE       0x14
#define DRM_CAP_ATOMIC_ASYNC_PAGE_FLIP 0x15

#define DRM_CLIENT_CAP_STEREO_3D            1
#define DRM_CLIENT_CAP_UNIVERSAL_PLANES     2
#define DRM_CLIENT_CAP_ATOMIC               3
#define DRM_CLIENT_CAP_ASPECT_RATIO         4
#define DRM_CLIENT_CAP_WRITEBACK_CONNECTORS 5
#define DRM_CLIENT_CAP_CURSOR_PLANE_HOTSPOT 6

struct drm_mode_card_res {
    uint64_t fb_id_ptr;
    uint64_t crtc_id_ptr;
    uint64_t connector_id_ptr;
    uint64_t encoder_id_ptr;
    uint32_t count_fbs;
    uint32_t count_crtcs;
    uint32_t count_connectors;
    uint32_t count_encoders;
    uint32_t min_width;
    uint32_t max_width;
    uint32_t min_height;
    uint32_t max_height;
};

#define DRM_DISPLAY_MODE_LEN 32

struct drm_mode_modeinfo {
    uint32_t clock;
    uint16_t hdisplay;
    uint16_t hsync_start;
    uint16_t hsync_end;
    uint16_t htotal;
    uint16_t hskew;
    uint16_t vdisplay;
    uint16_t vsync_start;
    uint16_t vsync_end;
    uint16_t vtotal;
    uint16_t vscan;
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
    char     name[DRM_DISPLAY_MODE_LEN];
};

#define DRM_MODE_TYPE_PREFERRED (1 << 3)
#define DRM_MODE_TYPE_DRIVER    (1 << 6)

struct drm_mode_crtc {
    uint64_t set_connectors_ptr;
    uint32_t count_connectors;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t x;
    uint32_t y;
    uint32_t gamma_size;
    uint32_t mode_valid;
    struct drm_mode_modeinfo mode;
};

struct drm_mode_get_encoder {
    uint32_t encoder_id;
    uint32_t encoder_type;
    uint32_t crtc_id;
    uint32_t possible_crtcs;
    uint32_t possible_clones;
};

#define DRM_MODE_ENCODER_NONE   0
#define DRM_MODE_ENCODER_VIRTUAL 5

struct drm_mode_get_connector {
    uint64_t encoders_ptr;
    uint64_t modes_ptr;
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint32_t count_modes;
    uint32_t count_props;
    uint32_t count_encoders;
    uint32_t encoder_id;
    uint32_t connector_id;
    uint32_t connector_type;
    uint32_t connector_type_id;
    uint32_t connection;
    uint32_t mm_width;
    uint32_t mm_height;
    uint32_t subpixel;
    uint32_t pad;
};

#define DRM_MODE_CONNECTOR_VIRTUAL 15
#define DRM_MODE_CONNECTED         1
#define DRM_MODE_SUBPIXEL_UNKNOWN  1

struct drm_mode_get_plane_res {
    uint64_t plane_id_ptr;
    uint32_t count_planes;
};

struct drm_mode_get_plane {
    uint32_t plane_id;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t possible_crtcs;
    uint32_t gamma_size;
    uint32_t count_format_types;
    uint64_t format_type_ptr;
};

struct drm_mode_set_plane {
    uint32_t plane_id;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t flags;
    int32_t  crtc_x;
    int32_t  crtc_y;
    uint32_t crtc_w;
    uint32_t crtc_h;
    uint32_t src_x;
    uint32_t src_y;
    uint32_t src_h;
    uint32_t src_w;
};

struct drm_mode_fb_cmd {
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t depth;
    uint32_t handle;
};

struct drm_mode_fb_cmd2 {
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t flags;
    uint32_t handles[4];
    uint32_t pitches[4];
    uint32_t offsets[4];
    uint64_t modifier[4];
};

#define DRM_FORMAT_XRGB8888 0x34325258
#define DRM_FORMAT_ARGB8888 0x34325241

struct drm_mode_create_dumb {
    uint32_t height;
    uint32_t width;
    uint32_t bpp;
    uint32_t flags;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
};

struct drm_mode_map_dumb {
    uint32_t handle;
    uint32_t pad;
    uint64_t offset;
};

struct drm_mode_destroy_dumb {
    uint32_t handle;
};

struct drm_mode_crtc_page_flip {
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t flags;
    uint32_t reserved;
    uint64_t user_data;
};

#define DRM_MODE_PAGE_FLIP_EVENT 0x01
#define DRM_MODE_PAGE_FLIP_ASYNC 0x02

struct drm_mode_obj_get_properties {
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint32_t count_props;
    uint32_t obj_id;
    uint32_t obj_type;
};

#define DRM_MODE_OBJECT_CRTC      0xcccccccc
#define DRM_MODE_OBJECT_CONNECTOR 0xc0c0c0c0
#define DRM_MODE_OBJECT_ENCODER   0xe0e0e0e0
#define DRM_MODE_OBJECT_MODE      0xdededede
#define DRM_MODE_OBJECT_PROPERTY  0xb0b0b0b0
#define DRM_MODE_OBJECT_FB        0xfbfbfbfb
#define DRM_MODE_OBJECT_BLOB      0xbbbbbbbb
#define DRM_MODE_OBJECT_PLANE     0xeeeeeeee

#define DRM_PROP_NAME_LEN 32

struct drm_mode_get_property {
    uint64_t values_ptr;
    uint64_t enum_blob_ptr;
    uint32_t prop_id;
    uint32_t flags;
    char     name[DRM_PROP_NAME_LEN];
    uint32_t count_values;
    uint32_t count_enum_blobs;
};

struct drm_mode_create_blob {
    uint64_t data;
    uint32_t length;
    uint32_t blob_id;
};

struct drm_mode_destroy_blob {
    uint32_t blob_id;
};

struct drm_mode_atomic {
    uint32_t flags;
    uint32_t count_objs;
    uint64_t objs_ptr;
    uint64_t count_props_ptr;
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint64_t reserved;
    uint64_t user_data;
};

#define DRM_MODE_ATOMIC_TEST_ONLY   0x0100
#define DRM_MODE_ATOMIC_NONBLOCK    0x0200
#define DRM_MODE_ATOMIC_ALLOW_MODESET 0x0400

#define DRM_MODE_PROP_PENDING   (1 << 0)
#define DRM_MODE_PROP_RANGE     (1 << 1)
#define DRM_MODE_PROP_IMMUTABLE (1 << 2)
#define DRM_MODE_PROP_ENUM      (1 << 3)
#define DRM_MODE_PROP_BLOB      (1 << 4)
#define DRM_MODE_PROP_OBJECT    (1 << 6)
#define DRM_MODE_PROP_ATOMIC    0x80000000

struct drm_mode_get_blob {
    uint32_t blob_id;
    uint32_t length;
    uint64_t data;
};

struct drm_mode_cursor {
    uint32_t flags;
    uint32_t crtc_id;
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
    uint32_t handle;
};

struct drm_event {
    uint32_t type;
    uint32_t length;
};

struct drm_event_vblank {
    struct drm_event base;
    uint64_t user_data;
    uint32_t tv_sec;
    uint32_t tv_usec;
    uint32_t sequence;
    uint32_t crtc_id;
};

#define DRM_EVENT_VBLANK          0x01
#define DRM_EVENT_FLIP_COMPLETE   0x02

#define DRM_IOCTL_VERSION            DRM_IOWR(0x00, struct drm_version)
#define DRM_IOCTL_GET_UNIQUE         DRM_IOWR(0x01, struct drm_unique)
#define DRM_IOCTL_SET_VERSION        DRM_IOWR(0x07, struct drm_set_version)
#define DRM_IOCTL_GET_CAP            DRM_IOWR(0x0c, struct drm_get_cap)
#define DRM_IOCTL_SET_CLIENT_CAP     DRM_IOW(0x0d, struct drm_set_client_cap)
#define DRM_IOCTL_SET_MASTER         DRM_IO(0x1e)
#define DRM_IOCTL_DROP_MASTER        DRM_IO(0x1f)

#define DRM_IOCTL_MODE_GETRESOURCES  DRM_IOWR(0xA0, struct drm_mode_card_res)
#define DRM_IOCTL_MODE_GETCRTC       DRM_IOWR(0xA1, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_SETCRTC       DRM_IOWR(0xA2, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_CURSOR        DRM_IOWR(0xA3, struct drm_mode_cursor)
#define DRM_IOCTL_MODE_GETENCODER    DRM_IOWR(0xA6, struct drm_mode_get_encoder)
#define DRM_IOCTL_MODE_GETCONNECTOR  DRM_IOWR(0xA7, struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_GETPROPERTY   DRM_IOWR(0xAA, struct drm_mode_get_property)
#define DRM_IOCTL_MODE_GETPROPBLOB   DRM_IOWR(0xAC, struct drm_mode_get_blob)
#define DRM_IOCTL_MODE_GETFB         DRM_IOWR(0xAD, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_ADDFB         DRM_IOWR(0xAE, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_RMFB          DRM_IOWR(0xAF, uint32_t)
#define DRM_IOCTL_MODE_PAGE_FLIP     DRM_IOWR(0xB0, struct drm_mode_crtc_page_flip)
#define DRM_IOCTL_MODE_CREATE_DUMB   DRM_IOWR(0xB2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB      DRM_IOWR(0xB3, struct drm_mode_map_dumb)
#define DRM_IOCTL_MODE_DESTROY_DUMB  DRM_IOWR(0xB4, struct drm_mode_destroy_dumb)
#define DRM_IOCTL_MODE_GETPLANERESOURCES DRM_IOWR(0xB5, struct drm_mode_get_plane_res)
#define DRM_IOCTL_MODE_GETPLANE      DRM_IOWR(0xB6, struct drm_mode_get_plane)
#define DRM_IOCTL_MODE_SETPLANE      DRM_IOWR(0xB7, struct drm_mode_set_plane)
#define DRM_IOCTL_MODE_ADDFB2        DRM_IOWR(0xB8, struct drm_mode_fb_cmd2)
#define DRM_IOCTL_MODE_OBJ_GETPROPERTIES DRM_IOWR(0xB9, struct drm_mode_obj_get_properties)
#define DRM_IOCTL_MODE_ATOMIC        DRM_IOWR(0xBC, struct drm_mode_atomic)
#define DRM_IOCTL_MODE_CREATEPROPBLOB  DRM_IOWR(0xBD, struct drm_mode_create_blob)
#define DRM_IOCTL_MODE_DESTROYPROPBLOB DRM_IOWR(0xBE, struct drm_mode_destroy_blob)

_Static_assert(sizeof(struct drm_version) == 64, "drm_version");
_Static_assert(sizeof(struct drm_mode_card_res) == 64, "drm_mode_card_res");
_Static_assert(sizeof(struct drm_mode_modeinfo) == 68, "drm_mode_modeinfo");
_Static_assert(sizeof(struct drm_mode_crtc) == 104, "drm_mode_crtc");
_Static_assert(sizeof(struct drm_mode_get_encoder) == 20, "drm_mode_get_encoder");
_Static_assert(sizeof(struct drm_mode_get_connector) == 80, "drm_mode_get_connector");
_Static_assert(sizeof(struct drm_mode_create_dumb) == 32, "drm_mode_create_dumb");
_Static_assert(sizeof(struct drm_mode_map_dumb) == 16, "drm_mode_map_dumb");
_Static_assert(sizeof(struct drm_mode_fb_cmd) == 28, "drm_mode_fb_cmd");
_Static_assert(sizeof(struct drm_mode_fb_cmd2) == 104, "drm_mode_fb_cmd2");
_Static_assert(sizeof(struct drm_mode_crtc_page_flip) == 24, "drm_mode_crtc_page_flip");
_Static_assert(sizeof(struct drm_event_vblank) == 32, "drm_event_vblank");
_Static_assert(sizeof(struct drm_mode_get_property) == 64, "drm_mode_get_property");
_Static_assert(sizeof(struct drm_mode_obj_get_properties) == 32, "drm_mode_obj_get_properties");
_Static_assert(sizeof(struct drm_mode_get_plane) == 32, "drm_mode_get_plane");
_Static_assert(sizeof(struct drm_mode_atomic) == 56, "drm_mode_atomic");
_Static_assert(sizeof(struct drm_mode_create_blob) == 16, "drm_mode_create_blob");

#endif
