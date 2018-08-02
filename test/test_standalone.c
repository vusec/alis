/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */
#define _GNU_SOURCE

#include "arena.h"
#include "arena_mgmt.h"
#include "map.h"

#include <ramses/msys.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <time.h>

const size_t SZ_ = 16L * 1024 * 1024;
const size_t ALIGN = 2 * 1024 * 1024;

const char *MSYS_STR = "map:intel:ivyhaswell:2dimm:2rank:pcibase=0x7f800000:tom=0x200000000";

static struct MasterArena ma;
static size_t mapsz;
static struct MemorySystem msys;

static void *alloc_isolated(size_t sz, size_t al, struct ArenaStats *st)
{
	ticketid_t tick;
	size_t chunk_count;
	void *ret = NULL;

	if (alis_arena_create(&msys, sz, 0, &ma, st)) {
		puts("Arena create error");
		return NULL;
	} else {
		#if 0
		for (size_t i = ma.arena.rb_top; i --> 0;) {
			printf("%zu %zu\n", i, ma.arena.rb_stack[i].data_pgcnt);
		}
		#endif
	}
	const size_t offslen = (st->data_pages > st->guard_pages) ?
	                        st->data_pages : st->guard_pages;
	off_t offs[offslen];
	memset(offs, 0, offslen * sizeof(*offs));
	tick = alis_arena_reserve(&(ma.arena), 0);
	errno = 0;
	if (tick) {
		chunk_count = alis_arena_get_data(&(ma.arena), tick, offs, offslen);
		const size_t cc = (chunk_count < offslen) ? chunk_count : offslen;
		ret = alis_map(NULL, al, ma.arena.mfd,
		               offs, cc, ma.arena.page_size);
		if (ret == MAP_FAILED) {
			puts("Mapping failed");
			ret = NULL;
		} else {
			mapsz = cc * ma.arena.page_size;
		}
	} else {
		puts("Ticket reservation error");
	}
	return ret;
}

static void free_isolated(void *p)
{
	alis_unmap(p, mapsz);
	alis_arena_destroy(&ma);
}

int main(int argc, char *argv[])
{
	size_t SZ;
	if (argc > 1) {
		SZ = atoll(argv[1]) * 1024 * 1024;
	} else {
		SZ = SZ_;
	}

	struct timespec t0, t;

	int err = ramses_msys_load(MSYS_STR, &msys, NULL);
	if (err) {
		perror(ramses_msys_load_strerr(err));
		return 1;
	}
	struct ArenaStats st = {0};
	errno = 0;
	clock_gettime(CLOCK_REALTIME, &t0);
	void *p = alloc_isolated(SZ, ALIGN, &st);
	clock_gettime(CLOCK_REALTIME, &t);
	perror("");
	printf("%p\nData: %zu Guard: %zu Dropped: %zu Iters: %zu\n",
	       p, st.data_pages, st.guard_pages, st.dropped_pages, st.alloc_iterations);
	if (p != NULL) {
		puts("Freeing");
		errno = 0;
		free_isolated(p);
		perror("");
	} else {
		return 1;
	}

	double tdiff = ((t.tv_sec - t0.tv_sec) * 1.0) + ((t.tv_nsec - t0.tv_nsec) * 0.000000001);
	printf("Allocation wall time: %.3f s\n", tdiff);
	return 0;
}
