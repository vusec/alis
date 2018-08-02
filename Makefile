proj_name := alis
lib_variants := standalone
EXTRA_CFLAGS ?= -DNDEBUG

standalone_objs := arena_mgmt.o arena.o map.o mergeheap.o

ramses_path := ./ramses
ramses_ipath := $(ramses_path)/include
ramses_ar := $(ramses_path)/libramses.a

OFLAGS := -O2
CFLAGS := -std=c99 -Wall -Wpedantic -pedantic -fPIC $(OFLAGS) $(EXTRA_CFLAGS)

libname := lib$(proj_name)

libs := $(patsubst %,$(libname)-%,$(lib_variants))
solibs := $(patsubst %,%.so,$(libs))
arlibs := $(patsubst %,%.a,$(libs))

targets := $(solibs) $(arlibs)

all: $(targets)

$(ramses_ar):
	$(MAKE) -C $(ramses_path) $(notdir $@)

%.o: %.c %.h
	$(CC) $(CFLAGS) -I$(ramses_ipath) -c $<

arena_mgmt.o: arena_mgmt.c arena_mgmt.h arena.h ceildiv.h
arena.o: arena.c arena.h mergeheap.h ceildiv.h

$(libname)-standalone.a: $(standalone_objs)
	ar -rcs $@ $?

$(libname)-standalone.so: $(standalone_objs)
	$(CC) -shared -o $@ $^

test/%.run: test/%.c $(targets) $(ramses_ar)
	$(CC) $(CFLAGS) -I. -I$(ramses_ipath) -o $@ $< $(libname)-standalone.a $(ramses_ar)

test_runs := $(patsubst %.c,%.run,$(wildcard test/test_*.c))
tests: $(test_runs)

test: all tests
	@cd test && for i in $(test_runs); do echo "Running $${i}..."; ./`basename $${i}` && echo 'OK' || echo 'FAILED'; done


.PHONY: clean cap

clean:
	rm -f *.o $(targets) test/*.run
	$(MAKE) -C $(ramses_path) clean

cap_bins := test/test_standalone.run test/test_standalone_pa.run
cap:
	for i in $(cap_bins); do setcap cap_sys_admin,cap_dac_read_search,cap_ipc_lock+ep $${i}; done
