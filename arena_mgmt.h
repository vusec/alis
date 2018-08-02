/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#ifndef ALIS_ARENA_MGMT_H
#define ALIS_ARENA_MGMT_H 1

#include "arena.h"

#include <ramses/msys.h>

struct ArenaBacking {
	void *buf;
	size_t map_sz;
};

struct MasterArena {
	struct ArenaBacking backing;
	struct Arena arena;
};


struct ArenaStats {
	size_t data_pages;
	size_t guard_pages;
	size_t dropped_pages;
	size_t alloc_iterations;
};

int alis_arena_create(struct MemorySystem *msys,
                      size_t size_hint, size_t max_cont_rows,
                      struct MasterArena *ma, struct ArenaStats *stats);
int alis_arena_destroy(struct MasterArena *ma);

#endif /* arena_mgmt.h */
