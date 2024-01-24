/* License:
 *  Copyright (C)  2023 - McUsr  This program is free software; you can
 *   redistribute it and/or  modify it under the terms of the GNU General
 *   Public License  as published by the Free Software Foundation; either
 *   version 2  of the License, or (at your option) any later version.  This 
 *   program is distributed in the hope that it will be useful, but WITHOUT
 *   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for 
 *   more details.  You should have received a copy of the GNU General Public 
 *   License  along with this program; if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
/**
 * @file
 * @brief A general purpose arena allocation strategy that is based on malloc.
 * @details
 * It is simple, you can't resize previously allocated memory, and it shouldn't break
 * during production, if it breaks, then we exit with an EXIT_FAILURE.
 * So, if you need a hashtable, then you should allocate the resizeable array with calloc,
 * so you can resize that with realloc, and allocate the hashes from the arena.
 * This arena allocation strategy uses malloc intrinisically, if you want other schemes
 * than malloc provides you should look elsewhere.
 */

#include <core_arena.h>

/**
 * @defgroup InternalVars  main backing  datastructure.
 * @brief The backing datastructures that makes the arena work.
 * @{
 */

static const ptrdiff_t c128K = 128 * 1024 ; /**> constant for 128K, which is max malloc can allocate from core. */
/** Typedef of struct arena */
typedef struct arena Arena;
/** Our struct for book keeping of the arena and its storage . */
struct __attribute__((aligned(MAX_ALIGN))) arena {
    struct arena *next; /**> Link to next arena, one arena can consist of manyy arenas backed by individual buffers. */
    size_t chunk_sz;    /**> The size of the buffer allocated internally to satisfy memory requests from. */
    char __attribute__((aligned(MAX_ALIGN))) *begin; /**> Next available location in the internal buffer to allocate memory from. */
    /** Address of one past end of buffer. */
    // *INDENT-OFF*
    char __attribute__((aligned(MAX_ALIGN))) *end; 
    // *INDENT-ON*
};

static uint32_t ARENAS_MAX;
static Arena *first; /**< A head pointer to the first arena */
static Arena **arenas; /**< Backing Array for accessing and using arenas */
static size_t *arena_chunk_sz; /**< Standard chunk size for arena. */

/** error message string for an out of range arena numberer. */
static const char *msgBadArena = "Bad arena %lu: max arena: %d see ARENAS_MAX in core_arena.h\n";

const ptrdiff_t _AHS = sizeof( Arena ); /**< Arena Header Size */

/** Simple MAX macro, since no sideeffects */
#define MAX(a,b) a > b ? a : b;

/** @} */

/** 
 * @defgroup  ErrorAndLogMessages Error and Log message functions.
 * @brief  This module is used for both logging and error messages.
 * @{
 */

static char *ename[] = {
    /*   0 */ "", 
    /*   1 */ "EPERM", "ENOENT", "ESRCH", "EINTR", "EIO", "ENXIO", 
    /*   7 */ "E2BIG", "ENOEXEC", "EBADF", "ECHILD", 
    /*  11 */ "EAGAIN/EWOULDBLOCK", "ENOMEM", "EACCES", "EFAULT", 
    /*  15 */ "ENOTBLK", "EBUSY", "EEXIST", "EXDEV", "ENODEV", 
    /*  20 */ "ENOTDIR", "EISDIR", "EINVAL", "ENFILE", "EMFILE", 
    /*  25 */ "ENOTTY", "ETXTBSY", "EFBIG", "ENOSPC", "ESPIPE", 
    /*  30 */ "EROFS", "EMLINK", "EPIPE", "EDOM", "ERANGE", 
    /*  35 */ "EDEADLK/EDEADLOCK", "ENAMETOOLONG", "ENOLCK", "ENOSYS", 
    /*  39 */ "ENOTEMPTY", "ELOOP", "", "ENOMSG", "EIDRM", "ECHRNG", 
    /*  45 */ "EL2NSYNC", "EL3HLT", "EL3RST", "ELNRNG", "EUNATCH", 
    /*  50 */ "ENOCSI", "EL2HLT", "EBADE", "EBADR", "EXFULL", "ENOANO", 
    /*  56 */ "EBADRQC", "EBADSLT", "", "EBFONT", "ENOSTR", "ENODATA", 
    /*  62 */ "ETIME", "ENOSR", "ENONET", "ENOPKG", "EREMOTE", 
    /*  67 */ "ENOLINK", "EADV", "ESRMNT", "ECOMM", "EPROTO", 
    /*  72 */ "EMULTIHOP", "EDOTDOT", "EBADMSG", "EOVERFLOW", 
    /*  76 */ "ENOTUNIQ", "EBADFD", "EREMCHG", "ELIBACC", "ELIBBAD", 
    /*  81 */ "ELIBSCN", "ELIBMAX", "ELIBEXEC", "EILSEQ", "ERESTART", 
    /*  86 */ "ESTRPIPE", "EUSERS", "ENOTSOCK", "EDESTADDRREQ", 
    /*  90 */ "EMSGSIZE", "EPROTOTYPE", "ENOPROTOOPT", 
    /*  93 */ "EPROTONOSUPPORT", "ESOCKTNOSUPPORT", 
    /*  95 */ "EOPNOTSUPP/ENOTSUP", "EPFNOSUPPORT", "EAFNOSUPPORT", 
    /*  98 */ "EADDRINUSE", "EADDRNOTAVAIL", "ENETDOWN", "ENETUNREACH", 
    /* 102 */ "ENETRESET", "ECONNABORTED", "ECONNRESET", "ENOBUFS", 
    /* 106 */ "EISCONN", "ENOTCONN", "ESHUTDOWN", "ETOOMANYREFS", 
    /* 110 */ "ETIMEDOUT", "ECONNREFUSED", "EHOSTDOWN", "EHOSTUNREACH", 
    /* 114 */ "EALREADY", "EINPROGRESS", "ESTALE", "EUCLEAN", 
    /* 118 */ "ENOTNAM", "ENAVAIL", "EISNAM", "EREMOTEIO", "EDQUOT", 
    /* 123 */ "ENOMEDIUM", "EMEDIUMTYPE", "ECANCELED", "ENOKEY", 
    /* 127 */ "EKEYEXPIRED", "EKEYREVOKED", "EKEYREJECTED", 
    /* 130 */ "EOWNERDEAD", "ENOTRECOVERABLE", "ERFKILL", "EHWPOISON"
};

/** Maximum length for an error name */
#define MAX_ENAME 133

static inline char *_errmsg_format( int err, const char *format, va_list ap )
{
#define ERR_BUF_SIZE 500
    static char buf[ERR_BUF_SIZE],
     userMsg[ERR_BUF_SIZE],
     errText[ERR_BUF_SIZE];

    vsnprintf( userMsg, ERR_BUF_SIZE, format, ap );

    snprintf( errText, ERR_BUF_SIZE, " [%s %s]", ( err > -1 && err <= MAX_ENAME ) ? ename[err] : "?UNKNOWN?", strerror( err ) );

#if __GNUC__ >= 7
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
    snprintf( buf, ERR_BUF_SIZE, "ERROR%s %s\n", errText, userMsg );
#if __GNUC__ >= 7
#pragma GCC diagnostic pop
#endif
    return buf;
}

static inline void _errmsg_write( const char *format, ... )
{
    char *formatted_emsg=NULL;
    va_list argList;
    int savedErrno;

    savedErrno = errno;         /* In case we change it here */

    va_start( argList, format );
    formatted_emsg = _errmsg_format(errno,format,argList );
    va_end( argList );

    fflush( stdout );       /* Flush any pending stdout */
    fputs( formatted_emsg, stderr );
    fflush( stderr );           /* In case stderr is not line-buffered */

    errno = savedErrno;
}


static inline char *_logmsg_format( const char *format, va_list ap )
{
#define LOG_BUF_SIZE 200
    static char buf[LOG_BUF_SIZE];

    vsnprintf( buf, LOG_BUF_SIZE, format, ap );
    return buf;
}
/**
 * @brief Also used for user errors that doesn't solicit any error number.
 */
static inline void _logmsg_write( const char *format, ... )
{
    char *formatted_emsg=NULL;
    va_list argList;
    int savedErrno;

    savedErrno = errno;         /* In case we change it here */

    va_start( argList, format );
    formatted_emsg = _logmsg_format(format,argList );
    va_end( argList );

    fflush( stdout );       /* Flush any pending stdout */
    fputs( formatted_emsg, stderr );
    fflush( stderr );           /* In case stderr is not line-buffered */

    errno = savedErrno;
}

/** @} */
/**
 * @defgroup MemFuncs Utility functions regarding  physical memory and page sizes.
 * @brief Utility function for finding availe free memory and system page_size.
 *
 */
size_t ARENAS_MAX_ALLOC;


/**
 * @brief Returns the system viritual memory page_size.
 */

long system_page_size(void)
{
    long page_size;
    if ((page_size = sysconf(_SC_PAGESIZE)) == -1) {
        _errmsg_write("system_page_size: sysconf(_SC_PAGESIZE) failed. Aborting.%s","\n");
        exit(EXIT_FAILURE);
    }
    return page_size ;
}

/** 
 * @brief Returns the physically available ram.
 */
static inline size_t ram_avail(void )
{
    long avail_phys_pages, page_size;
    long long mem_avail;
    if ((avail_phys_pages = sysconf(_SC_AVPHYS_PAGES)) == -1 ){
        _errmsg_write("ram_avail: sysconf(_SC_AVPHYS_PAGES) failed. Aborting.%s","\n");
        exit(EXIT_FAILURE);
    }

    if ((page_size = sysconf(_SC_PAGESIZE)) == -1) {
        _errmsg_write("ram_avail: sysconf(_SC_PAGESIZE) failed. Aborting.%s","\n");
        exit(EXIT_FAILURE);
    }

    if ((size_t)page_size > -1ULL/avail_phys_pages) {
        fprintf(stderr,"ram_avail: needs bigger datatypes to hold available ram!\n");
        exit(EXIT_FAILURE);
    } 
    mem_avail = avail_phys_pages * page_size ; 
    if ( mem_avail > PTRDIFF_MAX ) {
        fprintf(stderr,"ram_avail: needs bigger datatypes to hold available ram!\n");
        exit(EXIT_FAILURE); // Overflow conditions.
    }
    return (size_t) mem_avail ;
}

/** @} */

/**
 * @defgroup LoggingSystem Simple memory logging system.
 * @brief A small logging system that reports memory usage by the arenas at program exit.
 * @details
 * The logging system is an option, that can be opted out of for whatever reason, by
 * defining `CORE_ARENA_NO_LOGGING`.
 *
 * We support logging, so you can deduce the memory usage, the logging is only
 * intended for testing, and not to be left on in production code. The logging report is
 * bypassed if you exit your program with `_Exit` or a `TERM` signal, directly or
 * implicitly (`abort()`), as the report_usage is installed by `atexit()`.
 *
 * The logging is configured by an environment variable: `CORE_ARENA_LOG_LEVEL`.
 * There are three log levels:
 * * 0: Disable all logging. (`NO_ARENA_LOGGING`). 
 * * 1: Log allocations of chunks from computer memory, to the arenas. (`LOG_CHUNK_MALLOCS`).
 * * 2: Logs allocations from the arenas to your program in addition to (1).
 * (`FULL_ARENA_LOGGING`)
 *
 * You can only supply an integer value to `CORE_ARENA_LOG_LEVEL`.
 *
 * This module also contains the datastructures for the memory accounting.
 *
 * @{
 */

#define NO_ARENA_LOGGING 0 /**< Constant for no logging */
#define LOG_CHUNK_MALLOCS 1 /**< Constant for logging allocations of memory for the arenas. */
#define FULL_ARENA_LOGGING 2 /**< Constant for logging bothh allocations of memory *for* the arenas, and the allocations

 * of memory *from* the arenas. */
int16_t core_arena_log_level; /**< Defines our current log level, if compiled in. */

#ifndef CORE_ARENA_NO_LOGGING 
#ifdef TEST
size_t  tot_mem_usage; /**>Total usage in bytes. */
size_t  *arenas_mem_malloced ; 
size_t  *arenas_mem_malloced_count ; 
size_t *arenas_mem_mmapped ; 
size_t *arenas_mem_mmapped_count ; 
size_t *arenas_mem_served;
size_t *arenas_mem_served_count ;
#else
static size_t  tot_mem_usage; /**>Total usage in bytes. */
static size_t  *arenas_mem_malloced ; /**>Amount of ram allocated per arena. */
static size_t  *arenas_mem_malloced_count ;  /**>Number of allocations of ram per arenas. */
static size_t *arenas_mem_mmapped ;  /**>Amount of ram mmapped per arena (>128K). */
static size_t *arenas_mem_mmapped_count ; /**>Number of mmapped ram per arena. */
static size_t *arenas_mem_served; /**>Amount of ram served through arena_alloc/calloc */
static size_t *arenas_mem_served_count ; /**>Number of servings of ram per arena. */
#endif
#endif

/**
 * @brief returns the current log level, set from an environment variable. 
 * @details
 * Called from arena_init_arenas.
 */

int16_t get_log_level(void)
{
    const char *emsg="get_log_level: %s" ;
    int64_t parsed_val=0L;

    char *envvar_p = getenv( "CORE_ARENA_LOG_LEVEL" );

    if ( envvar_p == NULL ) {
        /* _logmsg_write(emsg,"CORE_ARENA_LOG_LEVEL not set.\n"); */
        ;
    } else {
        char *end_p = NULL;
        int old_errno = errno;
        errno = 0;
        parsed_val = strtol( envvar_p, &end_p, 10 );


        if ( errno != 0 ) {
            _errmsg_write(emsg,"strtol");
        }

        if ( end_p == envvar_p ) {
            _logmsg_write(emsg,"strtol found no digits in CORE_ARENA_LOG_LEVEL.\n");
        }
        if ( parsed_val < 0 || parsed_val > 2 ) {
            _logmsg_write(emsg,"value for CORE_ARENA_LOG_LEVEL out of range: excepts values in rangeg  0..2, was: %ld.\n",parsed_val );
            parsed_val=0L;
        }
        /* printf( "coolz %ld\n", parsed_val ); */
        errno = old_errno;
    }
    return parsed_val;
}

void report_memory_allocations(void )
{
        fprintf(stderr,"Allocations of blocks of memory to our arenas\n");
        for ( uint32_t i = 0; i < ARENAS_MAX; ++i ) {
            fprintf( stderr, "Arena[%u] malloced: %lu bytes in  %lu blocks.\n",
                     i, arenas_mem_malloced[i], arenas_mem_malloced_count[i] );

            fprintf( stderr, "Arena[%i] mmapped: %lu bytes in  %lu blocks.\n",
                     i, arenas_mem_mmapped[i], arenas_mem_mmapped_count[i] );

            /* fprintf( stderr, "Arena nr %i was granted %llu bytes of memory in %llu allocations.\n", */
            /*          i, allocated_chunks[i], allocation_chunk_count[i] ); */
        }

}

/**
 * @brief Reports memory usage, installed by atexit().
 */
/* #ifndef CORE_ARENA_NO_LOGGING */

void report_memory_usage( void )
{

    if (core_arena_log_level == LOG_CHUNK_MALLOCS  ) {
        fprintf( stderr, "\nReport of arena memory usage:\n" "=============================\n" );
        report_memory_allocations();
    } else if ( core_arena_log_level  ==  FULL_ARENA_LOGGING ) {
        fprintf( stderr, "\nReport of arena memory usage:\n" "=============================\n" );
        report_memory_allocations();
        for ( uint32_t i = 0; i < ARENAS_MAX; ++i ) {
            fprintf( stderr, "Arena nr %i  served %lu bytes of memory in %lu serves.\n",
                     i, arenas_mem_served[i], arenas_mem_served_count[i] );
        }
    }

}
/* #endif */

/** @} */
/**
 * @defgroup InternalFuncs Internal functions. 
 * @brief Internal allocation functions for allocating computer memory.
 * @{
 */

static const char *alloc_emsg = "%s: arena[%u] The chunk_sz requested is to small: %d\n";
static const char *alloc_emsg2 = "%s: arena[%u] The chunk_sz: %lu requested is too large.\n"
                           "The request is larger than ARENAS_MAX_ALLOC %lu: ";
static const char *alloc_emsg3 = "%s: arena[%u] The chunk_sz: %lu requested is too large.\n"
                           "It will make the total number of bytes requested larger than ARENAS_MAX_ALLOC %lu: ";

/**
 * @brief
 * Initializes an arena and configures it with the effective chunk_size, and allocates the
 * memory, with malloc(), it also accounts for memory allocated if logging is turned on.
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
static Arena *_arena_init( size_t n, size_t chunk_sz )
{
    ptrdiff_t chunk_pd = chunk_sz; // maybe someone without gcc wants to compile it.

    if ( chunk_pd <  MALLOC_PTR_SIZE + MAX_ALIGN ) {
        fprintf( stderr, alloc_emsg,"_arena_init",n, chunk_sz );
        abort(  );
    }
    chunk_pd -= MALLOC_PTR_SIZE;
    ///@todo same tests as in arena_alloc.

    /// @todo: Not sure about this at all. But it is to be able to really get pages
    ///@todo: document this.
    // the size of 1024, but I think malloc will add 8 to 1024.
    // so for instance the smart size to ask for is 4096-8 == 4088. 

   // the size of the pointer malloc uses to address the allocated block.
    ptrdiff_t padding = -chunk_pd & ( MAX_ALIGN - 1 );

    if ( padding ) {
        chunk_pd -= ( MAX_ALIGN - padding ); // Guaranteed to be positive.
    }

    if ( chunk_pd <= ( ptrdiff_t ) _AHS ) {
        fprintf( stderr, alloc_emsg,"_arena_init",n, ( chunk_pd + padding + MALLOC_PTR_SIZE ) );
        /// @todo: MALLOC_PTR_SIZE -> MALLOC_BLOCKSZ_FIELD. (renaming)
        exit(EXIT_FAILURE) ;
    }

    if ( chunk_pd > (ssize_t) (ARENAS_MAX_ALLOC - MALLOC_PTR_SIZE) ) {
        fprintf( stderr, alloc_emsg2, "_arena_init",n, chunk_pd, ARENAS_MAX_ALLOC );
        exit(EXIT_FAILURE) ;
    } else if ( tot_mem_usage > ARENAS_MAX_ALLOC - (chunk_pd + MALLOC_PTR_SIZE) ) {
        fprintf( stderr, alloc_emsg3, "_arena_init",n,chunk_pd, ARENAS_MAX_ALLOC );
        exit(EXIT_FAILURE) ;
    }

    Arena *p;
    p = malloc( chunk_pd );
    if ( !p ) {
        return NULL;
    } 
    tot_mem_usage += chunk_pd ; // updates total allocated.
#ifndef CORE_ARENA_NO_LOGGING
    if (core_arena_log_level >= LOG_CHUNK_MALLOCS  ) {
        if ( chunk_pd < c128K ) {
            arenas_mem_malloced[n] += chunk_pd ;
            arenas_mem_malloced_count[n] += 1 ;
        } else {
            arenas_mem_mmapped[n] += chunk_pd ;
            arenas_mem_mmapped_count += 1 ;
        }
    }
#endif
    p->chunk_sz = chunk_pd;
    p->begin = ( char * ) p + _AHS;
    p->end = ( char * ) p + chunk_pd; // real_size;
    p->next = NULL;
   // see https://nullprogram.com/blog/2023/09/27/ (the alloca() function //
    return p;
}

/**
 * @brief Allocates memory from an arena a new arena if necessary for delivering the
 * request..
 * @param **p The arena to request memory from, can change if needs new arena.
 * @param mem_sz The amount of memory requested.
 * @param n The arena number so we can log allocations to it, and identify configured
 * chunk_sz.
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

    // Size calculations differ here from _arena_init
    // We want a chunk of memory from a l


    Arena *ap;
    for ( ap = *p;; *p = ap ) {
       // Work using a size, not with pointer arithmetic.
        ptrdiff_t available = ap->end - ap->begin; 
        // assert(available >= 0 );

       // Per the note, this subtraction is safe. Both operands are
       // signed, so no surprise promotions turning a small negative
       // into large positive (i.e. size_t).
        if ( mem_pd > available - padding ) {
            if ( ap->next ) {
                ap = ap->next;
                ap->begin = ( char * ) ap + _AHS; // reset!
                // ap->end is never touched!
                continue; // keep looking

            } else { // End of the list, allocate a new chunk.
               // It is *not* yet safe to add header_size to mem_pd,
               // so subtract from the other side.¸
                if ( mem_pd > (PTRDIFF_MAX - _AHS - padding) ) {
                    return NULL; // request too large for metadata
                }
               // At this point we know header_size+mem_pd+padding is
               // safe to compute.

               // Note: chunk_sz does not require any alignment padding,
               // accounted for.
                ptrdiff_t real_size = MAX( ( mem_pd + padding + _AHS ), ( ptrdiff_t ) first[n].chunk_sz );


                if ( real_size > (ssize_t)ARENAS_MAX_ALLOC ) {
                    fprintf( stderr, alloc_emsg2,"_alloc",n,real_size, ARENAS_MAX_ALLOC );
                    exit(EXIT_FAILURE) ;
                } else if ( tot_mem_usage > ARENAS_MAX_ALLOC - real_size ) {
                    fprintf( stderr, alloc_emsg3, "_alloc",n,real_size, ARENAS_MAX_ALLOC );
                    exit(EXIT_FAILURE) ;
                }

                ap = ap->next = malloc( (size_t)real_size );
                if ( !ap ) {
                    return NULL; // OOM (can happen on Linux with huge mem_sz!)
                }
                tot_mem_usage += real_size ; // updates total allocated.

#ifndef CORE_ARENA_NO_LOGGING
                if (core_arena_log_level > NO_ARENA_LOGGING ) {
                    if ( real_size < c128K ) {
                        arenas_mem_malloced[n] += real_size ;
                        arenas_mem_malloced_count[n] += 1 ;
                    } else {
                        arenas_mem_mmapped[n] += real_size ;
                        arenas_mem_mmapped_count[n] += 1 ;
                    }
                }
#endif
                ap->next = NULL;
                *p = ap ; // BUGFIX we are breaking out, and won't update in for loop.
                if ( real_size > (ptrdiff_t) first[n].chunk_sz ) {
                    ap->chunk_sz = (size_t) (real_size - _AHS) ;
                } else {
                    ap->chunk_sz = first[n].chunk_sz;
                }
                ap->begin = ( char * ) ap + _AHS;
                ap->end = ap->begin + ap->chunk_sz;
                break; // use this arena
            }
        } else {
            break; // found space
        }
    }

    void *ptr = ap->begin;
    ap->begin += mem_pd + padding; // checks passed, so addition is safe
    // Starting point for next memory allocation.

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
    // MAX_ALIGN added by caller _arena_alloc.
    return ptr;
   // return memset(ptr, 0, mem_pd);
}

/** @} */
/**
 * @defgroup UserFuncs User functions 
 * @brief The different functions to be used from a user program.
 * @{
 */

/**
 * @defgroup ConfigFuncs Configuration functions.
 * @brief Functions for setting up and tearing down the arenas.
 * @{
 */

static bool arenas_initialized=false;

static void _arena_teardown(void)
{
    free(first);
    free(arenas);
}

#ifndef CORE_ARENA_NO_LOGGING

static size_t *max_chunk_size; /**> For logging max chunk size for each arena. */
static size_t *max_mem_request; /**> For logging largest memory request for each arena. */
static size_t *min_mem_request; /**> For logging smallest memory request for each arena. */
static size_t *avg_mem_request; /**> for logging average memory request for each arena */

#endif

/* Macro for figuring out if we can serve a memory request */
#define CHECK_MEM_AVAIL(mem_need, mem_object ) do {\
    if (tot_mem_usage > ARENAS_MAX_ALLOC - mem_need ) {\
        _logmsg_write(emsg2,#mem_object);\
        exit(EXIT_FAILURE);\
    }\
} while (0)


/**
 * @brief 
 * Initializes the number of arenas.
 * @details
 * Sets a static variable, to show we are initiated.
 * creates arrays fit for the number of arenas
 * installs an exit handler to take down the arenas on exit.
 * Determines logging options if logging is compiled in.
 * Gets the amount of `phys_avail` memory.
 * @param count 1 larger than the last arena, starting at zero.
 */
void arena_init_arenas(size_t count)
{
    static const char *emsg = "arena_init_arenas: Calloc couldn't allocate memory for %s array." ;
    static const char *emsg2 ="arena_init_arenas: Out of memory not enough free memory to allocates for %s.\n";
    size_t memory_consumption;

    assert( count > 0 ) ;
    ARENAS_MAX = count ;

    ARENAS_MAX_ALLOC = ram_avail() ;

    memory_consumption = ARENAS_MAX * sizeof(Arena) ;
    CHECK_MEM_AVAIL(memory_consumption,first);
    first = calloc(ARENAS_MAX, sizeof(Arena)) ;
    if (!first) {
        _errmsg_write( emsg,"first");
        exit(EXIT_FAILURE);

    }
    tot_mem_usage += memory_consumption ;

    memory_consumption = ARENAS_MAX * sizeof(Arena) ;
    CHECK_MEM_AVAIL(memory_consumption,arenas);
    arenas  = calloc(ARENAS_MAX, sizeof(Arena*)) ;
    if (!arenas) {
        _errmsg_write( emsg,"arenas");
        exit(EXIT_FAILURE);
    }
    tot_mem_usage += memory_consumption ;
    memory_consumption= ARENAS_MAX * sizeof(size_t ) ;
    CHECK_MEM_AVAIL(memory_consumption,arena_chunk_sz);
    arena_chunk_sz  = calloc(ARENAS_MAX, sizeof(size_t)) ;
    if (!arena_chunk_sz) {
        _errmsg_write( emsg,"arena_chunk_sz");
        exit(EXIT_FAILURE);
    }
    tot_mem_usage += memory_consumption ;

#ifndef CORE_ARENA_NO_LOGGING
    core_arena_log_level = get_log_level() ;

    if (core_arena_log_level >= LOG_CHUNK_MALLOCS  ) {
        memory_consumption = ARENAS_MAX * sizeof *arenas_mem_malloced ;

        CHECK_MEM_AVAIL(memory_consumption,arenas_mem_malloced);
        arenas_mem_malloced = calloc(ARENAS_MAX, sizeof *arenas_mem_malloced );
        if (!arenas_mem_malloced) {
            _errmsg_write( emsg,"arenas_mem_malloced");
            exit(EXIT_FAILURE);
        }
        tot_mem_usage += memory_consumption ;

        memory_consumption = ARENAS_MAX * sizeof *arenas_mem_malloced_count ;

        CHECK_MEM_AVAIL(memory_consumption,arenas_mem_malloced_count) ;
        arenas_mem_malloced_count = calloc(ARENAS_MAX, sizeof *arenas_mem_malloced_count );
        tot_mem_usage += memory_consumption ;

        memory_consumption = ARENAS_MAX * sizeof *arenas_mem_mmapped ;

        CHECK_MEM_AVAIL(memory_consumption,arenas_mem_mmapped) ;
        arenas_mem_mmapped = calloc(ARENAS_MAX, sizeof *arenas_mem_mmapped );
        if (!arenas_mem_mmapped) {
            _errmsg_write( emsg,"arenas_mem_mmapped");
            exit(EXIT_FAILURE);
        }
        tot_mem_usage += memory_consumption ;

        memory_consumption = ARENAS_MAX * sizeof *arenas_mem_mmapped_count ;
        CHECK_MEM_AVAIL(memory_consumption,arenas_mem_mmapped_count) ;
        arenas_mem_mmapped_count = calloc(ARENAS_MAX, sizeof *arenas_mem_mmapped_count );
        tot_mem_usage += memory_consumption ;
    }
    if (core_arena_log_level >= FULL_ARENA_LOGGING  ) {

        memory_consumption = ARENAS_MAX * sizeof *arenas_mem_served ;
        CHECK_MEM_AVAIL(memory_consumption,arenas_mem_served) ;
        arenas_mem_served = calloc(ARENAS_MAX, sizeof *arenas_mem_served );
        if (!arenas_mem_served) {
            _errmsg_write( emsg,"arenas_mem_served");
            exit(EXIT_FAILURE);
        }
        tot_mem_usage += memory_consumption ;

        memory_consumption = ARENAS_MAX * sizeof *arenas_mem_served_count ;
        CHECK_MEM_AVAIL(memory_consumption,arenas_mem_served_count) ;
        arenas_mem_served_count = calloc(ARENAS_MAX, sizeof *arenas_mem_served_count );
        if (!arenas_mem_served_count) {
            _errmsg_write( emsg,"arenas_mem_served_count");
            exit(EXIT_FAILURE);
        }
        tot_mem_usage += memory_consumption ;
    }
#endif 

#if 0 == 1
    for (uint_32 i = 0; i < count; ++i) {
        first[i].next = malloc(sizeof(Arena);
        
    } 
#endif
    atexit(_arena_teardown) ;
    arenas_initialized = true ;
}

/** @} */

/**
 * @defgroup AllocFuncs Allocation functions. 
 * @brief Functions that allocates memory from the arena to your program.
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
    assert( arenas_initialized == true ) ;

    if ( n >= ARENAS_MAX ) {
        fprintf( stderr, msgBadArena, n, ARENAS_MAX );
        abort(  ); // Overflow conditions.
    }

    static const char *emsg = "arena_alloc: arena[%u]: Couldn't allocate memory for arena with mem_pd: %lu.\n" ;
    static const char *emsg2 = "arena_alloc: arena[%u]: Couldn't allocate memory of size_t %ld.\n"
        "The request is larger than ARENAS_MAX_ALLOC: %lu. ";
    static const char *emsg3 = "arena_alloc: arena[%u]: Couldn't allocate memory of size_t %ld.\n"
        "The request is larger than memory available: %lu. ";

    ///@todo tests and error messages and EXIT_FAILURE

   // First reject anything nonsensical or excessively large .
    if ( mem_sz == 0 || mem_sz > PTRDIFF_MAX ) {
        return NULL; // request impossibly large (out of memory)
    }

    void *p;
    p = arenas[n]->begin; // start of buffer to allocate.
    ptrdiff_t mem_pd = mem_sz;
    ptrdiff_t padding = -mem_pd & ( MAX_ALIGN - 1 );
    mem_pd += padding;

    if ( mem_pd < ( ptrdiff_t ) mem_sz || (  mem_pd   > (PTRDIFF_MAX - _AHS) ) ) {
        fprintf( stderr, emsg,n, ( size_t ) mem_pd );
        exit(EXIT_FAILURE);
    } else if (mem_pd > (ssize_t)ARENAS_MAX_ALLOC ) {
        fprintf( stderr, emsg2,n, ( size_t ) mem_pd, ARENAS_MAX_ALLOC );
        exit(EXIT_FAILURE);
    } else if (mem_pd > (ssize_t) (ARENAS_MAX_ALLOC - tot_mem_usage) ) {
        fprintf( stderr, emsg3,n, (size_t) mem_pd, (ARENAS_MAX_ALLOC-tot_mem_usage) );
        exit(EXIT_FAILURE);
    }

    ptrdiff_t gauge = (ptrdiff_t)arenas[n]->begin + mem_pd ;
    assert( gauge >= 0) ;
    if ( gauge  > (ptrdiff_t) arenas[n]->end ) {
       // padding is already added to mem_pd here.
        p = _alloc( &arenas[n], mem_pd, n );
    } else { // zero out last byte and padding
        arenas[n]->begin += mem_pd ;
        for ( char *zptr = arenas[n]->begin - ( padding + 1 ); zptr < arenas[n]->begin; zptr++ ) {
             *zptr = '\0';
         }
        if ( gauge < (ptrdiff_t) (arenas[n]->end - MAX_ALIGN) ) {
            arenas[n]->begin += MAX_ALIGN ;
        }
    }

#ifndef CORE_ARENA_NO_LOGGING
    if (core_arena_log_level >= FULL_ARENA_LOGGING )  {
        arenas_mem_served[n] += mem_pd ;
        arenas_mem_served_count[n] += 1 ;
    }
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
///@todo change n from size_t to uint32_t?
void *arena_calloc( size_t n, size_t nelem, size_t mem_sz )
{
    assert( arenas_initialized == true ) ;

    if ( n >= ARENAS_MAX ) {
        fprintf( stderr, msgBadArena, n, ARENAS_MAX );
        exit(EXIT_FAILURE);
    }

    static const char *emsg = "arena_calloc: arena[%u]: Couldn't allocate memory for array with mem_ll: %ld.\n"
        "The request is larger than ARENAS_MAX_ALLOC: %lu. ";
    static const char *emsg2 = "arena_calloc: arena[%u]: Couldn't allocate memory for array with %ld nelem of size_t %ld.\n"
        "The request is larger than ARENAS_MAX_ALLOC: %lu. ";
    static const char *emsg3 = "arena_calloc: arena[%u]: Couldn't allocate memory for array with %ld nelem of size_t %ld.\n"
        "The request is larger than memory available: %lu. ";
    assert( mem_sz > 0 ) ;
    long long mem_ll ;
    if (nelem > -1ULL/mem_sz) {
        fprintf( stderr, emsg2, (size_t) nelem, ( size_t ) mem_sz, ARENAS_MAX_ALLOC );
        exit(EXIT_FAILURE);
    } else {
        mem_ll = nelem * mem_sz;
    }
    if ( mem_ll <= 0 ) { // nelem was 0
        return NULL;
    } else if ( mem_ll > PTRDIFF_MAX ) {
        fprintf( stderr, emsg,(int)n, ( size_t ) mem_ll, ARENAS_MAX_ALLOC);
        exit(EXIT_FAILURE);
    } else if (mem_ll > (ssize_t)ARENAS_MAX_ALLOC) {
        fprintf( stderr, emsg,(int)n, ( size_t ) mem_ll, ARENAS_MAX_ALLOC );
        exit(EXIT_FAILURE);
    } else if ((size_t)mem_ll >  ARENAS_MAX_ALLOC - tot_mem_usage ) {
        fprintf( stderr, emsg3,(int)n, (size_t) nelem, ( size_t ) mem_sz, (ARENAS_MAX_ALLOC-tot_mem_usage) );
        exit(EXIT_FAILURE);
    } else {
        void *ptr = arena_alloc( n, (size_t) mem_ll ); // Logging of memory is done in arena_alloc.
        return memset( ptr, 0, (size_t) mem_ll );
    }
}


/**
 * @brief Deallocate all objects from a lifetime, when their time is up, but retain the
 * memory for the allocatation of a new set of objects in another lifetime.
 * @param n The index of the arena to deallocate.
 * @details
 * The memory aren't freed from the arena, so it is available for quick reuse.
 */
void arena_dealloc( size_t n )
{
    assert( arenas_initialized == true ) ;

    if ( n >= ARENAS_MAX ) {
        fprintf( stderr, msgBadArena, n, ARENAS_MAX );
        abort(  ); // Overflow conditions.
    }
    arenas[n] = first[n].next;
    if ( arenas[n] ) {
        arenas[n]->begin = ( char * ) arenas[n] + sizeof *arenas[n];
       // Works out beautifully with _alloc(),  which resets.
    } else {
        arenas[n] = &first[n];
    }
}

/** @} */

/**
 * @defgroup ArenaInitDeinit Indiviual Arena initialization and destruction functions. 
 * @brief Functions that configures an individual arena, and frees memory back when done.
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
    assert( arenas_initialized == true ) ;
#ifndef CORE_ARENA_NO_LOGGING 
    if (core_arena_log_level > NO_ARENA_LOGGING ) { 
        static bool log_inited;
        if ( !log_inited ) {
            atexit( report_memory_usage );
            log_inited = true;
        }
    }
#endif
    if ( n >= ARENAS_MAX ) {
        fprintf( stderr, msgBadArena, n, ARENAS_MAX );
        abort(  ); // Overflow conditions.
    }

    static const char *emsg = "arena_create: Couldn't allocate memory for arena with chunk_sz: %lu.";
    /// @todo reuse error message as well.
   // First reject anything nonsensical or excessively large .
    if ( chunk_sz == 0 || chunk_sz > PTRDIFF_MAX ) {
        fprintf( stderr, emsg, chunk_sz );
        abort(  );
    }

    first[n].next = _arena_init( n, chunk_sz );

    first[n].chunk_sz = first[n].next->chunk_sz ;
    // default chunk_sz for each block for arena[n] adjusted for padding ;
    /// @todo: first step, next is to subtract AHS. (ideal sizes pagesize wise).
    if ( first[n].next == NULL ) {
        fprintf( stderr, emsg, chunk_sz );
        abort(  );
    }
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
    assert( arenas_initialized == true ) ;

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
#ifndef CORE_ARENA_NO_LOGGING 
    ///@todo log freeing during destroy with block count;
    if (core_arena_log_level >= LOG_CHUNK_MALLOCS  ) {
        tot_mem_usage -= arenas_mem_malloced[n] ;
        tot_mem_usage -= arenas_mem_mmapped[n] ;
    }
#endif
    first[n].next = NULL;
    arenas[n] = NULL;
}

/** @} */
/** @} */
