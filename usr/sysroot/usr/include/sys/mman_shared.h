#ifndef _SYS_MMAN_SHARED_H
#define _SYS_MMAN_SHARED_H
#ifdef __cplusplus
extern "C" {
#endif

#define MFD_CLOEXEC       0x0001U
#define MFD_ALLOW_SEALING 0x0002U
#define MFD_HUGETLB       0x0004U

int memfd_create(const char *name, unsigned int flags);

#ifdef __cplusplus
}
#endif
#endif
