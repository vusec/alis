/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#ifndef CEILDIV_H
#define CEILDIV_H 1

#include <stddef.h>

static inline size_t ceildiv(size_t a, size_t b)
{
	return (a / b) + ((a % b) ? 1 : 0);
}

#endif /* ceildiv.h */
