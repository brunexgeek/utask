utask: userspace cooperative mutitasking
========================================

`utask` is a single file library implementing userspace cooperative multitasking inside native threads. Tasks have many-to-one relantionship with native threads (i.e. each thread has its own tasks) and you can create up to 64 tasks per thread.

By default, `utask` can be used only by one thread. If you want to use it in more threads, define `UTASK_MULTI_THREAD` before including `utask.h`. Defining `UTASK_MULTI_THREAD` incurs additional overhead to retrieve per thread internal information.

This library requires `ucontext` API provided by GNU/Linux.

# Usage

Include `utask.h` in your project. You can include `utask.h` in any source file, but one of them (and only one) must define `UTASK_IMPL` before including it. That tells `utask` to define the library implementation. If you want to use tasks in more than one thread, also defines `UTASK_MULTI_THREAD`.

```
/* #define UTASK_MULTI_THREAD */
#define UTASK_IMPL
#include "utask.h"
```

Before using any function you must initialize the task scheduler in the current thread with `utask_initialize`. Always check the return value for errors.

```
int result = utask_initialize(2, UT_DEFAULT, UT_DEFAULT);
if (result != UT_EOK)
    abort("Failure");
```

Now just create some tasks and start the task scheduler to run them. The function `utask_run` will block the execution until all tasks are finished. Make sure that inside `my_function_1` and `my_function_2` you call `utask_yield` regularly to give up processor time to other tasks.

```
utask_create(my_function_1, NULL);
utask_create(my_function_2, NULL);
utask_run();
printf("Finished running all tasks!\n");
```

Remember to terminate the task scheduler to release the allocated resources.

```
utask_terminate();
```
