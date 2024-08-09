/*
 * YAFFS: Yet Another Flash File System. A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2015 Aleph One Ltd.
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This software is the subject of a Licence Agreement between Aleph One Limited and Hi_Silicon_Huawei_DigiMedia.
 *
 */

/*
 * ydirectenv.h: Environment wrappers for YAFFS direct.
 */

#ifndef __YDIRECTENV_H__
#define __YDIRECTENV_H__

#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "yaffs_osglue.h"
#include "yaffs_hweight.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef CONFIG_YAFFS_USE_32_BIT_TIME_T
#define CONFIG_YAFFS_USE_32_BIT_TIME_T 1
#endif

#ifdef CONFIG_YAFFS_USE_32_BIT_TIME_T
	#define YTIME_T u32
#else
	#define YTIME_T u64
#endif

#define YCHAR char
#define YUCHAR unsigned char
#define _Y(x) x

#ifndef Y_LOFF_T
#define Y_LOFF_T loff_t
#endif

/* Some RTOSs (eg. VxWorks) need strnlen. */
size_t strnlen(const char *s, size_t maxlen);

#define yaffs_strcat(a, b)	strcat(a, b)
#define yaffs_strncat(a, b, c)	strncat(a, b, c)
#define yaffs_strcpy(a, b)	strcpy(a, b)
#define yaffs_strncpy(a, b, c)	strncpy(a, b, c)
#define yaffs_strnlen(s, m)	strnlen(s, m)
#ifdef CONFIG_YAFFS_CASE_INSENSITIVE
#define yaffs_strcmp(a, b)	strcasecmp(a, b)
#define yaffs_strncmp(a, b, c)	strncasecmp(a, b, c)
#else
#define yaffs_strcmp(a, b)	strcmp(a, b)
#define yaffs_strncmp(a, b, c)	strncmp(a, b, c)
#endif

#define hweight8(x)	yaffs_hweight8(x)
#define hweight32(x)	yaffs_hweight32(x)

void yaffs_qsort(void *aa, size_t n, size_t es,
		int (*cmp)(const void *, const void *));

#define sort(base, n, sz, cmp_fn, swp) yaffs_qsort(base, n, sz, cmp_fn)

#define YAFFS_PATH_DIVIDERS  "/"

#undef __kmalloc
#define __kmalloc(x) yaffsfs_malloc(x)
#undef kmalloc
#define kmalloc(x, flags) __kmalloc(x)

#undef __kfree
#define __kfree(x) yaffsfs_free(x)
#undef kfree
#define kfree(x) __kfree(x)

#undef __vmalloc
#define __vmalloc(x) yaffsfs_malloc(x)
#undef vmalloc
#define vmalloc(x) __vmalloc(x)

#undef __vfree
#define __vfree(x) yaffsfs_free(x)
#undef vfree
#define vfree(x) __vfree(x)

#define cond_resched()  do {} while (0)

#ifdef CONFIG_YAFFS_NO_TRACE
#define yaffs_trace(...) do { } while (0)
#else
#define yaffs_trace(msk, fmt, ...) do { \
	if (yaffs_trace_mask & (msk)) \
		PRINTK("yaffs: " fmt "\n", ##__VA_ARGS__); \
} while (0)

#endif

#define YAFFS_LOSTNFOUND_NAME		"lost+found"
#define YAFFS_LOSTNFOUND_PREFIX		"obj"

#include "yaffscfg.h"

#define Y_CURRENT_TIME yaffsfs_CurrentTime()
#define Y_TIME_CONVERT(x) x

#define YAFFS_ROOT_MODE			0666
#define YAFFS_LOSTNFOUND_MODE		0666

#include "yaffs_list.h"

#include "yaffsfs.h"


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __YDIRECTENV_H__ */
