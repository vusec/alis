/*
 * Copyright (c) 2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#ifndef ALIS_ARENA_INT_H
#define ALIS_ARENA_INT_H 1

/* Updates the a->rb_pgtotals structure */
void arena_update_totals(struct Arena *a, size_t start);

#endif /* arena_int.h */
