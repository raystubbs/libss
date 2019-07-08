CC      ?= gcc
CCFLAGS := -std=c99 -O3 -Wall

build: ss.h ss.c
	$(CC) $(CCFLAGS) ss.c -shared -fpic -o libss.so
	$(CC) -c $(CCFLAGS) ss.c -o ss.o
	ar rcs libss.a ss.o
	rm ss.o

test: test.c ss.h ss.c
	$(CC) $(CCFLAGS) ss.c test.c -o test
	./test
	rm test

clean:
	- rm *.o *.so *.a
	- rm test
