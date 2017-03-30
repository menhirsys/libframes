.SUFFIXES:

.PHONY:
run_tests: test
	./test

CFLAGS=-std=c99 -pedantic

test: $(shell git ls-files)
	$(CC) $(CFLAGS) -o $@ test.c
