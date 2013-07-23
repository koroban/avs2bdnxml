CC=gcc
CFLAGS=-O3 -Wall -DLE_ARCH
LDFLAGS=-lm
OBJS=pgsparse.o
EXE=pgsparse

%.o: %.c
	$(CC) -c $< $(CFLAGS)

$(EXE): $(OBJS)
	$(CC) -o $(EXE) $(OBJS) $(LDFLAGS)

all: $(EXE)

dist: clean all
	strip -s $(EXE)
	upx-ucl --best $(EXE)
	rm -f $(OBJS)

.phony clean:
	rm -f $(EXE) $(OBJS)

