/**
 * \file z-virt.c
 * \brief Memory management routines
 *
 * Copyright (c) 1997 Ben Harrison.
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */
#include "z-virt.h"
#include "z-util.h"

//#define AMIGAMEM
#ifdef AMIGAMEM
#include <proto/exec.h>
#endif

unsigned int mem_flags = 0;

#define SZ(uptr)	*((size_t *)((char *)(uptr) - sizeof(size_t)))

/**
 * Allocate `len` bytes of memory.
 *
 * Returns:
 *  - NULL if `len` == 0; or
 *  - a pointer to a block of memory of at least `len` bytes
 *
 * Doesn't return on out of memory.
 */
void *mem_alloc(size_t len)
{
	char *mem;

//	static int c=0;
//	printf("mem_alloc: %d %d\n", ++c, len);
	/* Allow allocation of "zero bytes" */
	if (len == 0) return (NULL);

#ifdef AMIGAMEM
	mem = AllocVec(len + sizeof(size_t), MEMF_ANY);
#else
	mem = malloc(len + sizeof(size_t));
#endif
	if (!mem)
		quit("Out of Memory!");
	mem += sizeof(size_t);
	if (mem_flags & MEM_POISON_ALLOC)
		memset(mem, 0xCC, len);
	SZ(mem) = len;

	return mem;
}

void *mem_zalloc(size_t len)
{
//	static int c=0;
//	printf("mem_zalloc: %d\n", ++c);
	void *mem = mem_alloc(len);
	memset(mem, 0, len);
	return mem;
}

void mem_free(void *p)
{
//	static int c=0;
//	printf("mem_free: %d\n", ++c);

	if (!p) return;

	if (mem_flags & MEM_POISON_FREE)
		memset(p, 0xCD, SZ(p));
#ifdef AMIGAMEM
	FreeVec((char *)p - sizeof(size_t));
#else
	free((char *)p - sizeof(size_t));
#endif
}

#ifdef AMIGAMEM
void *arealloc(void *ptr, size_t size)
{
	if (!ptr) return ptr;
	char *newmem = AllocVec(size, MEMF_ANY);
	if (newmem) {
		size_t oldsize = *((size_t *)ptr);
		size-=sizeof(size_t);
//		printf("oldsize: %d -> %d\n", oldsize, size);
		int min = oldsize;
		if (size<min) min = size;
		memcpy((void *)((char *)newmem+sizeof(size_t)), (void *)((char *)ptr+sizeof(size_t)), min);
		FreeVec(ptr);
	}
	return newmem;
}
#endif

void *mem_realloc(void *p, size_t len)
{
	char *m = p;

//	static int c=0;
//	printf("mem_realloc: %d %d\n", ++c, len);
	/* Fail gracefully */
	if (len == 0) return (NULL);
#ifdef AMIGAMEM
	m = arealloc(m ? m - sizeof(size_t) : NULL, len + sizeof(size_t));
#else
	m = realloc(m ? m - sizeof(size_t) : NULL, len + sizeof(size_t));
#endif
	/* Handle OOM */
	if (!m) quit("Out of Memory!");

	m += sizeof(size_t);
	SZ(m) = len;

	return m;
}

/**
 * Duplicates an existing string `str`, allocating as much memory as necessary.
 */
char *string_make(const char *str)
{
	char *res;
	size_t siz;

	/* Error-checking */
	if (!str) return NULL;

	/* Allocate space for the string (including terminator) */
	siz = strlen(str) + 1;
	res = mem_alloc(siz);

	/* Copy the string (with terminator) */
	my_strcpy(res, str, siz);

	return res;
}

void string_free(char *str)
{
	mem_free(str);
}

char *string_append(char *s1, const char *s2)
{
	u32b len;
	if (!s1 && !s2) {
		return NULL;
	} else if (s1 && !s2) {
		return s1;
	} else if (!s1 && s2) {
		return string_make(s2);
	}
	len = strlen(s1);
	s1 = mem_realloc(s1, len + strlen(s2) + 1);
	my_strcpy(s1 + len, s2, strlen(s2) + 1);
	return s1;
}
