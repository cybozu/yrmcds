#!/bin/sh

CXX=$1
shift
CPPFLAGS="$@"

T=$(mktemp --suffix=.cpp)
trap "rm -f $T" INT QUIT TERM HUP 0

f() {
    echo "#include <$1>" >$T
    if $CXX "$CPPFLAGS" -E $T >/dev/null 2>&1; then
        echo $1
        exit 0
    fi
}

f gperftools/tcmalloc.h
f google/tcmalloc.h
