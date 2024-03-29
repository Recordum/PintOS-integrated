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
#include "debug.h"
/* declaration function */
void donate(struct thread* current_thread, struct lock *lock);
bool high_function(const struct list_elem *first, const struct list_elem *second, void *aux UNUSED);
void give_back_prioirty(struct lock *lock, struct thread *current_thread);
bool has_not_lock(struct thread *current_thread);
void set_thread_priority_to_origin(struct thread *current_thread, struct thread *priority_check);
void remove_priority_list(struct thread *current_thread, struct thread *priority_check);
bool high_cond(const struct list_elem *first, const struct list_elem *second, void *aux UNUSED);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
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
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_push_back (&sema->waiters, &thread_current ()->elem);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

bool
high_function(const struct list_elem *first, const struct list_elem *second, void *aux UNUSED){
	const struct thread *thread_first = list_entry(first, struct thread, elem);
	const struct thread *thread_second = list_entry(second, struct thread, elem);
	return thread_first->priority > thread_second->priority;
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
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
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	struct thread* current_thread = thread_current();
	old_level = intr_disable ();
	sema->value++;
	if (!list_empty (&sema->waiters)){
		list_sort(&sema->waiters, high_function, NULL);
		thread_unblock (list_entry (list_pop_front (&sema->waiters),struct thread, elem));
	}
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
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
sema_test_helper (void *sema_) {
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
lock_init (struct lock *lock) {
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
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));
	if(lock->holder != NULL){
		if(lock->holder->priority < thread_current()->priority){
			thread_current()->wait_lock = lock;
			donate(thread_current(), lock);
		}
	}
	sema_down (&lock->semaphore);
	lock->holder = thread_current ();
	list_push_front(&(thread_current()->possesion_lock_list), &lock->lock_elem);
}

// void 
// donate(struct thread* current_thread, struct lock *lock){
//     struct thread *holder = lock->holder;
//     while (holder != NULL && current_thread->priority > holder->priority) {
// 		list_push_front(&(lock->holder->priority_list),&current_thread->priority_elem);
// 		list_sort(&(lock->holder->priority_list), high_function, NULL);
//         holder->priority = current_thread->priority;
//         if (holder->wait_lock != NULL) {
// 			list_pop_front(&(lock->holder->priority_list));
//             lock = holder->wait_lock;
//             holder = lock->holder;
//         } else {
//             holder = NULL;
//         }
//     }
// }
/*Recursion*/
void 
donate(struct thread* current_thread, struct lock *lock){
    struct thread *holder = lock->holder;
    if (holder != NULL && current_thread->priority > holder->priority) {
        list_push_front(&(holder->priority_list),&current_thread->priority_elem);
        list_sort(&(holder->priority_list), high_function, NULL);
        holder->priority = current_thread->priority;
        if (holder->wait_lock != NULL) {
            list_pop_front(&(holder->priority_list));
            donate(current_thread, holder->wait_lock);
        }
    }
}




/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));
	struct thread* current_thread = thread_current();
	list_remove(&lock->lock_elem);
	if(current_thread != idle_thread && !list_empty(&(current_thread->priority_list))){
		give_back_prioirty(lock, current_thread);
	}
	lock->holder = NULL;
	sema_up (&lock->semaphore);

}

void
give_back_prioirty(struct lock *lock, struct thread *current_thread){
	if (!list_empty (&lock->semaphore.waiters)){
		struct thread* priority_check = list_entry(list_begin(&((&lock->semaphore)->waiters)), struct thread, elem);
		priority_check->wait_lock = NULL;
		if (has_not_lock(current_thread)){
			set_thread_priority_to_origin(current_thread, priority_check);
			return;
		}
		remove_priority_list(current_thread, priority_check);
	}
}

bool
has_not_lock(struct thread *current_thread){
	return list_size(&(current_thread->possesion_lock_list)) == 0;
}

void
set_thread_priority_to_origin(struct thread *current_thread, struct thread *priority_check){
	while(true){
		if (list_begin(&(current_thread->priority_list)) == list_end(&(current_thread->priority_list))){
			current_thread->priority = current_thread->origin_priority;
			return;
		}
		struct list_elem *priority_elem = list_pop_front(&(current_thread->priority_list));
		if (priority_elem != &(priority_check->priority_elem)){
			list_push_front(&(priority_check->priority_list), priority_elem);
		}			
	}
}

void
remove_priority_list(struct thread *current_thread, struct thread *priority_check){
	struct list_elem* cur_element = list_begin(&(current_thread->priority_list));
	while(true){
		if (priority_check == list_entry(cur_element, struct thread, priority_elem)){
			list_remove(cur_element);
			current_thread->priority = list_entry(list_begin(&current_thread->priority_list), struct thread, priority_elem) -> priority;
			return;
			}
		cur_element = list_next(cur_element);		
	}		
}
/* Compare function for priority queue */
bool
priority_compare(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  ASSERT(a != NULL);
  ASSERT(b != NULL);

  const struct thread *thread_a = list_entry(a, struct thread, priority_elem);
  const struct thread *thread_b = list_entry(b, struct thread, priority_elem);

  return thread_a->priority > thread_b->priority;
}


/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
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
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_push_back (&cond->waiters, &waiter.elem);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

bool
high_cond(const struct list_elem *first, const struct list_elem *second, void *aux UNUSED){
	const struct semaphore_elem *first_semaphore_elem = list_entry(first, struct semaphore_elem, elem);
	const struct semaphore_elem *second_semaphore_elem = list_entry(second, struct semaphore_elem, elem);
	const struct semaphore first_semaphore = first_semaphore_elem->semaphore;
	const struct semaphore second_semaphore = second_semaphore_elem->semaphore;
	const struct thread *first_thread = list_entry (list_front (&first_semaphore.waiters),struct thread, elem);
	const struct thread *second_thread = list_entry (list_front (&second_semaphore.waiters),struct thread, elem);
	return first_thread->priority > second_thread->priority;
}
/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)){
		list_sort(&cond->waiters,high_cond,NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
