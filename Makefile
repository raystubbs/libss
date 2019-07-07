CC ?= gcc

build: ss.h ss.c
	$(CC) -O3 ss.c -shared -fpic -o libss.so
	$(CC) -c -O3 ss.c -o ss.o
	ar rcs libss.a ss.o
	rm ss.o

test: test.c ss.h ss.c
	$(CC) -O3 ss.c test.c -o test
	./test
	rm test

clean:
	- rm *.o *.so *.a
	- rm test
