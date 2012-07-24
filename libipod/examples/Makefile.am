noinst_PROGRAMS = dumptracks dumptracks_cpp readwritedb teststrings

INCLUDES = -I../src -I../include

dumptracks_SOURCES = dumptracks.c
dumptracks_LDFLAGS = -L../src -L../src/.libs
dumptracks_LDADD = -lipod -lexpat
dumptracks_DEPENDENCIES = ../src/.libs/libipod.a

dumptracks_cpp_SOURCES = dumptracks_cpp.cpp
dumptracks_cpp_LDFLAGS = -L../src -L../src/.libs
dumptracks_cpp_LDADD = -lipod
dumptracks_cpp_DEPENDENCIES = ../src/.libs/libipod.a

readwritedb_SOURCES = readwritedb.c
readwritedb_LDFLAGS = -L../src -L../src/.libs
readwritedb_LDADD = -lipod
readwritedb_DEPENDENCIES = ../src/.libs/libipod.a

teststrings_SOURCES = teststrings.c
teststrings_LDFLAGS = -L../src -L../src/.libs
teststrings_LDADD = -lipod
teststrings_DEPENDENCIES = ../src/.libs/libipod.a
