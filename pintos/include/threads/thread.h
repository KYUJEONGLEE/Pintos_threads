#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "filesys/file.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* States in a thread's life cycle. */
enum thread_status
{
	THREAD_RUNNING, /* Running thread. */
	THREAD_READY,	/* Not running but ready to run. */
	THREAD_BLOCKED, /* Waiting for an event to trigger. */
	THREAD_DYING	/* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) - 1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0	   /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63	   /* Highest priority. */

/*
	MLFQS 정책에서 사용하는 변수들
*/
static const int F = 1 << 14; // 2^14
typedef int fixed_t; // 고정 소수점 type 정의

#define INT_TO_FIXED(n) ((n) * F)
#define FIXED_TO_INT_ZERO(x) ((x) / F)
#define FIXED_TO_INT_NEAR(x) ((x) >= 0 ? ((x) + F / 2) / F : ((x) - F / 2) / F)
#define FIXED_ADD(x, y) ((x) + (y))
#define FIXED_ADD_INT(x, n) ((x) + ((n) * F))
#define FIXED_SUB(x, y) ((x) - (y))
#define FIXED_SUB_INT(x, n) ((x) - ((n) * F))
#define FIXED_MUL(x, y)	((fixed_t)(((int64_t)(x)) * (y) / F))
#define FIXED_MUL_INT(x, n) ((x) * (n))
#define FIXED_DIV(x, y) ((fixed_t)(((int64_t)(x)) * F / (y)))
#define FIXED_DIV_INT(x, n) ((x) / (n))

#define NICE_MAX 20
#define NICE_MIN -20

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread
{
	/* Owned by thread.c. */
	tid_t tid;					 /* Thread identifier. */
	enum thread_status status;	 /* Thread state. */
	char name[16];				 /* Name (for debugging purposes). */
	int priority;				 /* Priority. */
	int original_priority;		 /* donation이 되기전 original priority */
	int64_t wakeup_tick;		 // EY: wakeup_tick 구조체에 추가
	struct list_elem sleep_elem; // EY: sleep_elem으로
	/* Shared between thread.c and synch.c. */
	struct list_elem elem;	   /* List element. */
	struct list held_locks;	   // 현재 들고 있는 lock 목록
	bool is_donated;		   //  현재 priority donation을 받고 있는지 여부
	struct lock *waiting_lock; // 현재 이 쓰레드가 얻으려고 기다리는 lock
	struct list_elem all_elem; // all_thread_list elem
	/*
		MLFQS 정책에서 사용하는 변수들
	*/
	int nice; // 해당 thread가 양보적인지 나타내는 수치
	fixed_t recent_cpu; // 해당 thread가 최근에 사용한 cpu time에 대한 수치를 나타내는 변수
	
	//File Descriptor
	struct file * fdt[128]; //각 파일을 가리키는 인덱스 128짜리 fdt테이블 생성
	int next_fd;


#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4; /* Page map level 4 */
	/*
		그냥 status에 exit status를 저장하면 안된다고 한다.
		status 는 스레드가 실행 중인지/죽는 중인지 의 상태를 저장하는 곳이고,
		exit_status를 따로 할당해서 프로그램이 exit(int)로 종료했다 의 상태를 저장하는 곳이다.
	*/
	int exit_status;
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf; /* Information for switching */
	unsigned magic;		  /* Detects stack overflow. */
};

struct mlfqs_list{
	struct list ready_list;
	int priority;
};
/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void *aux);
tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);
void thread_unblock(struct thread *);

struct thread *thread_current(void);
tid_t thread_tid(void);
const char *thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);

bool priority_higher(const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED);
int thread_get_priority(void);
void thread_set_priority(int);
static int calc_priority(struct thread *t);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

void do_iret(struct intr_frame *tf);

#endif /* threads/thread.h */
