MODULE = podtopod

objects := $(patsubst %.c,%.o,$(wildcard libipod/src/*.c))

obj-m = podtopod.o connect.o artist.o album.o song.o list_utils.o transfer.o $(objects) 

CFLAGS += -Ilibipod/include


