

/**
 * @file
 * @brief A general purpose arena strategy that is based on malloc.
 *
 * @details
 * ###Objective
 *
 * Makefile an easy to used arena memory allocation library, thatt  coexists nicely with
 * malloc and libraries that utilizes malloc.
 *
 * Have fast lifetime oriented allocation of memory, without individually
 * freeing memory and memory leaks, although dangling pointers are still possible.
 * Allocating larger chunks from malloc that is later on serving smaller memory allocation
 * requests, thingss keeps the heap less fragmented, The memory allocated by to us by
 * mallocloc is easy to return a chunk when we don't need it anymore , this will lower the
 * overall memory usage of your program.
 *
 *
 * ###Task
 *
 * Implement a dynamic arena allocation strategy, that dynamically allocate as much memory
 * we need, when the amount isn't known, with a faillsafe if the ask is greater than the
 * configure buffer size for the arena.
 *
 * ###Caveat
 *
 * The library doessn't support  dynamic arrayays I feel that realloc/realloc arrayay is
 * much better suited for that, they must be freed individually, with free().
 *
 * ###Intended usage:
 *
 * The intended usage is to used the arena for individually allocating many small things,
 * like structs, nodes, hash table values (nodes), strings and the like, maybe stored in a
 * dynamic resizable array, allocated, and resized with stdlib's realloc.
 *
 * chunk_sz: I personally intended to use a pagesize of my system's _PG_SZ_, which in my
 * case, on an x86-64 linux system  is 4096 bytes. That, or multiples of that size is the
 * most efficient chunk sz both speed and memory wise,if larger quantities of memory is feasible.
 * Smaller quantities made up of some quotient after a division by some power of two are
 * also good: 2048, 1024,512,256 etc, and so are also combinations like 3072, 1536, 768,
 * you get the idea.
 *
 * ###Design
 *
 * The design is founded upon three different sources:
 *
 * First and foremost based on the seminal paper: ["Fast Allocation and Deallocation of
 * Memory Based on Object Lifetimes" by David R
 * Hanson](https://www.cs.princeton.edu/techreports/1988/191.pdf).
 *
 * Secondly by u/skeetos/Chris Wellons' blog posts concerning arena memory allocation
 * strategies.  Especially his posts [Arena allocators tips and
 * tricks](https://nullprogram.com/blog/2023/09/27/) and [A simple, arena-backed, generic
 * dynamic array for c](https://nullprogram.com/blog/2023/10/05/)
 *
 * I have also used the rudimentary implementation of alloc in K&R Second edition, chp.
 * 8.7 as a foundation for knowledge on the subject, and William Stallings "Operating
 * Systems internals and design principles" concerning memory managment strategies.
 *
 * My own idea, even if maybe implicit in the others' ideas, is to explicitly use malloc
 * and free for the basic memory managment to avoid any `malloc/realloc/reallocarray`
 * versus `sbrk` conflict situations, that either maybe create holes in memory, or distort
 * pointers, due to the fact that an arena has to be moved, during reallocation.
 *
 * When we free an arena by destroying it we return the memory to the general pool of free
 * core memory, so the memory can be used for whatever purpose malloc/realloc/reallocarray
 * or other libraries that allocate memory, needs free memory for. Keeping a low memory
 * consumption.
 *
 * If we know we are going to reuse the memory for the same arena, then we can simply
 * deallocate, and the previousvly malloced() memory will be readyily availiable for
 * reuse.
 *
 * ###Implementation
 *
 * Coding wise I have amalgated the code from David R Hanson and Chris Wellons when it
 * comes to use variable names, I think Wellons' names: "begin" and "end" is better than
* Hansons' "avail" and "limit", their purpose are otherwise the same.
 *
 * The code is written for the c99 standard, using the Amd-x86-64 ABI, for a __linux__
 * Unix System, You may want to change the MAX_ALIGN to  better suit your needs for your
 * architecture, and depending on what attributes your compiler has available your may
 * also want to change the padding/alignmentent of `struct arena`.
 *
 * If you are going to port the code to a non __linux__ , then you should implement
 * checking for NULL after having called `malloc()`.
 *
 * Thanks to u/skeeto for reviewing my code and thereby hardening it.
 *
 * The whole correspondence can be found at:
 * https://www.reddit.com/r/C_Programming/comments/18r7vwy/an_arena_backed_memory_allocator_after_my_own_head/
 *
 * Thank you again u/skeeto, your effort is highly appreciated.
 */

 /* Copyright (C) * 2023 - McUsr * This program is free software; you can
    redistribute it and/or * modify it under the terms of the GNU General
    Public License * as published by the Free Software Foundation; either
    version 2 * of the License, or (at your option) any later version. * * This 
    program is distributed in the hope that it will be useful, * but WITHOUT
    ANY WARRANTY; without even the implied warranty of * MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the * GNU General Public License for 
    more details. * * You should have received a copy of the GNU General Public 
    License * along with this program; if not, write to the Free Software *
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA. */

#include "core_arena.h"

/**
 * @defgroup InternalVars  Internal datastructure and variables.
 * @{
 */
/** Typedef of struct arena */
typedef struct arena Arena;
/** Our struct for book keeping of the arena and its storage . */
struct arena {
    struct arena *next; /**< Link to next arena, one arena can consist of manyy arenas backed by individual buffers. */
    size_t chunk_sz;    /**< The size of the buffer allocated internally to satisfy memory requests from. */
    char *begin; /**< Next available location in the internal buffer to allocate memory from. */
    /** Address of one past end of buffer. */
    // *INDENT-OFF*
    char __attribute__((aligned(MAX_ALIGN))) *end; 
    // *INDENT-ON*
};

static Arena first[ARENAS_MAX]; /**< A head pointer to the first arena */
static Arena *arenas[ARENAS_MAX]; /**< Backing Array for accessing and using arenas */

/** error message string for an out of range arena numberer. */
static const char *msgBadArena = "Bad arena %lu: max arena: %d see ARENAS_MAX in core_arena.h\n";

const ptrdiff_t _AHS = sizeof( Arena ); /**< Arena Header Size */

/** Simple MAX macro, since no sideeffects */
#define MAX(a,b) a > b ? a : b;

/** @} */

/**
 * @defgroup LoggingSystem Simple logging system.
 * @brief A small logging system that reports memory usage by the arenas at program exit.
 * @details We support logging, so you can deduce the memory usage, the logging is only
 * intended for testing, and not to be left on in production code. The logging report is
 * bypassed if you exit your program with `_Exit` or a `TERM` signal, as
 * the report_usage is installed by `atexit()`.
 * @{
 */
/** Constant for no logging */
#define NO_ARENA_LOGGING 0
/** Constant for logging allocations of memory for the arenas. */
#define LOG_CHUNK_MALLOCS 1
/** Constant for logging bothh allocations of memory *for* the arenas, and the allocations
 * of memory *from* the arenas. */
#define FULL_ARENA_LOGGING 2

#define ARENAS_LOG_LEVEL 1

#if ARENAS_LOG_LEVEL ==  1

static unsigned long long allocated_chunks[ARENAS_MAX];
static unsigned long long allocation_chunk_count[ARENAS_MAX];
#elif ARENAS_LOG_LEVEL == 2

static unsigned long long allocated_chunks[ARENAS_MAX];
static unsigned long long allocation_chunk_count[ARENAS_MAX];
static unsigned long long allocated_memory[ARENAS_MAX];
static unsigned long long allocation_memory_count[ARENAS_MAX];

#endif


/**
 * @brief Reports memory usage, installed by atexit().
 */
#if ARENAS_LOG_LEVEL > 0
void report_memory_usage( void )
{
#if ARENAS_LOG_LEVEL ==  0
    ;
#elif ARENAS_LOG_LEVEL ==  1
    fprintf( stderr, "\nReport of arena memory usage:\n" "=============================\n" );
    for ( int i = 0; i < ARENAS_MAX; ++i ) {
        fprintf( stderr, "Arena nr %i was granted %llu bytes of memory in %llu allocations.\n",
                 i, allocated_chunks[i], allocation_chunk_count[i] );
    }
#elif ARENAS_LOG_LEVEL == 2
    fprintf( stderr, "\nReport of arena memory usage:\n" "=============================\n" );
    for ( int i = 0; i < ARENAS_MAX; ++i ) {
        fprintf( stderr, "Arena nr %i was granted %llu bytes of memory in %llu allocations.\n",
                 i, allocated_chunks[i], allocation_chunk_count[i] );
        fprintf( stderr, "Arena nr %i  gave away  %llu bytes of memory in %llu serves.\n",
                 i, allocated_memory[i], allocation_memory_count[i] );
    }
#endif

}
#endif

/** @} */
/**
 * @defgroup InternalFuncs Internal functions. 
 * @{
 */

/**
 * @brief
 * Initializes an arena and configures it with the effective chunk_size, and allocates the
 * memory, with malloc().
 * @param n The arena numberer we arena initializing.
 * @param chunk_sz  MALLOC_PTR_SIZE bytes larger than effective chunk size in bytes.
 * @details
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
static Arena *_arena_init( size_t chunk_sz )
{
    static const char *emsg = "_arena_init: The chunk_sz requested is to small: %d\n";

    if ( chunk_sz < MALLOC_PTR_SIZE ) {
        fprintf( stderr, emsg, chunk_sz );
        abort(  );
    }
    chunk_sz -= MALLOC_PTR_SIZE;
    if ( chunk_sz < MAX_ALIGN ) {
        fprintf( stderr, emsg, chunk_sz );
        abort(  );
    }
   // the size of the pointer malloc uses to address the allocated block.
    size_t padding = -chunk_sz & ( MAX_ALIGN - 1 );

    if ( padding ) {
        chunk_sz -= ( MAX_ALIGN - padding ); // Guarranteed to be a positive
                                             // number.
    }

    if ( chunk_sz <= ( size_t ) _AHS ) {
        fprintf( stderr, emsg, ( chunk_sz + padding + sizeof( unsigned int ) ) );
        abort(  );
    }
   // size_t real_size = chunk_sz + _AHS + padding;
    Arena *p;
    p = malloc( chunk_sz );
    if ( !p ) {
        return NULL;
    }
    p->chunk_sz = chunk_sz;
    p->begin = ( char * ) p + _AHS;
    p->end = ( char * ) p + chunk_sz; // real_size;
    p->next = NULL;
   // see https://nullprogram.com/blog/2023/09/27/ (the alloca() function //
    return p;
}

/**
 * @brief Allocates memory from an arena a new arena if necessary for delivering the
 * request..
 * @param **p The arena to request memory from, can change if needs new arena.
 * @param mem_sz The amount of memory requested.
 * @param n The arena number so we can log allocations to it.
 * @detail
 * This is basically Hanson's work.
 * This scheme is like it is so that it can work after a deallocation of the areas too,
 * reusing the previously not freed memory.
 * Algorithm taken from Hanson p. 3 where it is described, but with padding method from
 * Wellons, in addition I have added a "fail-safe" for when the ask for memory is larger
 * than then chunk_sz the arena is currently configured for, so that it will then as a
 * "one-off" case allocate a sufficiently large block.
 * 23-12-27: Now this is basically u/skeetos work for I have followed most of his 
 * recommendations in hardening the function.
 */
static void *_alloc( Arena ** p, size_t mem_sz, size_t n )
{
   // First reject anything nonsensical or excessively large .
    if ( mem_sz == 0 || mem_sz > PTRDIFF_MAX ) {
        return NULL; // request impossibly large (out of memory)
    }
   // Convert to signed so that it does not implicitly cast expression 
   // using it to size_t later. After this mem_sz is no longer needed.
    ptrdiff_t mem_pd = mem_sz;
   // Padding will be a small non-negative number. It is always safe
   // to subtract from a size, as the result will at worst be a small
   // negative number.
    ptrdiff_t padding = -mem_pd & ( MAX_ALIGN - 1 );


    Arena *ap;
    for ( ap = *p;; *p = ap ) {
       // Work using a size, not with pointer arithmetic.
        ptrdiff_t available = ap->end - ap->begin;

       // Per the note, this subtraction is safe. Both operands are
       // signed, so no surprise promotions turning a small negative
       // into large positive (i.e. size_t).
        if ( mem_pd > available - padding ) {
            if ( ap->next ) {
                ap = ap->next;
                ap->begin = ( char * ) ap + _AHS; // reset?
                continue; // keep looking

            } else { // End of the list, allocate a new chunk.
               // It is *not* yet safe to add header_size to mem_pd,
               // so subtract from the other side.
                if ( mem_pd > PTRDIFF_MAX - _AHS - padding ) {
                    return NULL; // request too large for metadata
                }
               // At this point we know header_size+mem_pd+padding is
               // safe to compute.

               // Note: chunk_sz does not require any alignment padding,
               // accounted for.
                ptrdiff_t real_size = MAX( ( mem_pd + padding + _AHS ), ( ptrdiff_t ) ( *p )->chunk_sz );

                ptrdiff_t chunk_sz = ap->chunk_sz; // save a copy
                ap = ap->next = malloc( real_size );
                if ( !ap ) {
                    return NULL; // OOM (can happen on Linux with huge mem_sz!)
                }
#if         ARENAS_LOG_LEVEL > 0
                allocated_chunks[n] += real_size;
                allocation_chunk_count[n] += 1 ;
#endif
                    ap->next = NULL;
                ap->chunk_sz = chunk_sz;
                ap->begin = ( char * ) ap + _AHS;
                ap->end = ap->begin + real_size;
                break; // use this arena
            }
        } else {
            break; // found space
        }
    }

    void *ptr = ap->begin;
    ap->begin += mem_pd + padding; // checks passed, so addition is safe
   // Note: I strongly recommend zero-initialization by default. It
   // makes for clearer, shorter, and more robust programs. If it's
   // too slow in some cases, add an opt-out flag.

   // Zeroing out the last byte and the padding
   // will implement an `arena_calloc` that zeroes out all returned memory,
   // to follow the usual convention, to get it done! makes swapping methods
   // easier.
    for ( char *zptr = ap->begin - ( padding + 1 ); zptr < ap->begin; zptr++ ) {
        *zptr = '\0';
    }
    return ptr;
   // return memset(ptr, 0, mem_pd);
}

/** @} */
/**
 * @defgroup UserFuncs User functions 
 * @{
 */
/**
 * @defgroup AllocFuncs Allocation functions. 
 * @{
 */
/**
 * @brief Allocates memory for an object from an arena.
 * @param n The index of the arena to request memory from.
 * @param mem_sz The amount of memory we want to allocate.
 * @details
 * Just calls  _alloc(), if there isn't enough memory to satisfy the request.
 */

void *arena_alloc( size_t n, size_t mem_sz )
{
    if ( n >= ARENAS_MAX ) {
        fprintf( stderr, msgBadArena, n, ARENAS_MAX );
        abort(  ); // Overflow conditions.
    }

    static const char *emsg = "arena_alloc: Couldn't allocate memory for arena with mem_pd: %lu.\n" "Aborting.";
   // First reject anything nonsensical or excessively large .
    if ( mem_sz == 0 || mem_sz > PTRDIFF_MAX ) {
        return NULL; // request impossibly large (out of memory)
    }

    void *p;
    p = arenas[n]->begin;
    ptrdiff_t mem_pd = mem_sz;
    ptrdiff_t padding = -mem_pd & ( MAX_ALIGN - 1 );
    mem_pd += padding;
    if ( mem_pd < ( ptrdiff_t ) mem_sz || ( ( mem_pd + _AHS ) > PTRDIFF_MAX ) ) {
        fprintf( stderr, emsg, ( size_t ) mem_pd );
        abort(  ); // Overflow conditions.
    }
    if ( ( arenas[n]->begin += mem_pd ) > arenas[n]->end ) {
       // padding is already added to mem_pd here.
        p = _alloc( &arenas[n], mem_sz, n );
    } else { // zero out last byte and padding
        for ( char *zptr = arenas[n]->begin - ( padding + 1 ); zptr < arenas[n]->begin; zptr++ ) {
            *zptr = '\0';
        }
    }

#if ARENAS_LOG_LEVEL > 1
    allocated_memory[n] += mem_sz;
    allocation_memory_count[n] += 1;
#endif

    return p;
}


/**
 * @brief Allocates memory for an array,zeroes it out.
 * @param n The arena that is the "owner" of the allocation.
 * @param nelem The number of elements in the array.
 * @param mem_sz The size of the individual memoryry..
 * @details
 * Great for when you need fully initialized memory.
 */
void *arena_calloc( size_t n, size_t nelem, size_t mem_sz )
{
    if ( n >= ARENAS_MAX ) {
        fprintf( stderr, msgBadArena, n, ARENAS_MAX );
        abort(  ); // Overflow conditions.
    }

    static const char *emsg = "arena_calloc: Couldn't allocate memory for arena with mem_ull: %lu.\n" "Aborting.";
    unsigned long long mem_ull = nelem * mem_sz;

    if ( mem_ull == 0 ) {
        return NULL;
    } else if ( mem_ull > PTRDIFF_MAX ) {
        fprintf( stderr, emsg, ( size_t ) mem_ull );
        abort(  ); // Overflow conditions.
    } else {
        size_t mem_req = ( size_t ) mem_ull;
        void *ptr = arena_alloc( n, mem_req ); // Any logging of memory
                                               // allocatations happens here!
        return memset( ptr, 0, mem_req );
    }
}


/**
 * @brief Deallocate all objects from a lifetime, when their time is up, but retain the
 * memory for the allocatation of a new set of objects in another lifetime.
 * @param n The index of the arena to deallocate.
 * @details
 * The memory aren't freed from the arena, so it is available for quick reuse.
 */
void arena_deallocate( size_t n )
{
    if ( n >= ARENAS_MAX ) {
        fprintf( stderr, msgBadArena, n, ARENAS_MAX );
        abort(  ); // Overflow conditions.
    }
    arenas[n] = first[n].next;
    if ( arenas[n] ) {
        arenas[n]->begin = ( char * ) arenas[n] + sizeof *arenas[n];
       // Works out beautifully with _alloc().
    } else {
        arenas[n] = &first[n];
    }
}

/** @} */

/**
 * @defgroup InitDeinit Initialization and destroy functions. 
 * @{
 */
/**
 * @brief Creates a ready to use arena, and configures the arena to support a chunk_sz.
 * @param n The index of the arena to destroy.
 * @param chunk_sz The nominal size of the arena to allocate memory from.
 * @details
 * Aborts if something is wrong.
 */
void arena_create( size_t n, size_t chunk_sz )
{
#if ARENAS_LOG_LEVEL > 0
    static bool log_inited;
    if ( !log_inited ) {
        atexit( report_memory_usage );
        log_inited = true;
    }
#endif
    if ( n >= ARENAS_MAX ) {
        fprintf( stderr, msgBadArena, n, ARENAS_MAX );
        abort(  ); // Overflow conditions.
    }

    static const char *emsg = "arena_create: Couldn't allocate memory for arena with chunk_sz: %lu.\n" "Aborting.";
   // First reject anything nonsensical or excessively large .
    if ( chunk_sz == 0 || chunk_sz > PTRDIFF_MAX ) {
        fprintf( stderr, emsg, chunk_sz );
        abort(  );
    }

    first[n].next = _arena_init( chunk_sz );
    if ( first[n].next == NULL ) {
        fprintf( stderr, emsg, chunk_sz );
        abort(  );
    }
#if ARENAS_LOG_LEVEL > 0
    allocated_chunks[n] += chunk_sz;
    allocation_chunk_count[n] += 1 ;
#endif
    arenas[n] = first[n].next;
}

/**
 * @brief Destroys an arena frees all memory.
 * @param n The index of the arena to destroy.
 * @details
 * All memory is released into the common pool of free memory.
 *
 */

void arena_destroy( size_t n )
{
    if ( n >= ARENAS_MAX ) {
        fprintf( stderr, msgBadArena, n, ARENAS_MAX );
        abort(  ); // Overflow conditions.
    }

    Arena *p,
    *q;
    for ( p = ( Arena * ) ( first[n].next ); p; ) {
        q = p->next;
        free( p );
        p = q;
    }
    first[n].next = NULL;
    arenas[n] = NULL;
}

/** @} */
/** @} */
