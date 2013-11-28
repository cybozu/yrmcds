# Makefile for yrmcds
# Prerequisites: gcc 4.8+ or clang 3.3+

PREFIX = /usr/local
DEFAULT_CONFIG = $(PREFIX)/etc/yrmcds.conf

CC = gcc
CXX = g++
CPPFLAGS = -I. -DCACHELINE_SIZE=$(shell getconf LEVEL1_DCACHE_LINESIZE)
CPPFLAGS += -DDEFAULT_CONFIG=$(DEFAULT_CONFIG) -DUSE_TCMALLOC
OPTFLAGS = -O2 #-flto
DEBUGFLAGS = -gdwarf-3 #-fsanitize=address
WARNFLAGS = -Wall -Wnon-virtual-dtor -Woverloaded-virtual
CPUFLAGS = -march=core2 -mtune=corei7
CXXFLAGS = -std=gnu++11 $(OPTFLAGS) $(DEBUGFLAGS) $(shell getconf LFS_CFLAGS) $(WARNFLAGS) $(CPUFLAGS)
LDFLAGS = -L. $(shell getconf LFS_LDFLAGS)
LIBTCMALLOC = -ltcmalloc_minimal
LDLIBS = $(shell getconf LFS_LIBS) -lyrmcds $(LIBTCMALLOC) -lpthread
CLDOC = LD_LIBRARY_PATH=/usr/local/clang/lib cldoc

HEADERS = $(wildcard src/*.hpp cybozu/*.hpp)
SOURCES = $(wildcard src/*.cpp cybozu/*.cpp)
OBJECTS = $(patsubst %.cpp,%.o,$(SOURCES))

EXE = yrmcdsd
TESTS = $(patsubst %.cpp,%,$(wildcard test/*.cpp))
LIB = libyrmcds.a
LIB_OBJECTS = $(filter-out src/main.o,$(OBJECTS))
PACKAGES = build-essential libgoogle-perftools-dev python-pip

all: $(EXE)
lib: $(LIB)
tests: $(TESTS)

strip: $(EXE)
	nm -n $(EXE) | grep -v '\( [aNUw] \)\|\(__crc_\)\|\( \$$[adt]\)' > $(EXE).map
	strip $(EXE)

install: $(EXE)
	cp etc/upstart /etc/init/yrmcds.conf
	cp etc/logrotate /etc/logrotate.d/yrmcds
	cp etc/yrmcds.conf $(DEFAULT_CONFIG)
	cp $(EXE) $(PREFIX)/sbin/yrmcds
	install -o nobody -g nogroup -m 644 /dev/null /var/log/yrmcds.log

COPYING.hpp: COPYING
	echo -n 'static char COPYING[] = R"(' > $@
	cat $< >>$@
	echo ')";' >>$@
src/main.o: COPYING.hpp

$(OBJECTS): $(HEADERS)

$(LIB): $(LIB_OBJECTS)
	$(AR) rcus $@ $^

$(EXE): src/main.o $(LIB)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

test/%.exe: test/%.o $(LIB)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

$(TESTS): $(LIB)
	@$(MAKE) -s $@.exe
	./$@.exe
	@echo

html:
	@clang++ -v 2>/dev/null || (echo "No clang++ in PATH." && false)
	rm -rf html
	$(CLDOC) generate $(CPPFLAGS) $(CXXFLAGS) -w -D__STRICT_ANSI__ -- --output html --merge docs $(HEADERS)

serve: html
	@cd html; python -m SimpleHTTPServer 8888 || true

clean:
	rm -f src/*.o cybozu/*.o test/*.o test/*.exe COPYING.hpp $(EXE) $(EXE).map $(LIB)

setup:
	sudo apt-get install -y --install-recommends $(PACKAGES)
	sudo pip install cldoc --upgrade

.PHONY: all strip lib tests install html serve clean setup $(TESTS)
