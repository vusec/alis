/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#ifndef ALIS_MAP_H
#define ALIS_MAP_H 1

#include <stddef.h>
#include <sys/types.h>
#include <sys/mman.h>

void *alis_map(void *addr, size_t align, int mfd, off_t *offsets,
               size_t chunk_count, size_t chunk_size);
int alis_unmap(void *addr, size_t len);

#endif /* map.h */
