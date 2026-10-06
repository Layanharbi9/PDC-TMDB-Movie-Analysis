# Generic Makefile for OpenMP and MPI C programs on macOS

# Compilers
CC_OPENMP = gcc-14
CC_MPI    = mpicc

# Flags
SDK     := $(shell xcrun --show-sdk-path)
CFLAGS_OPENMP = -fopenmp -lm -isysroot $(SDK)
CFLAGS_MPI    = -isysroot $(SDK)

# Detect all C files
SRC_FILES = $(wildcard *.c)
TARGETS   = $(patsubst %.c,%,$(SRC_FILES))

.PHONY: all clean run

# Build rule: choose compiler automatically based on header
all: $(TARGETS)

%: %.c
	@if grep -q "mpi.h" $<; then \
		echo "🔧 Building $@ with MPI..."; \
		$(CC_MPI) $(CFLAGS_MPI) $< -o $@; \
	else \
		echo "🔧 Building $@ with OpenMP..."; \
		$(CC_OPENMP) $(CFLAGS_OPENMP) $< -o $@; \
	fi

# Run last built program (you can specify RUN=example1)
run:
	@if [ -z "$(RUN)" ]; then \
		echo "Usage: make run RUN=<program_name>"; \
	else \
		if grep -q "mpi.h" $(RUN).c; then \
			mpirun -np 4 ./$(RUN); \
		else \
			./$(RUN); \
		fi \
	fi

# Clean
clean:
	rm -f $(TARGETS)
