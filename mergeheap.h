/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#ifndef ALIS_MERGEHEAP_H
#define ALIS_MERGEHEAP_H 1

#include <stddef.h>
#include <stdint.h>

struct HeapNode {
	void *head;
	size_t remain;
};

typedef uint64_t heapkey_t;
#define MAX_HEAPKEY ((heapkey_t)-1)

struct MergeHeap {
	size_t size;
	size_t top;
	size_t elem_size;
	heapkey_t (*key_fn)(const void *a);
	struct HeapNode heap[];
};

int mheap_insert(struct MergeHeap *mh, void *head, size_t rem);
void mheap_pop(struct MergeHeap *mh);
void *mheap_next(struct MergeHeap *mh);

size_t mheap_calcsize(size_t max_estimate);

#endif /* mergeheap.h */
