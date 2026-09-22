#ifndef _STDBOOL_H
#define _STDBOOL_H
#ifdef __cplusplus
extern "C" {
#endif
#ifndef __cplusplus
#define bool  _Bool
#define true  1
#define false 0
#endif
#define __bool_true_false_are_defined 1
#ifdef __cplusplus
}
#endif
#endif