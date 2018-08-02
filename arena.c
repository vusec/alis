/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "arena.h"
#include "ceildiv.h"
#include "mergeheap.h"

#include <ramses/binsearch.h>

#include <alloca.h>
#include <assert.h>


static int rb_data_pgcnt_cmp(const void *rba, const void *rbb)
{
	size_t a = ((struct RowBlock *)rba)->data_pgcnt;
	size_t b = ((struct RowBlock *)rbb)->data_pgcnt;
	return (a == b) ? 0 : ((a < b) ? -1 : 1);
}

static heapkey_t ape_phys_key(const void *ape)
{
	return (heapkey_t)((struct ArenaPageEntry *)ape)->pa;
}


void arena_update_totals(struct Arena *a, size_t start)
{
	if (start == 0) {
		a->rb_pgtotals[0] = (a->rb_tickmap[0] == 0) ? a->rb_stack[0].data_pgcnt : 0;
		start++;
	}
	for (size_t i = start; i < a->rb_top; i++) {
		a->rb_pgtotals[i] = a->rb_pgtotals[i-1] +
		                    ((a->rb_tickmap[i] == 0) ? a->rb_stack[i].data_pgcnt : 0);
	}
}

ticketid_t alis_arena_reserve(struct Arena *a, size_t size)
{
	size_t pgcnt;
	if (size) {
		pgcnt = ceildiv(size, a->page_size);
	} else {
		pgcnt = a->rb_pgtotals[a->rb_top - 1];
	}
	if (a->rb_top > 0 && a->last_ticket < TICKET_MAX &&
	    pgcnt <= a->rb_pgtotals[a->rb_top - 1])
	{
		/* Locate best starting point */
		size_t sp;
		if (a->rb_stack[a->rb_top - 1].data_pgcnt <= pgcnt) {
			sp = a->rb_top - 1;
		} else {
			struct RowBlock refrb = {pgcnt, 0, 0, 0};
			bool found = binsearch(&refrb, a->rb_stack, a->rb_top,
			                       sizeof(*a->rb_stack), rb_data_pgcnt_cmp, &sp);
			if (!found) {
				while (a->rb_pgtotals[sp] < pgcnt) {
					sp++;
				}
				if ((a->rb_stack[sp+1].data_pgcnt / pgcnt) <
				    (pgcnt / a->rb_stack[sp].data_pgcnt))
				{
					sp++;
				}
			}
		}
		/* Perform reservation */
		size_t allocd = 0;
		a->last_ticket++;
		ticketid_t tkid = a->last_ticket;
		while (allocd < pgcnt) {
			if (a->rb_tickmap[sp] == 0) {
				allocd += a->rb_stack[sp].data_pgcnt;
				a->rb_tickmap[sp] = tkid;
			}
			if (sp == 0) {
				break;
			} else {
				sp--;
			}
		}
		assert(allocd >= pgcnt);
		arena_update_totals(a, sp);
		return tkid;
	} else {
		return 0;
	}
}

enum writeval {
	MFD_OFF,
	PHYS_ADDR
};

static void writeout(struct MergeHeap *mh, enum writeval wval,
                     void *out, size_t max_chunks)
{
	struct ArenaPageEntry *last = NULL;
	struct ArenaPageEntry *p = (struct ArenaPageEntry *)mheap_next(mh);
	for (size_t i = 0;
	     p && i < max_chunks;
	     p = (struct ArenaPageEntry *)mheap_next(mh))
	{
		if (!last || last->pa != p->pa) {
			switch (wval) {
				case MFD_OFF:
					((off_t *)out)[i++] = p->mfd_off;
					break;
				case PHYS_ADDR:
					((physaddr_t *)out)[i++] = p->pa;
					break;
			}
		}
		last = p;
	}
}

enum chunktype {
	DATA_CHUNKS,
	GUARD_CHUNKS
};

static size_t fill_mergeheap(struct Arena *a, ticketid_t ticket, size_t sp,
                             enum chunktype ct, struct MergeHeap *mh)
{
	size_t totalchunks = 0;
	do {
		if (a->rb_tickmap[sp] == ticket) {
			switch (ct) {
				case DATA_CHUNKS:
					mheap_insert(mh,
					             &(a->data_pgents[a->rb_stack[sp].data_pgents_off]),
					             a->rb_stack[sp].data_pgcnt);
					totalchunks += a->rb_stack[sp].data_pgcnt;
					break;
				case GUARD_CHUNKS:
					mheap_insert(mh,
					             &(a->guard_pgents[a->rb_stack[sp].guard_pgents_off]),
					             a->rb_stack[sp].guard_pgcnt);
					totalchunks += a->rb_stack[sp].guard_pgcnt;
					break;
				default:
					return 0;
			}
		}
	} while (sp-- > 0);
	return totalchunks;
}

static size_t get_chunks(struct Arena *a, ticketid_t ticket, enum chunktype ct,
                         enum writeval wval, void *outbuf, size_t max_chunks)
{
	size_t sp;
	for (sp = a->rb_top - 1; sp && a->rb_tickmap[sp] != ticket; sp--);
	if (a->rb_tickmap[sp] == ticket) {
		/* Prepare merge heap */
		const size_t heapsz = mheap_calcsize(sp + 1);
		struct MergeHeap *mh = alloca(sizeof(*mh) + heapsz * sizeof(*mh->heap));
		mh->size = heapsz;
		mh->top = 0;
		mh->elem_size = sizeof(struct ArenaPageEntry);
		mh->key_fn = ape_phys_key;
		size_t totalchunks = fill_mergeheap(a, ticket, sp, ct, mh);
		writeout(mh, wval, outbuf, max_chunks);
		return totalchunks;
	} else {
		return 0;
	}
}

size_t alis_arena_get_data(struct Arena *a, ticketid_t ticket,
                           off_t *offsets, size_t max_chunks)
{
	return get_chunks(a, ticket, DATA_CHUNKS, MFD_OFF, offsets, max_chunks);
}

size_t alis_arena_get_guard(struct Arena *a, ticketid_t ticket,
                            off_t *offsets, size_t max_chunks)
{
	return get_chunks(a, ticket, GUARD_CHUNKS, MFD_OFF, offsets, max_chunks);
}

size_t alis_arena_get_data_physaddr(struct Arena *a, ticketid_t ticket,
                                    physaddr_t *addrs, size_t max_chunks)
{
	return get_chunks(a, ticket, DATA_CHUNKS, PHYS_ADDR, addrs, max_chunks);
}

size_t alis_arena_get_guard_physaddr(struct Arena *a, ticketid_t ticket,
                                     physaddr_t *addrs, size_t max_chunks)
{
	return get_chunks(a, ticket, GUARD_CHUNKS, PHYS_ADDR, addrs, max_chunks);
}

void alis_arena_release(struct Arena *a, ticketid_t ticket)
{
	size_t min = a->rb_top;
	for (size_t i = a->rb_top; i --> 0;) {
		if (a->rb_tickmap[i] == ticket) {
			a->rb_tickmap[i] = 0;
			min = i;
		}
	}
	arena_update_totals(a, min);
}
