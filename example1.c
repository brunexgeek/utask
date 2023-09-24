#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* #define UTASK_MULTI_THREAD */
#define UTASK_IMPL
#include "utask.h"

struct tester_args {
	char *name;
	int iters;
};

static void process( const char *name, int i )
{
	printf("Task #%d of %d [%s] Iteration %d\n", utask_id(), utask_count(), name, i);
	utask_yield();
}

static void tester(void *arg)
{
	int i;
	struct tester_args *ta = (struct tester_args *)arg;
	for (i = ta->iters; i  > 0; i--)
		process(ta->name, i);
	free(ta->name);
	free(ta);
}

static void create_test_task(const char *name, int iters)
{
	struct tester_args *ta = malloc(sizeof(*ta));
	ta->name = malloc(strlen(name) + 1);
	if (ta->name) strcpy(ta->name, name);
	ta->iters = iters;
	utask_create(tester, ta);
}

int main(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	utask_initialize(UT_DEFAULT, UT_DEFAULT, UT_DEFAULT);
	create_test_task("Alfa", 2);
	create_test_task("Beta", 5);
	utask_run();
	printf("Finished running all tasks!\n");

	create_test_task("Gamma", 4);
	utask_run();
	printf("Finished running all tasks!\n");
	utask_terminate();

	return EXIT_SUCCESS;
}
