README for the core_arena library.
==================================

Objective
---------

Make a heap based arena allocation library for the Linux platform, that is
suitable for accessing many small items, note of equal size  that *can* release
memory back into `malloc's` pool of free memory if the allocated chunks are less
than 128K, otherwise if chunks arena larger than 128K and `mmap` is implicitly
invoked, release memory back to the system pool. Also safeguard against
allocating more memory than physically available, and provide a logging system
that is run time configurable, and can be used for tuning, that can be opted out
of compile time.

Advantages
-------------

The library lets you allocate memory for an arena from blocks of memory, that
you can later de allocate collectively.

* This makes it easier to release memory no longer need by a stage, object or
routine in your program when you are done with it, freeing it to the process
heap, or the system memory pool, depending on the size of the allocation.

* It is especially useful in situations, where several lifespans memory wise, meets.

* It may make overall memory management more efficient, minimizing the number of
`malloc's` and especially `free's` you have to do in your code.

* It can help keep the memory as defragmented as possible using bigger blocks of
the process heap, than allocating small objects.

* There are runtime logging facilities to help you inspect the library's memory
consumption, for tuning, and macros/functions for finding the optimal block size,
once you are content, you can opt out the logging facility compile time before
you ship. (See: [logging system](notes/logging.md).)

Motivation
----------

Be able to use and reuse memory safer, still fast, and efficiently, and have the ability to
in some situations tune the memory allocations, by watching the logs of our
allocations.

Constraints
-----------

We can't `realloc` dynamic arrays, any arrays you want to use realloc to grow, or
shrink, needs to have been `calloc'ed` by your system library.

* We don't under any circumstances use more memory than the system report as 
physically available.

* The library isn't thread safe in any respect, and is for single threaded
programs.


Features
--------

A runtime logging system, you can opt out of compile time by defining
`-DCORE_ARENA_NO_LOGGING`.

Building
--------

The library is built by a GNU Make make file but you need to do some
work up front by setting some environment variables.


You can build the library both as a static library, as a shared library, or as
an object file linked directly into your executable.


Memory Constraints
------------------

It is not intended to be a general purpose allocation that relies on
over committing memory, We assume that `overcommit_memory==0`, and that 
memory which is physically available, is all you can get. This really isn't then
arena_allocator to use if you want pages with 1 megabyte of consecutive memory.

Should you need more than a chunk size of 128K, then `malloc` will allocate
that memory for you via `mmap`,  and `free` will release the memory back into
then systems pool. Not as free memory within then heap. This is for flexibility,
and that it works for larger chunks than malloc can provide too.


## Requirements

This version is made for the Linux platform and as such, uses Unix system calls
for interacting with the kernel in a more or less portable way, avoiding  Glibc
features where possible, so, it should be easy to port to any other Unix
Systems.

## Developing environment

see Tech Notes.

## Installation

Both the include file **core_arena.h** and the source file **core_arena.c**
should be installed into your project and you should change the path to the
include file into something more fitting if you want to store it apart from
**core_arena.c**.

You can then compile it with your project like your would with any other module.

## Configuration in core_arena.h:

The constants **MAX_ALIGN** and **MALLOC_PTR_SIZE** might need to be recalibrated if
you aren't on a Linux x86-64 system. You may want to change the  constant
**ARENAS_MAX** defines the number of arenas you intend to use, this can't be altered
run-time.

The **MAX_ALIGN** constant denotes the alignment that is used to access memory
efficiently.

The **MALLOC_PTR_SIZE** denotes the pointers size `malloc()` will use for itself
in the allocated block.

The **ARENAS_MAX** is originally configured for two Arenas, (the index of the
arenas starts at `0`).

* Specify the number of arenas your intend to use in `core_arena.h`.

### You need to configure the upper limit of memory.

I recommend you compile and run the `misc/test.c` program: `gcc -g3 -o
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

If you are on a different platform than Linux, you should determine what the page size is on
your system and use that number of bytes as a vantage point for specifying the
chunk size.

### Getting memory from the arena into your program.

You allocate memory for an object in memory with: `void *arena_alloc`,
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
  Last updated:24-01-30 01:03

<!--
vim: foldlevel=99
-->

