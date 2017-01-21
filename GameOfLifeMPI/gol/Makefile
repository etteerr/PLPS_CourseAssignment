IDIR = -I./parinclude
CFLAGS=$(IDIR) -lm -MMD -MP -fmessage-length=0 -O3 -Wall # -fopenmp -std=c++0x

LIBS=-lgomp

CPP = mpic++

_DEP = GoLMap.h
_OBJ = main.o Gompi.o

DEP = $(patsubst %, ../parinclude/%, $(_DEP))
OBJ = $(patsubst %, ../parobjs/%, $(_OBJ))

#rule
../parobjs/%.o: ../parsource/%.cpp
	@echo 'Building file: $<'
	$(CPP) -c -o $@ $< $(CFLAGS)
	@echo ' '

all:	clean gol-par gol-seq

gol-par: $(OBJ)
	@echo 'Linking executable $@'
	$(CPP) -o $@ $^ $(CFLAGS)

gol-seq: gol-seq.c
	@echo 'Building gol-seq'
	gcc -Wall -O3 -o gol-seq gol-seq.c -lm

clean:
	rm -f *.o gol-seq *~ *core gol-par ../parobjs/*
