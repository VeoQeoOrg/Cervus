#ifndef _KERNEL_NET_NE2000_H
#define _KERNEL_NET_NE2000_H

// Driver implemented in Cinder, so the compiler prefixes exported symbols with cinder_
void cinder_ne2000_init(void);
void cinder_ne2000_start_worker(void);

#endif
