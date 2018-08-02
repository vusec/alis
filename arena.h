/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#ifndef ALIS_ARENA_H
#define ALIS_ARENA_H 1

#include <ramses/types.h>

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

struct ArenaPageEntry {
	physaddr_t pa;
	off_t mfd_off;
};

struct RowBlock {
	size_t data_pgcnt;
	size_t data_pgents_off;
	size_t guard_pgcnt;
	size_t guard_pgents_off;
};

#define TICKET_MAX 0xffff
typedef uint16_t ticketid_t;

struct Arena {
	size_t page_size;

	struct RowBlock *rb_stack;
	size_t rb_top;
	struct ArenaPageEntry *data_pgents;
	size_t data_pgents_size;
	struct ArenaPageEntry *guard_pgents;
	size_t guard_pgents_size;

	size_t *rb_pgtotals;
	ticketid_t *rb_tickmap;
	ticketid_t last_ticket;
	int mfd;
};

/*
 * Reserve an isolated area of memory of minimum length `size'.
 * If `size' is 0, reserves all free pages in the arena.
 *
 * On success, returns a non-zero ticket id associated with the reservation.
 * On failure or if arena is full, returns 0.
 */
ticketid_t alis_arena_reserve(struct Arena *arena, size_t size);

/*
 * Obtain the data pages reserved by `ticket'.
 * Stores in `*offsets' up to `max_chunks' offsets into the mfd, which need
 * to be mapped into a process' address space; returns the *total* number of
 * pages reserved, which may be greater than `max_chunks'.
 */
size_t alis_arena_get_data(struct Arena *arena, ticketid_t ticket,
                           off_t *offsets, size_t max_chunks);
/*
 * Obtain the guard pages reserved by `ticket'.
 * Stores in `*offsets' up to `max_chunks' offsets into the mfd, which need
 * to be mapped into a process' address space; returns the *total* number of
 * pages reserved, which may be greater than `max_chunks'.
 */
size_t alis_arena_get_guard(struct Arena *arena, ticketid_t ticket,
                            off_t *offsets, size_t max_chunks);
/* Like alis_arena_get_data, returns physical addresses instead of mfd offsets */
size_t alis_arena_get_data_physaddr(struct Arena *a, ticketid_t ticket,
                                    physaddr_t *addrs, size_t max_chunks);
/* Like alis_arena_get_guard, returns physical addresses instead of mfd offsets */
size_t alis_arena_get_guard_physaddr(struct Arena *a, ticketid_t ticket,
                                     physaddr_t *addrs, size_t max_chunks);
/*
 * Release the data and guard pages associated with the reservation identified
 * by `ticket'.
 */
void alis_arena_release(struct Arena *a, ticketid_t ticket);

#endif /* arena.h */
