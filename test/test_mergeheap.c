/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#include "mergeheap.h"

#include <stdio.h>
#include <stdlib.h>
#include <alloca.h>

static heapkey_t intkey(const void *a) {return (heapkey_t)*((int *)a);}
static void tassert(int c) {if (!c) exit(1);}

int main(int argc, char *argv[])
{
	srand(0x1337);

	int max = (argc > 1) ? atoi(argv[1]) : 127;
	int slen = (argc > 2) ? atoi(argv[2]) : 512;
	int vec[max][slen];
	for (int i = 0; i < max; i++) {
		int s = rand() & 0xffff;
		for (int j = 0; j < slen; j++) {
			vec[i][j] = s + (j * 200);
			//~ printf("%5zu ", intkey(vec[i] + j));
		}
		//~ putchar('\n');
	}
	//~ puts("---------------");
	//~ return 0;
	size_t hsz = mheap_calcsize(max);
	struct MergeHeap *m = alloca(sizeof(*m) + hsz * sizeof(struct HeapNode));
	m->size = hsz;
	m->top = 0;
	m->elem_size = sizeof(int);
	m->key_fn = intkey;
	for (int i = 0; i < max; i++) {
		mheap_insert(m, vec[i], slen);
	}
	//~ for (int i = 0; i < max; i++) {
		//~ printf("%5zu ", intkey(m->heap[i].head));
	//~ }
	//~ putchar('\n');
	//~ puts("---------------");
	int *p;
	int last = 0;
	while ((p = (int *)mheap_next(m))) {
		//~ printf("%5d ", *p);
		tassert(last <= *p);
		last = *p;
	}
	//~ putchar('\n');
	return 0;
}
