// $Id$
/*
 * MIOS32 integration helpers for FatFs.
 */

#ifndef _MIOS32_FATFS_H
#define _MIOS32_FATFS_H

#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Format logical drive 0 using FatFs defaults and a shared sector buffer. */
extern FRESULT MIOS32_FATFS_Format(void);

#ifdef __cplusplus
}
#endif

#endif /* _MIOS32_FATFS_H */
