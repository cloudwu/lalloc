all:  testalloc

JEMALLOC_STATICLIB := /home/cloud/skynet/3rd/jemalloc/lib/libjemalloc_pic.a
JEMALLOC_INC := /home/cloud/skynet/3rd/jemalloc/include/jemalloc

testcase1.o : testcase1.c
	gcc -Wall -c -o $@ $^

testcase2.o : testcase2.c
	gcc -Wall -c -o $@ $^

testalloc : lualloc.c lualloc.h testalloc.c testcase1.o
	gcc -O2 -Wall -o testalloc lualloc.c testalloc.c testcase1.o -I$(JEMALLOC_INC) $(JEMALLOC_STATICLIB) -lpthread

clean :
	rm testalloc

