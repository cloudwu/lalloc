#ifndef lalloc_testcase_h
#define lalloc_testcase_h

enum TEST_ACTION {
	TEST_ALLOC,
	TEST_FREE,
	TEST_REALLOC,
};

struct test_item {
	enum TEST_ACTION action;
	int index;
	int size;
	int osize;
};

#endif
