#ifndef _CERVUS_MUSL_H
#define _CERVUS_MUSL_H

#define hidden __attribute__((__visibility__("hidden")))
#define weak_alias(old, new) extern __typeof(old) new __attribute__((__weak__, __alias__(#old)))
#define __strchrnul strchrnul

#endif
