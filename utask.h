/*
 *   Copyright 2022 Bruno Ribeiro
 *   <https://github.com/brunexgeek/utask>
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#ifndef UTASK_H
#define UTASK_H

#define UT_DEFAULT -1

#define UT_EOK        0  /* OK */
#define UT_EINIT     -1  /* Already initialized */
#define UT_EUNINIT   -2  /* Uninitialized */
#define UT_EMEMORY   -3  /* Not enough memory */
#define UT_EMANY     -4  /* Too many tasks */
#define UT_EBUSY     -5  /* Operation cannot be performed at the moment */
#define UT_EINVALID  -6  /* Invalid argument */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the task scheduler for the current thread.
 *
 * If UTASK_MULTI_THREAD is not defined before 'utask.h' is included
 * only one thread can use tasks. Otherwise, this function can be called
 * for each thread to initialize its task scheduler.
 *
 * This function must be called outside a task.
 */
int utask_initialize( int max, int stack_size, int flags );

/**
 * Terminate the task scheduler for the current thread.
 *
 * This function must be called outside a task.
 */
int utask_terminate();

/**
 * Create a new task and add it in the task scheduler's running queue.
 *
 * This function can be called inside and outside a task.
 *
 * @return Task identifier (same value returned by 'utask_id' function).
 */
int utask_create(void (*func)(void*), void *arg);

/**
 * Run the task scheduler until all tasks are completed.
 *
 * Tasks are selected via round-robin algorithm.
 */
int utask_run();

/**
 * Relinquish control and enables the task scheduler run another task.
 */
int utask_yield();

/**
 * Return the current task identifier.
 */
int utask_id();

/**
 * Return the number of tasks in runnable state.
 */
int utask_count();

typedef struct
{
	struct
	{
		int max;
		int count;
	} task;
}  utask_info_t;

/**
 * Returns information about the task scheduler state.
 */
int utask_info( utask_info_t *info );

#ifdef __cplusplus
}
#endif

#endif /* UTASK_H */

#ifdef UTASK_IMPL

#include <ucontext.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define UT_RUNNING 0x1000000
#define UT_READY   0x2000000

#define ST_EMPTY   0
#define ST_CREATED 1
#define ST_RUNNING 2
#define ST_DONE    3

typedef struct utask
{
	int status;
	int id;
    int stack_size;
    void *stack;
    void *stack_start;
    int stack_usage;
    void (*func)(void*);
	void *arg;
	ucontext_t ctx;
} utask_t;

#define UT_INIT       0
#define UT_SCHEDULE   1
#define UT_EXIT_TASK  2

typedef struct uscheduler
{
	ucontext_t ctx;
	int current;
	int count;
	int flags;
	int stack_size;
	int max;
	utask_t *task_list;
	uint64_t nswitches;
    #ifdef UTASK_MULTI_THREAD
    struct uscheduler *next;
    pid_t tid;
    #endif
} uscheduler_t;

/**
 * Static storage for every task scheduler (single- or multi-thread)
 */
static uscheduler_t *priv = NULL;

#ifdef UTASK_MULTI_THREAD

#define UT_PRIV get_scheduler()

static int utask_thread_id();

static uscheduler_t *get_scheduler()
{
    if (!priv) return NULL;

    uscheduler_t *sc = priv;
    int tid = utask_thread_id();
    while (sc && sc->tid != tid) sc = sc->next;
    return sc;
}

static uscheduler_t *prepare_scheduler()
{
    uscheduler_t *sc = NULL;;
    int tid = utask_thread_id();

    if (priv)
    {
        sc = priv;
        while (sc && sc->tid != tid) sc = sc->next;
        if (sc) return sc;
    }

    sc = (uscheduler_t *) calloc(1, sizeof(uscheduler_t));
    sc->tid = tid;
    sc->next = priv;
    return priv = sc;
}

static uscheduler_t *remove_scheduler()
{
    uscheduler_t *sc = priv;
    uscheduler_t *psc = NULL;
    int tid = utask_thread_id();
    while (sc && sc->tid != tid)
    {
        psc = sc;
        sc = sc->next;
    }
    if (!sc) return NULL;
    if (priv == sc)
        priv = sc->next;
    else
        psc->next = sc->next;
    sc->next = NULL;
    return sc;
}

#else

#define UT_PRIV priv

uscheduler_t *prepare_scheduler()
{
    if (priv) return priv;
    return priv = (uscheduler_t *) calloc(1, sizeof(uscheduler_t));
}

static uscheduler_t *remove_scheduler()
{
    uscheduler_t *sc = priv;
    priv = NULL;
    return sc;
}

#endif

int utask_initialize( int max, int stack_size, int flags )
{
    (void) flags;
    if (stack_size <= 0)
        stack_size = 16 * 1024;
    if (max <= 0)
        max = 16;
    else
    if (max > 64)
        return UT_EMANY;

    uscheduler_t *sc = prepare_scheduler();
    if (!sc)
        return UT_EMEMORY;
    if (sc->flags & UT_READY)
        return UT_EINIT;

    sc->task_list = NULL;
	sc->current = -1;
	sc->count = 0;
	sc->flags = (flags & 0x00FFFFFF) | UT_READY;
    sc->stack_size = (stack_size + 4095) & (~4095);
    sc->max = max;
    #ifdef UTASK_MULTI_THREAD
        sc->tid = utask_thread_id();
    #endif

    sc->task_list = malloc(sizeof(utask_t) * max);
    if (!sc->task_list) return UT_EMEMORY;

	for (int i = 0; i < sc->max; ++i)
	{
		sc->task_list[i].status = ST_EMPTY;
		sc->task_list[i].id = i;
	}

    return UT_EOK;
}

int utask_terminate()
{
    uscheduler_t *sc = remove_scheduler();
    if (!sc) return UT_EUNINIT;
    if (sc->flags & UT_RUNNING)
    {
        /* TODO: handle this case better (instead of removing and adding back) */
        #ifdef UTASK_MULTI_THREAD
            sc->next = priv;
        #endif
        priv = sc;
        return UT_EBUSY;
    }

    if (sc->task_list)
    {
        /* TODO: remove stacks from unfinished tasks (probably not possible currently) */
        free(sc->task_list);
    }
    sc->task_list = NULL;
    free(sc);

    return UT_EOK;
}

/* Task entry point. Must use C linkage (add 'extern "C"' if using C++ compiler) */
static void utask_entry(int i)
{
    uscheduler_t *sc = UT_PRIV;
	utask_t *task = sc->task_list + i;

    task->stack_start = &task;
	task->func(task->arg);
	task->status = ST_DONE;
    swapcontext(&sc->task_list[sc->current].ctx, &sc->ctx);
}

int utask_create( void (*func)(void *), void *arg )
{
    uscheduler_t *sc = UT_PRIV;
    if (!sc) return UT_EUNINIT;
	if (sc->count >= sc->max) return UT_EMANY;

    /* find the first empty task*/
	int i = 0;
	while (i < sc->max && sc->task_list[i].status != ST_EMPTY) ++i;
	if (i >= sc->max) return UT_EMEMORY;

	utask_t *task = sc->task_list + i;
	task->status = ST_CREATED;
    task->stack_size = sc->stack_size;
    task->stack = malloc(task->stack_size);
    task->stack_usage = 0;
    task->id = i;
    task->func = func;
    task->arg = arg;

    memset(&task->ctx, 0, sizeof(ucontext_t));
    if (getcontext(&task->ctx) < 0)
    {
        task->status = ST_EMPTY;
        free(task->stack);
    }
    task->ctx.uc_stack.ss_sp   = task->stack;
	task->ctx.uc_stack.ss_size = task->stack_size;
    task->ctx.uc_link = NULL;
	makecontext(&task->ctx, (void(*)()) utask_entry, 1, task->id);

	sc->count++;
	return task->id;
}

static int utask_choose_task( uscheduler_t *sc )
{
	utask_t *task = NULL;

	if (sc->count == 0) return -1;

    /* point to the next task*/
	int current = 0;
	if (sc->current >= 0)
		current = (sc->current + 1) % sc->max;
    /* find the next valid task (ST_RUNNING or ST_CREATED)*/
	for (int i = 0; i < sc->max; ++i)
    {
		task = sc->task_list + current;
		if (task->status == UT_RUNNING || task->status == ST_CREATED)
			return current;
		current = (current + 1) % sc->max;
	} while (1);
}

static void utask_stack_overflow( uscheduler_t *sc )
{
    utask_t *task = sc->task_list + sc->current;
    int size = (int) ((long)task->stack_start - (long) &task);
    size = (size < 0) ? -size : size;

    if (size > task->stack_size - 32)
    {
        fprintf(stderr, "*** stack overflow at task #%d stack_size=%d stack_usage=%d ***\n",
            sc->current, task->stack_size, (int) size);
        abort();
    }
    task->stack_usage = size;
}

int utask_yield()
{
    uscheduler_t *sc = UT_PRIV;
    if (!sc) return UT_EUNINIT;
	int count = sc->nswitches;
    utask_stack_overflow(sc);
    swapcontext(&sc->task_list[sc->current].ctx, &sc->ctx);
    return sc->nswitches - count;
}

int utask_run()
{
    uscheduler_t *sc = UT_PRIV;
    if (!sc) return UT_EUNINIT;

    sc->flags |= UT_RUNNING;
    sc->current = -1;

    do
    {
        sc->current = utask_choose_task(sc);
        if (sc->current >= 0)
        {
            utask_t *task = sc->task_list + sc->current;
			sc->nswitches++;
            swapcontext(&sc->ctx, &task->ctx);
            if (task->status == ST_DONE)
            {
                task->status = ST_EMPTY;
                sc->count--;
                free(task->ctx.uc_stack.ss_sp);
            }
        }
	} while (sc->current >= 0);
    sc->flags &= ~UT_RUNNING;

    return 0;
}

int utask_info( utask_info_t *info )
{
	if (!info) return UT_EINVALID;
    uscheduler_t *sc = UT_PRIV;
	if (!sc) return UT_EUNINIT;

	info->task.count = sc->count;
	info->task.max = sc->max;

	return UT_EOK;
}

int utask_id()
{
    uscheduler_t *sc = UT_PRIV;
	if (!sc) return -1;
	return sc->current;
}

int utask_count()
{
    uscheduler_t *sc = UT_PRIV;
	if (!sc) return 0;
	return sc->count;
}

#ifdef UTASK_MULTI_THREAD

#include <sys/syscall.h>
#include <unistd.h>

static pid_t utask_thread_id()
{
    return (pid_t) syscall(SYS_gettid);
}

#endif

#endif /* UTASK_IMPL */
