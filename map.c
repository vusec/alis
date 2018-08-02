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

#include "map.h"

#include <stdint.h>
#include <assert.h>

#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>

static void *sys_mmap(void *addr, size_t length, int prot, int flags,
                      int fd, off_t offset)
{
	return (void *)syscall(SYS_mmap, addr, length, prot, flags, fd, offset);
}

static int sys_munmap(void *addr, size_t len)
{
	return (int)syscall(SYS_munmap, addr, len);
}

static void *mapalign(void *addr, size_t sz, size_t align)
{
	size_t reqsz = align ? sz + align : sz;
	void *buf = sys_mmap(addr, reqsz, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (buf != MAP_FAILED && align) {
		size_t err = ((uintptr_t)buf % align);
		size_t left = err ? align - err : 0;
		size_t right = align - left;
		sys_munmap(buf, left);
		buf = (void *)((uintptr_t)buf + left);
		assert((uintptr_t)buf % align == 0);
		sys_munmap((void *)((uintptr_t)buf + sz), right);
	}
	return buf;
}

void *alis_map(void *addr, size_t align, int mfd, off_t *offsets,
               size_t chunk_count, size_t chunk_size)
{
	size_t sz = chunk_count * chunk_size;
	void *m = mapalign(addr, sz, align);
	if (m != MAP_FAILED) {
		uintptr_t cur = (uintptr_t)m;
		for (size_t i = 0; i < chunk_count; i++) {
			cur = (uintptr_t)sys_mmap((void *)cur, chunk_size,
			                          PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED,
			                          mfd, offsets[i]);
			if ((void *)cur == MAP_FAILED) {
				(void) sys_munmap(m, sz);
				m = MAP_FAILED;
				break;
			} else {
				cur += chunk_size;
			}
		}
	}
	return m;
}

int alis_unmap(void *addr, size_t len)
{
	return sys_munmap(addr, len);
}
