/**
 * @file
 * @brief A general purpose arena strategy that is based on malloc - header file.
 * @see core_arena.c
 */
#ifndef REAL_ARENA_H
/* Copyright (C) 
 * 2023 - McUsr
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 * 
 */
#define REAL_ARENA_H
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/sysinfo.h>
#include <unistd.h>

typedef unsigned int uint_32;

/** Should maybe be adjusted to 16 if you use long double.
 * see: https://www.codesynthesis.com/~boris/blog/2009/04/06/cxx-data-alignment-portability */
#define MAX_ALIGN 16
/** the size of the pointer malloc needs into the memory block, probably same as WORD_SIZE
 * and thereby MAX_ALIGN, but you never know. */
#define MALLOC_PTR_SIZE 8

/** There is a test program "memmax.c" in the misc folder you can run to find your systems
 * cap for memory allocations.  */
/* #define ARENAS_MAX_ALLOC 15200157696LL */

void arena_init_arenas(size_t count) ;

void arena_create(size_t n, size_t chunk_sz);
/* Creates a ready to use arena, and configures the arena to support a chunk_sz.
 *
 * We consider how malloc operates carefully to optimize block sizes and thereby improves
 * efficiency of the heap.
 * * The  amount of memory that malloc needs to store a pointer in the block is subtracted
 * from the requested memory.
 * * We do this, so that it is easy to use block sizes the size of page size (4096), whole
 * multiples of that, or whole parts of that, (2048, 1024, 512, 256, 128, 64.). This makes
 * it faster to allocate blocks, and if malloc needs to split blocks delivered from the
 * heap, then the heap is kept as tidy as  posible.
 * * I recommend the smallest chunk_sz requested to be 1024 bytes.
 */

/** Define the number of arenas you need. */

void *arena_alloc( size_t n, size_t mem_sz );
/* Allocates memory for an object from an arena. */

void *arena_calloc( size_t n,size_t nelem, size_t mem_sz );
/* Allocates memory for an array,zeroes it out. */

void arena_dealloc(size_t n );
/* Deallocate all objects from a lifetime, when their time is up, but retain the
 * memory for the allocation of a new set of objects in another lifetime. */
/* TODO: test how this works. */

void arena_destroy( size_t n );
/* Destroys an arena frees all memory, except for the arrays holding the arenas and
 * arena-logging info. */
#endif

