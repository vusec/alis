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
#define _GNU_SOURCE

#include "arena_mgmt.h"
#include "arena_int.h"
#include "ceildiv.h"

#include <ramses/bufmap.h>
#include <ramses/translate/pagemap.h>

#include <assert.h>
#include <alloca.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <fcntl.h>

#define GUARD_BYTE	0xAA
#define MINALEN		(32 * 1024 * 1024)
#define MA_THRESH	(128 * 1024 * 1024)

static size_t min(size_t a, size_t b)
{
	return (a <= b) ? a : b;
}

static size_t shift_alen(size_t hint, size_t shft)
{
	return hint + ((shft > 0) ? (hint << shft) : (hint >> -shft));
}

static size_t estimate_shift(size_t hint, size_t pagesize)
{
	int shft = 0;
	for (size_t x = (MA_THRESH / hint) + 1;
	     x > 0 && shft > -(MA_THRESH / pagesize);
	     shft--, x >>= 1);
	for (size_t x = MINALEN / shift_alen(hint, shft); x; shft++, x >>= 1);
	return shft;
}

static size_t getalen(size_t hint, size_t shft)
{
	size_t ret = shift_alen(hint, shft);
	return (ret >= MINALEN) ? ret : MINALEN;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
static size_t bm_get_entry_ptes(struct BufferMap *bm, size_t ri, size_t off,
                                size_t count, size_t *pte_indexes)
{
	int r;
	size_t enti = 0;
	size_t rei = off;
	while (enti < count && rei < bm->ranges[ri].entry_cnt) {
		struct DRAMAddr da = ramses_bufmap_addr(bm, ri, rei);
		physaddr_t pa = ramses_resolve_reverse(bm->msys, da);
		r = ramses_bufmap_find_pte(bm, pa, &pte_indexes[enti]);
		assert(!r);
		enti++;
		rei++;
	}
	return enti;
}
#pragma GCC diagnostic pop

static int ape_pa_cmp(const void *a, const void *b)
{
	physaddr_t pa = ((struct ArenaPageEntry *)a)->pa;
	physaddr_t pb = ((struct ArenaPageEntry *)b)->pa;
	return (pa == pb) ? 0 : ((pa < pb) ? -1 : 1);
}

static int rb_datalen_cmp(const void *a, const void *b)
{
	size_t al = ((struct RowBlock *)a)->data_pgcnt;
	size_t bl = ((struct RowBlock *)b)->data_pgcnt;
	return (al == bl) ? 0 : (al < bl) ? -1 : 1;
}


#define PTE_UNSAFE      0x1
#define PTE_EDGE        0x2
#define PTE_GUARD_PRE   0x4
#define PTE_GUARD_POST  0x8
#define PTE_ROWBLOCK    0x10
#define PTE_VISIT       0x0 /* Impromptu debug flag if non-zero */
typedef uint8_t pteflag_t;

static size_t mark(pteflag_t *pte_flags, struct BufferMap *bm,
                   size_t ri, size_t ei, size_t ecnt, pteflag_t flags)
{
	const size_t MAXENTS = 2048;

	size_t ret = 0;
	if (flags && ecnt) {
		size_t em = 0;
		while (em < ecnt) {
			const size_t entstack_sz = min(ecnt - em, MAXENTS);
			size_t pteis[entstack_sz];
			size_t ec = bm_get_entry_ptes(bm, ri, ei + em, entstack_sz, pteis);
			assert(ec == entstack_sz);
			for (size_t i = 0; i < ec; i++) {
				if (!((pte_flags[pteis[i]] & flags) == flags)) {
					pte_flags[pteis[i]] |= flags;
					ret++;
				}
			}
			em += ec;
		}
	}
	return ret;
}


static size_t pass1(struct BufferMap *bm, pteflag_t *pte_flags)
{
	const struct MappingProps mprops = bm->msys->mapping.props;
	const size_t rowlen = mprops.col_cnt * mprops.cell_size;
	const size_t epr = rowlen / bm->entry_len;
	assert((rowlen % bm->entry_len) == 0);

	size_t maxecnt = 0;
	for (size_t ri = 0; ri < bm->range_cnt; ri++) {
		int s = 0;
		size_t ei = 0;
		size_t ecnt = bm->ranges[ri].entry_cnt;
		if (ecnt > maxecnt) {
			maxecnt = ecnt;
		}
		if (bm->ranges[ri].start.col != 0) {
			size_t ents_left = ((mprops.col_cnt - bm->ranges[ri].start.col) *
			                   mprops.cell_size) / bm->entry_len;
			ents_left = (ents_left < ecnt) ? ents_left : ecnt;
			mark(pte_flags, bm, ri, ei, ents_left, PTE_UNSAFE | PTE_VISIT);
			ei += ents_left;
		}
		/* ei at start of row */
		while (ei < ecnt) {
			size_t rem = ecnt - ei;
			if (s) { /* S1 */
				if (rem >= epr) { /* (it) = F */
					if (rem >= (2*epr)) { /* (it+1) = F */
						#if (PTE_VISIT)
						mark(pte_flags, bm, ri, ei, epr, PTE_VISIT);
						#endif
						ei += epr;
					} else { /* (it+1) = E,I */
						mark(pte_flags, bm, ri, ei, epr, PTE_EDGE | PTE_VISIT);
						ei += epr;
						s = 0;
					}
				} else { /* (it) = E */
					s = 0;
				}
			} else { /* S0 */
				if (rem >= epr) { /* (it) = F */
					mark(pte_flags, bm, ri, ei, epr, PTE_EDGE | PTE_VISIT);
					ei += epr;
					s = 1;
				} else { /* (it) = I */
					mark(pte_flags, bm, ri, ei, rem, PTE_UNSAFE | PTE_VISIT);
					ei = ecnt;
				}
			}
			/* ei == ecnt when (it) = E */
		}
	}
	return maxecnt;
}

struct pass2_stats {
	size_t rb_top;
	size_t data_pge_top;
	size_t guard_pge_top;
};

static struct pass2_stats pass2(struct BufferMap *bm, pteflag_t *pte_flags,
                                size_t maxecnt, size_t max_rows_per_block,
                                struct RowBlock *rb_stack,
                                struct ArenaPageEntry *dpgents,
                                struct ArenaPageEntry *gpgents)
{
	size_t *pteis;
	const size_t MAXSTACKENTS = 2 * bm->page_size / sizeof(*pteis);
	const size_t epr = ramses_bufmap_epr(bm);
	const size_t max_rbecnt = max_rows_per_block * epr;
	assert((ramses_bufmap_rowlen(bm) % bm->entry_len) == 0);

	if (maxecnt > MAXSTACKENTS) {
		pteis = malloc(maxecnt * sizeof(*pteis));
	} else {
		pteis = alloca(maxecnt * sizeof(*pteis));
	}

	size_t rb_top = 0;
	size_t dpge_base = 0;
	size_t dpge_top = 0;
	size_t gpge_base = 0;
	size_t gpge_top = 0;

	for (size_t ri = 0; ri < bm->range_cnt; ri++) {
		const size_t ecnt = min(bm->ranges[ri].entry_cnt, maxecnt);
		size_t ec = bm_get_entry_ptes(bm, ri, 0, ecnt, pteis);
		assert(ec == ecnt);
		size_t rbecnt = 0;
		for (size_t ei = 0; ei < ec; ei++) {
			pteflag_t cur_flags = pte_flags[pteis[ei]];
			if (!(cur_flags & (PTE_UNSAFE | PTE_EDGE | PTE_GUARD_PRE | PTE_GUARD_POST)) &&
			    (max_rbecnt == 0 || rbecnt < max_rbecnt))
			{
				if (!(cur_flags & PTE_ROWBLOCK)) {
					if (dpge_top == dpge_base) {
						/* First page in row block, mark prev row(s) GUARD */
						assert(ei >= epr);
						for (size_t gei = ei - epr; gei < ei; gei++) {
							if (!(pte_flags[pteis[gei]] & PTE_GUARD_PRE)) {
								pte_flags[pteis[gei]] |= PTE_GUARD_PRE;
								gpgents[gpge_top] = ((struct ArenaPageEntry){
									.pa = bm->ptes[pteis[gei]].pa,
									.mfd_off = bm->ptes[pteis[gei]].va - (uintptr_t)bm->bufbase
								});
								gpge_top++;
							}
						}
					}
					pte_flags[pteis[ei]] |= PTE_ROWBLOCK;
					/* Add page to dpgents */
					dpgents[dpge_top] = ((struct ArenaPageEntry){
						.pa = bm->ptes[pteis[ei]].pa,
						.mfd_off = bm->ptes[pteis[ei]].va - (uintptr_t)bm->bufbase
					});
					dpge_top++;
				}
				rbecnt++;
			} else {
				if (dpge_top > dpge_base) {
					/* Finished assembling row block, mark next row(s) GUARD */
					assert(ei + epr <= ecnt);
					for (size_t gei = ei; gei < ei + epr; gei++) {
						if (!(pte_flags[pteis[gei]] & PTE_GUARD_POST)) {
							pte_flags[pteis[gei]] |= PTE_GUARD_POST;
							gpgents[gpge_top] = ((struct ArenaPageEntry){
								.pa = bm->ptes[pteis[gei]].pa,
								.mfd_off = bm->ptes[pteis[gei]].va - (uintptr_t)bm->bufbase
							});
							gpge_top++;
						}
					}

					rb_stack[rb_top] = ((struct RowBlock){
						.data_pgcnt = dpge_top - dpge_base,
						.data_pgents_off = dpge_base,
						.guard_pgcnt = gpge_top - gpge_base,
						.guard_pgents_off = gpge_base
					});
					rb_top++;
					rbecnt = 0;
					qsort(dpgents + dpge_base, dpge_top - dpge_base, sizeof(*dpgents), ape_pa_cmp);
					dpge_base = dpge_top;
					qsort(gpgents + gpge_base, gpge_top - gpge_base, sizeof(*gpgents), ape_pa_cmp);
					gpge_base = gpge_top;
				}
			}
		}
		assert(dpge_top == dpge_base);
	}
	if (maxecnt > MAXSTACKENTS) {
		free(pteis);
	}
	qsort(rb_stack, rb_top, sizeof(*rb_stack), rb_datalen_cmp);
	return ((struct pass2_stats){rb_top, dpge_top, gpge_top});
}


int alis_arena_create(struct MemorySystem *msys,
                      size_t size_hint, size_t max_cont_rows,
                      struct MasterArena *ma, struct ArenaStats *stats)
{
	int pagemap_fd;
	struct Translation trans;
	struct BufferMap bm;
	pteflag_t *pte_flags = NULL;

	int mfd;
	void *buf;
	size_t alen;
	size_t itcnt;
	size_t dpcnt;
	size_t gpcnt;
	size_t xpcnt;

	struct RowBlock *rb_stack = NULL;
	struct ArenaPageEntry *dpgents = NULL;
	struct ArenaPageEntry *gpgents = NULL;
	size_t *pgtotals = NULL;
	ticketid_t *tickmap = NULL;

	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd == -1) {
		return 1;
	}
	ramses_translate_pagemap(&trans, pagemap_fd);

	const size_t PAGE_SIZE = ramses_translate_granularity(&trans);
	const size_t minpc = ceildiv(size_hint, PAGE_SIZE);
	int shift = (size_hint > MA_THRESH) ? estimate_shift(size_hint, PAGE_SIZE) : 1;

	for(itcnt = 0; ; itcnt++) {
		/* Prepare backing buffer */
		alen = getalen(size_hint, shift + itcnt);
		mfd = syscall(SYS_memfd_create, "AlisArenaBacking", 0);
		if (mfd < 0) {
			break;
		}
		if (ftruncate(mfd, alen) != 0) {
			goto err_close;
		}
		buf = mmap(NULL, alen, PROT_READ|PROT_WRITE, MAP_SHARED, mfd, 0);
		if (buf == MAP_FAILED) {
			goto err_close;
		}
		madvise(buf, alen, MADV_HUGEPAGE);
		if (mlock(buf, alen) != 0) {
			goto err_unmap;
		}

		/* Prepare temporary data structures */
		if (ramses_bufmap(&bm, buf, alen, &trans, msys, 0) != 0) {
			goto err_unmap;
		}
		pte_flags = calloc(bm.pte_cnt, sizeof(*pte_flags));
		if (pte_flags == NULL) {
			goto err_freebm;
		}

		size_t maxecnt = pass1(&bm, pte_flags);

		/* Check if it's possible to satisfy allocation hint */
		dpcnt = bm.pte_cnt;
		for (size_t pi = 0; pi < bm.pte_cnt; pi++) {
			if (pte_flags[pi] & (PTE_UNSAFE | PTE_EDGE)) {
				dpcnt--;
				if (dpcnt < minpc) {
					break;
				}
			}
		}
		if (dpcnt < minpc) {
			goto cont_postpass1;
		}

		/* Set up aux data structures */
		dpgents = malloc(dpcnt * sizeof(*dpgents));
		gpgents = malloc(2 * dpcnt * sizeof(*gpgents));
		const size_t maxrbs = dpcnt /
			((bm.msys->mapping.props.col_cnt * bm.msys->mapping.props.cell_size) / bm.page_size);
		rb_stack = malloc(maxrbs * sizeof(*rb_stack));
		if (dpgents == NULL || gpgents == NULL || rb_stack == NULL) {
			goto err_freeaux;
		}

		struct pass2_stats p2s = pass2(&bm, pte_flags, maxecnt, max_cont_rows,
		                               rb_stack, dpgents, gpgents);
		if (p2s.data_pge_top < minpc) {
			goto cont_postpass2;
		}

		dpgents = realloc(dpgents, p2s.data_pge_top * sizeof(*dpgents));
		gpgents = realloc(gpgents, p2s.guard_pge_top * sizeof(*gpgents));
		rb_stack = realloc(rb_stack, p2s.rb_top * sizeof(*rb_stack));

		/* Fill data & guard pages, discard unusable pages and collect stats */
		dpcnt = 0;
		gpcnt = 0;
		xpcnt = 0;
		for (size_t i = 0; i < bm.pte_cnt; i++) {
			pteflag_t f = pte_flags[i];
			if (f & PTE_ROWBLOCK) {
				memset((void *)bm.ptes[i].va, 0, bm.page_size);
				dpcnt++;
			} else if (f & (PTE_GUARD_PRE | PTE_GUARD_POST)) {
				memset((void *)bm.ptes[i].va, GUARD_BYTE, bm.page_size);
				gpcnt++;
			} else {
				madvise((void *)bm.ptes[i].va, bm.page_size, MADV_REMOVE);
				munmap((void *)bm.ptes[i].va, bm.page_size);
				xpcnt++;
			}
		}
		free(pte_flags);
		pte_flags = NULL;

		/* Writeout */
		pgtotals = calloc(p2s.rb_top, sizeof(*pgtotals));
		tickmap = calloc(p2s.rb_top, sizeof(*tickmap));
		if (pgtotals == NULL || tickmap == NULL) {
			goto err_freeout;
		}
		ma->backing.buf = buf;
		ma->backing.map_sz = alen;
		ma->arena = ((struct Arena){
			.page_size = PAGE_SIZE,
			.rb_stack = rb_stack,
			.rb_top = p2s.rb_top,
			.rb_pgtotals = pgtotals,
			.rb_tickmap = tickmap,
			.data_pgents = dpgents,
			.data_pgents_size = p2s.data_pge_top,
			.guard_pgents = gpgents,
			.guard_pgents_size = p2s.guard_pge_top,
			.last_ticket = 0,
			.mfd = mfd
		});
		arena_update_totals(&(ma->arena), 0);
		if (stats != NULL) {
			stats->data_pages = dpcnt;
			stats->guard_pages = gpcnt;
			stats->dropped_pages = xpcnt;
			stats->alloc_iterations = itcnt + 1;
		}
		ramses_bufmap_free(&bm);
		close(pagemap_fd);
		return 0;

		/* Non-fatal errors; clean up and retry */
	cont_postpass2:
		free(dpgents);
		free(gpgents);
		free(rb_stack);
	cont_postpass1:
		free(pte_flags);
		ramses_bufmap_free(&bm);
		munmap(buf, alen);
		close(mfd);
		errno = 0;
		continue;

		/* Fatal errors; clean up and bail out */
	err_freeout:
		free(pgtotals);
		free(tickmap);
	err_freeaux:
		free(dpgents);
		free(gpgents);
		free(rb_stack);
		free(pte_flags);
	err_freebm:
		ramses_bufmap_free(&bm);
	err_unmap:
		munmap(buf, alen);
	err_close:
		close(mfd);
		break;
	}
	close(pagemap_fd);
	return 1;
}

int alis_arena_destroy(struct MasterArena *ma)
{
	int r;
	free(ma->arena.rb_stack);
	free(ma->arena.rb_tickmap);
	free(ma->arena.data_pgents);
	free(ma->arena.guard_pgents);
	r = close(ma->arena.mfd);
	if (!r) {
		r |= munmap(ma->backing.buf, ma->backing.map_sz);
	}
	return r;
}
