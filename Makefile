# Makefile for yrmcds
# Prerequisites: gcc 4.8+ or clang 3.3+

PREFIX = /usr/local
DEFAULT_CONFIG = $(PREFIX)/etc/yrmcds.conf

CACHELINE_SIZE := $(shell getconf LEVEL1_DCACHE_LINESIZE)
ifeq ($(CACHELINE_SIZE), 0)
	CACHELINE_SIZE = 32
endif

CC ?= gcc
CXX ?= g++
CPPFLAGS = -I. -DCACHELINE_SIZE=$(CACHELINE_SIZE) $(TCMALLOC_FLAGS)
CPPFLAGS += -DDEFAULT_CONFIG=$(DEFAULT_CONFIG)
OPTFLAGS = -O2 #-flto
DEBUGFLAGS = -gdwarf-3 #-fsanitize=address
WARNFLAGS = -Wall -Wnon-virtual-dtor -Woverloaded-virtual
CPUFLAGS = #-march=core2 -mtune=corei7
CXXFLAGS := -std=gnu++11 $(OPTFLAGS) $(DEBUGFLAGS) $(shell getconf LFS_CFLAGS) $(WARNFLAGS) $(CPUFLAGS) $(CXXFLAGS)
LDFLAGS = -L. $(shell getconf LFS_LDFLAGS)
LDLIBS = $(shell getconf LFS_LIBS) -lyrmcds $(LIBTCMALLOC) -latomic -lpthread
CLDOC := LD_LIBRARY_PATH=$(shell llvm-config --libdir 2>/dev/null) cldoc

HEADERS = $(sort $(wildcard src/*.hpp src/*/*.hpp cybozu/*.hpp))
SOURCES = $(sort $(wildcard src/*.cpp src/*/*.cpp cybozu/*.cpp))
OBJECTS = $(patsubst %.cpp,%.o,$(SOURCES))

EXE = yrmcdsd
TESTS = $(patsubst %.cpp,%,$(sort $(wildcard test/*.cpp)))
LIB = libyrmcds.a
LIB_OBJECTS = $(filter-out src/main.o,$(OBJECTS))
PACKAGES = build-essential libgoogle-perftools-dev python-pip

TCMALLOC_HEADER := $(shell sh ./detect_tcmalloc.sh $(CXX) $(CPPFLAGS))
ifeq ($(TCMALLOC_HEADER), gperftools/tcmalloc.h)
    TCMALLOC_FLAGS = -DUSE_TCMALLOC
    LIBTCMALLOC = -ltcmalloc_minimal
    export TCMALLOC_FLAGS LIBTCMALLOC
else ifeq ($(TCMALLOC_HEADER), google/tcmalloc.h)
    TCMALLOC_FLAGS = -DUSE_TCMALLOC -DTCMALLOC_IN_GOOGLE
    LIBTCMALLOC = -ltcmalloc_minimal
    export TCMALLOC_FLAGS LIBTCMALLOC
endif

all: $(EXE)
lib: $(LIB)
tests: $(TESTS)

strip: $(EXE)
	nm -n $(EXE) | grep -v '\( [aNUw] \)\|\(__crc_\)\|\( \$$[adt]\)' > $(EXE).map
	strip $(EXE)

ifeq ($(wildcard /run/systemd/system), /run/systemd/system)
install-service:
	cp etc/yrmcds.service /etc/systemd/system/yrmcds.service
	systemctl daemon-reload
else
install-service:
	cp etc/upstart /etc/init/yrmcds.conf
endif

install: $(EXE)
	$(MAKE) install-service
	cp etc/logrotate /etc/logrotate.d/yrmcds
	cp etc/yrmcds.conf $(DEFAULT_CONFIG)
	cp $(EXE) $(PREFIX)/sbin/yrmcdsd
	install -o nobody -g nogroup -m 644 /dev/null /var/log/yrmcds.log

COPYING.hpp: COPYING
	echo -n 'static char COPYING[] = R"(' > $@
	cat $< >>$@
	echo ')";' >>$@
src/main.o: COPYING.hpp

$(OBJECTS): $(HEADERS)

$(LIB): $(LIB_OBJECTS)
	$(AR) rcs $@ $^

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
	rm -f src/*.o src/*/*.o cybozu/*.o test/*.o test/*.exe COPYING.hpp $(EXE) $(EXE).map $(LIB)

setup:
	sudo apt-get install -y --install-recommends $(PACKAGES)
	sudo pip install cldoc --upgrade

.PHONY: all strip lib tests install install-service html serve clean setup test_env $(TESTS)
