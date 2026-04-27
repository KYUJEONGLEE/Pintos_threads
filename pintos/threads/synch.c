/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void priority_donation (struct thread *holder, struct thread *current);
static bool priority_less (const struct list_elem *a_,
						   const struct list_elem *b_, void *aux UNUSED);

static bool
priority_less (const struct list_elem *a_, const struct list_elem *b_,
			   void *aux UNUSED)
{
	const struct thread *a = list_entry (a_, struct thread, elem);
	const struct thread *b = list_entry (b_, struct thread, elem);

	return a->priority < b->priority;
}

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value)
{
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0)
	{
		list_insert_ordered (&sema->waiters, &thread_current ()->elem,
							 priority_higher, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema)
{
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema)
{
	enum intr_level old_level;
	struct thread *unblocked_thread = NULL;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters))
	{
		// waiters에 있는 스레드들 우선순위 순서대로 다시 정렬
		// 한번 더 sort? => donation 고려
		list_sort (&sema->waiters, priority_higher, NULL);
		// 가장 우선순위 높은 스레드를 waiters에서 꺼내줌
		unblocked_thread
			= list_entry (list_pop_front (&sema->waiters), struct thread, elem);
		// 꺼낸 스레드를 ready_list에 넣어서 실행 가능한 상태로 바꿔줌
		thread_unblock (unblocked_thread);
	}
	sema->value++;
	intr_set_level (old_level);

	// 깨운 스레드가 있고, 그 스레드가 현재 스레드보다 우선순위가 높으면
	// 양보해야함. waiters 에서 깨운 쓰레드가 없는데 그 priority에 접근하면
	// 터짐.
	if (unblocked_thread != NULL && !intr_context ()
		&& unblocked_thread->priority > thread_current ()->priority)
	{
		thread_yield ();
	}
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void)
{
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));
	struct thread *holder = lock->holder;
	struct thread *current = thread_current ();
	if (holder != NULL)
	{
		current->waiting_lock = lock;
		struct thread *donor = current;
		while (donor->waiting_lock != NULL && donor->waiting_lock->holder != NULL)
		{
			holder = donor->waiting_lock->holder;
			priority_donation (holder, donor);
			donor = holder;
		}
	}
	sema_down (&lock->semaphore);
	current->waiting_lock = NULL;
	lock->holder = current;
	/* lock을 실제로 얻은 뒤, 현재 스레드가 보유한 lock 목록에 기록한다. */
	list_push_back (&thread_current ()->held_locks, &lock->hold_elem);
}
/*
holder의 priorty가 현재 쓰레드의 우선순위보다 낮으면 우선순위를 높여준다.
(donation) donation 받기 전 기존 우선순위를 original priority에 저장한다.
*/
static void
priority_donation (struct thread *holder, struct thread *current)
{
	if (holder->priority < current->priority)
	{
		/* original_priority는 보존하고, 현재 적용 priority만 donation 값으로
		 * 올린다. */
		holder->priority = current->priority;
		holder->is_donated = true;
	}
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
	{
		lock->holder = thread_current ();
		/* try_acquire로 얻은 lock도 release 시 priority 재계산 대상에 포함한다.
		 */
		list_push_back (&thread_current ()->held_locks, &lock->hold_elem);
	}
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock)
{
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));
	struct thread *current = thread_current ();
	struct list_elem *cur_elem;

	/* 방금 놓는 lock은 더 이상 current의 donation 근거가 아니므로 목록에서
	 * 제거한다. */
	list_remove (&lock->hold_elem);

	current->priority = current->original_priority;
	current->is_donated = false;
	/* 아직 들고 있는 lock들의 waiters를 보고 남아 있는 donation 중 최댓값을
	 * 반영한다. */
	for (cur_elem = list_begin (&current->held_locks);
		 cur_elem != list_end (&current->held_locks);
		 cur_elem = list_next (cur_elem))
	{
		struct lock *held_lock = list_entry (cur_elem, struct lock, hold_elem);
		struct list *waiters = &held_lock->semaphore.waiters;
		if (!list_empty (waiters))
		{
			struct thread *max_priority_thread = list_entry (
				list_max (waiters, priority_less, NULL), struct thread, elem);
			if (max_priority_thread->priority > current->priority)
			{
				current->priority = max_priority_thread->priority;
				current->is_donated = true;
			}
		}
	}
	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock)
{
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem
{
	struct list_elem elem;		/* List element. */
	struct semaphore semaphore; /* This semaphore. */
};

static bool
cond_priority_higher (const struct list_elem *a_, const struct list_elem *b_,
				 void *aux UNUSED)
{
	/*
		a_와 _b는 struct semaphore_elem 의 elem의 주솟값을 가진다.
		list_entry로 semaphore_elem 의 주솟값을 얻음(a)
	*/ 
	const struct semaphore_elem *a = list_entry (a_, struct semaphore_elem, elem);
	const struct semaphore_elem *b = list_entry (b_, struct semaphore_elem, elem);
	/*
		위에서 얻어온 a(= semaphore_elem의 주솟값)에서 semaphore.waiters 리스트의 가장 첫 원소에 접근
		그 waiters에 들어가 있는 thread에 접근하기 위해서 elem을 thread로 다시 한번 더 변환을 시켜줘야 함.
		waiters의 원소가 thread.elem인 이유? => sema_down() 함수에서 thread_current()->elem을 push 함.
	*/
	const struct thread *a_thread = list_entry (list_front (&a->semaphore.waiters),
                       struct thread, elem);

	const struct thread *b_thread = list_entry (list_front (&b->semaphore.waiters),
                       struct thread, elem);

	/*
		list_max 를 사용하려면 부호 방향을 바꿔줘야 한다.
		왜?
		list_max 는 내부에서 less(max, e, aux)를 호출하는데 
		인자로 받는 bool less는 max 가 e보다 작으면 true 를 반환한다.
		그래서 b가 a보다 클 때 true 여야 한다.
	*/
	return a_thread->priority < b_thread->priority;
}

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock)
{
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ())
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_push_back (&cond->waiters, &waiter.elem);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
/*
	condition variable에서 wait 하고 있는 thread 하나를 깨우는 함수
	cond_wait()는 조건이 맞지 않아서 잠 재우는 함수(cond->waiters에 등록함)
	cond_signal()은 아무나 깨우는 것이 아님.
	priority가 가장 높은 waiter 를 깨워야 한다.
*/
void
cond_signal (struct condition *cond, struct lock *lock UNUSED)
{
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)){
		// 가장 높은 priority의 thread를 가져오기 위해 list_max()에서 priority max 값을 가져온다.
		struct list_elem *max_elem = list_max(&cond->waiters, cond_priority_higher, NULL);
		list_remove(max_elem);
		struct semaphore *waiting_sema = &list_entry (max_elem, struct semaphore_elem, elem)->semaphore;
		sema_up (waiting_sema);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock)
{
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
