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

#include "mergeheap.h"

#include <assert.h>
#include <stdint.h>

#define PARENT(x) (((x) - 1) / 2)
#define LEFT(x)  ((2 * (x)) + 1)
#define RIGHT(x) ((2 * (x)) + 2)
#define KEY(m,x) ((m)->key_fn((m)->heap[(x)].head))

static inline void mheap_swap(struct MergeHeap *mh, size_t a, size_t b)
{
	struct HeapNode tmp = mh->heap[a];
	mh->heap[a] = mh->heap[b];
	mh->heap[b] = tmp;
}

static void mheap_siftup(struct MergeHeap *mh)
{
	if (mh->top) {
		size_t p = mh->top - 1;
		while (p && KEY(mh, PARENT(p)) > KEY(mh, p)) {
			mheap_swap(mh, p, PARENT(p));
			p = PARENT(p);
		}
	}
}

static void mheap_siftdown(struct MergeHeap *mh)
{
	if (mh->top) {
		size_t p = 0;
		while (LEFT(p) < mh->top) {
			heapkey_t pkey = KEY(mh, p);
			heapkey_t lkey = KEY(mh, LEFT(p));
			heapkey_t rkey = RIGHT(p) < mh->top ? KEY(mh, RIGHT(p)) : MAX_HEAPKEY;
			if (pkey <= lkey && pkey <= rkey) {
				break;
			} else {
				size_t runt = lkey <= rkey ? LEFT(p) : RIGHT(p);
				mheap_swap(mh, p, runt);
				p = runt;
			}
		}
	}
}

#undef PARENT
#undef LEFT
#undef RIGHT
#undef KEY

int mheap_insert(struct MergeHeap *mh, void *p, size_t rem)
{
	if (mh->top < mh->size && rem) {
		mh->heap[mh->top].head = p;
		mh->heap[mh->top].remain = rem;
		mh->top++;
		mheap_siftup(mh);
		return 0;
	} else {
		return 1;
	}
}

void mheap_pop(struct MergeHeap *mh)
{
	if (mh->top > 1) {
		mh->top--;
		mh->heap[0] = mh->heap[mh->top];
		mheap_siftdown(mh);
	} else {
		mh->top = 0;
	}
}

void *mheap_next(struct MergeHeap *mh)
{
	void *r = NULL;
	if (mh->top) {
		assert(mh->heap->remain);
		r = mh->heap->head;
		mh->heap->remain--;
		mh->heap->head = (void *)((uintptr_t)mh->heap->head + mh->elem_size);
		if (mh->heap->remain) {
			mheap_siftdown(mh);
		} else {
			mheap_pop(mh);
		}
	}
	return r;
}

size_t mheap_calcsize(size_t max_estimate)
{
	size_t heapsz = 1;
	while (max_estimate) {
		max_estimate >>= 1;
		heapsz <<= 1;
	}
	return heapsz;
}
