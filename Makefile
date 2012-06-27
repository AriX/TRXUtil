CC=gcc

TRXUtil:TRXUtil.o
	$(CC) TRXUtil.o -o $@
	rm -f TRXUtil.o

clean:
	rm -f a.out TRXUtil.o TRXUtil
