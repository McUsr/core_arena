# vim: ft=make foldlevel=99 spl= sts=0 sw=2 ts=2
# Generic Makefile for  C-programs.
# It is intended to be run to be run from vim, or from the commandline.
# The intent is that you use direnv in the directory and edit the 
# TARGET value in that, to set the correct target.
# if there are TARGETS (PLURAL), then you should have one production rule for each,
# and at that time you might as well, specify the TARGETS within the makefile.
#
#
#
# https://www.lucavall.in/blog/crafting-clean-maintainable-understandable-makefile-for-c-project
# So, I snatch the details with bin_dir and source dir from here maybe.
# 23-10-25T20:43 from here on onwards I'll let gcc make the dependencies.

# so for the library makefile I want one extra little level of functionality
# for compiling for testing purposes.
#
# The convention is, to keep things simple, so we have separate makefiles in the
# tests directory. and a label here that invokes it. (should cd on same line).
# or we just invoke a script that does it for us.

CP = cp
DIFF = diff
LN = ln
MKDIR = mkdir
RM = rm
TAR = tar
CTAGS = ctags


SRC_DIR := src
OBJ_DIR := build
INCLUDE_DIR = src
# The above one, I don't use for gcc.
TESTS_DIR := tests
BIN_DIR := bin

SRC := $(wildcard $(SRC_DIR)/*.c)

OBJ := $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
# OBJ := $(pathsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRC))
# above not working!
HDR := $(wildcard $(INCLUDE_DIR)/*.h)
EXE := emalloc 

ifeq ($(origin TARGET),undefined)
	TARGET := $(BIN_DIR)/$(EXE)
endif

CPPFLAGS := $(CPPFLAGS) -MMD -MP

ifeq ($(origin BUILD),undefined)
	# https://stackoverflow.com/questions/38801796/how-to-conditionally-set-makefile-variable-to-something-if-it-is-empty
	BUILD := debug
endif

# https://stackoverflow.com/questions/8035394/gnu-make-how-to-make-conditional-cflags
# When I specify another build variable on make's command line, this set of specific options 
# are used.
#
# cflags.common := -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=500 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -fPIC
cflags.common := -Wall -Wextra -Wpedantic -fPIC
cflags.debug := -g3 -O0  -static-libasan
cflags.sanitize := -g3 -O0 -fsanitize=address,undefined 
cflags.release = -O2 
CFLAGS  := $(cflags.$(BUILD)) $(cflags.common)
# LDFLAGS = -L/usr/local/lib/so64 -Llib
	LDFLAGS =
# LDLIBS  = -lcoolbeans
  LDLIBS =

# generating a dist file of the version.
# https://github.com/JnyJny/meowmeow/blob/master/Makefile 
MANIFEST = $(SRC) $(HDR) README.md Makefile License
VERSION = 0.0.1
SDIST_ROOT = dist
SDIST_TARFILE=$(SDIST_ROOT)-$(VERSION).tar.gz

.PHONY: all run deps tag gdb asm od memcheck1 sdist clean clobber tagsrc

all: $(TARGET) | tag dox

# $(TARGET): $(OBJ) | $(BIN_DIR)
# 	$(CC) -shared -o $(TARGET) $(OBJ)
# 	sudo cp $(TARGET) /usr/local/lib/so64
# 	sudo ldconfig

$(TARGET):  $(TESTS_DIR)/$(EXE:%=%.c) $(OBJ) | $(BIN_DIR)
	$(CC) -std=c99 $(CPPFLAGS) $(CFLAGS) -o $@ $(OBJ)  $(TESTS_DIR)/$(EXE:%=%.c)


$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) -std=c99 $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/%.c :: $(HDR)
	@touch $@

$(BIN_DIR) $(OBJ_DIR):
	mkdir -p $@

run:
	$(TARGET)

# figure out dependencies;
# make -n -W <file> lets me run a "what if" <file> was new.
deps:
	gcc -MM $(SRC)


# Another way of doing this:
	
tagsrc: namefile

namefile: $(SRC)
	@gcc -MM $(SRC) >namefile

tag: tagsrc
	/usr/bin/cscope -bq -inamefile 2>/dev/null
	ctags --c-types=f -f functions $(SRC)
	ctags  $(SRC)

gdb:
	gdb -q -tui $(TARGET)

asm:
	gcc  -S $(OBJECTS) -fverbose-asm -O2 -o -

od:
	objdump -S $(TARGET)

memcheck1:
	valgrind -s --leak-check=full  --show-leak-kinds=all $(TARGET) 

dox:
	@doxygen >/dev/null 2>&1

idx:
	xdg-open html/index.html

sdist: clobber
	$(MKDIR) -p $(SDIST_ROOT)
	$(TAR) zcf $(SDIST_TARFILE) $(SDIST_ROOT)

clean:
	@$(RM) -rv $(BIN_DIR) $(OBJ_DIR) namefile

clobber: clean
		@$(RM) -f $(TARGET) $(SDIST_TARFILE)
		@$(RM) -fr $(SDIST_ROOT)

