README for the core_arena library.
==================================

## Installation

Both the include file **core_arena.h** and the source file **core_arena.c**
should be installed into your project and you should change the path to the
include file into something more fitting if you want to store it apart from
**core_arena.c**.

You can then compile it with your project like your would with any other module.

## Configuration in core_arena.h:

The constants **MAX_ALIGN** and **MALLOC_PTR_SIZE** might need to be recallibrated if
you aren't on a Linux x86-64 system. You may want to change the  constant
**ARENAS_MAX** defines the number of arenas you intend to use, this can't be altered
run-time.

The **MAX_ALIGN** constant denotes the alignment that is used to access memory
efficiently.

The **MALLOC_PTR_SIZE** denotes the pointers size `malloc()` willl use for itself
in the allocated block.

The **ARENAS_MAX** is originally configured for two Arenas, (the index of the
arenas starts at `0`).

* Specify the number of arenas your intend to use in `core_arena.h`.

### You need to configure the upper limit of memory.

I recommend you compile and run the the `misc/test.c` program: `gcc -g3 -o
memmax misc/test.c` and use the largest value that passes for the `#define ARENAS_MAX_ALLOC`
define constant in src/core_arena.h.


### Configuring logging.

We support logging, so you can deduce the memory usage, the logging is only
intended for testing, and not to be left on in production code.

The **ARENAS_LOG_LEVEL** can be defined to **NONE**, **CHUNKS**, and **EVERYTHING**.
**NONE** means there will be no logging, if **CHUNKS** is specified, then the number of
chunks allocated during the lifetime  of the arena will be reported. If you use
an arena, later destroy it, only to create it a second time, then it is the
number from the  second usage that is reported.
If **EVERYTHING** is specified, then the total bytes allocated from the chunks are
reported too.

## Usage Overview


### Configuring the arena.

* Include `core_arena.h` in any source file you intend to use arena allocation
from.

* You must call `arena_create(n,chunk_sz)` before using the arena.

#### Parameter  explanation:

`n` is the number of the arena you configure, with which you will reference the
arena when using the other calls.

The chunk_sz you specify is the size of memory that will be handled out during
individual `arena_alloc/arena_calloc` calls. Every arena needs a struct for book
keeping,and malloc needs some bytes for book-keeping too. The necessary bytes
for book-keeping are  subtracted from the specified chunk_sz.  I recommend a
chunk_sz of a multiple/part of **4096** (the page size on a Linux platform).

**2048**, **1024**, **512**, **256**, **128**, are examples of good fractions of **4096**.
**3072**, **1536**, **384**, and so on, are also good numbers when you known the
total number of bytes you need and want to cap the bufsize.

If you are on a different platform than Linux, you should determine what the pagesize is on
your system and use that number of bytes as a vantage point for specifying the
chunk size.

### Getting memory from the arena into your program.

You allocate memory for an object in memory with: `void *arena_alloc`.
and memory for a zeroed out array with:  `void *arena_calloc`.

###  Ending/deallocating an arena.

When the collective lifetime for the objects of the arena is over you can delete
them with `arena_dealloc` and retain the memory for reuse with the same arena in
another lifetime, or your can call `arena_destroy`, and return the memory back
to the operating system. 

### Beware.

No calls like `free` or `realloc` from `stdlib.h` will work on pointers to memory returned by
the `arena_*` functions, but most likely generate a segment violation (`SIG_SEGV`) error.

Even if the logging arrays are defined as `long long` I recommend you turn
logging off in production code, because at some time, there will be either an
overflow or wraparound, rendering the numbers useless anyway.  The logging
report is bypassed if you exit your program with `_Exit` or a `TERM` signal, as
the report_usage is installed by `atexit()`.

--------------------------------------
  Last updated:23-12-30 05:41

<!--
vim: foldlevel=99
-->

