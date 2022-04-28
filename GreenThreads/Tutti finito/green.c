#include <stdlib.h>
#include <assert.h>
#include <signal.h>
#include <sys/time.h>
#include "green.h"

#define FALSE 0
#define TRUE 1

#define STACK_SIZE 4096
#define PERIOD 100

static sigset_t block;

void timer_handler(int);

static ucontext_t main_cntx = {0};
static green_t main_green = {&main_cntx, NULL, NULL, NULL, NULL, FALSE};

static green_t *running = &main_green;
struct green_t *ready_queue = NULL;

static void init() __attribute__((constructor));

void init()
{
	getcontext(&main_cntx);

	// Initialize the timer
	sigemptyset(&block);
	sigaddset(&block, SIGVTALRM);
	struct sigaction act = {0};
	struct timeval interval;
	struct itimerval period;

	act.sa_handler = timer_handler;
	assert(sigaction(SIGVTALRM, &act, NULL) == FALSE);
	interval.tv_sec = 0;
	interval.tv_usec = PERIOD;
	period.it_interval = interval;
	period.it_value = interval;
	setitimer(ITIMER_VIRTUAL, &period, NULL);
}

void enqueue(green_t **list, green_t *thread) {

	 // If the list is null, then no elements need to be added and we proceed by asigning the thread to the empty list
	if (*list == NULL) {

		*list = thread;
	}
	else {
		// assign a temporary thread 
		green_t *susp = *list;

		// Iterate thorugh the list until the last element
		while (susp->next != NULL) {

			susp = susp->next;
		}
		// add thread to the end of the list
		susp->next = thread;
	}
}

green_t *dequeue(green_t **list) {

	// If list is empty then return nothing
	if (*list == NULL) {

		return NULL;
	} else {

		// Assign the first element in the list to thread
		green_t *thread = *list;

		// Remove the first element in the list
		*list = (*list)->next;

		// Null the next value of the thread, inherited from the ready queue
		thread->next = NULL;

		// return said thread
		return thread;
	}
}

// Executes a specific function and returns the result of said functions. Ends with terminating a thread
void green_thread() {

	// Assign the currently running thread to a temporary reference
	green_t *this = running;

	// Store the result of the functino in result
	void *result = (*this->fun)(this->arg);

	// place waiting (joining) thread in ready queue
	enqueue(&ready_queue, this->join);

	// save result of execution
	this->retval = result;

	// we're a zombie
	this->zombie = TRUE;

	// find the next thread to run
	green_t *next = dequeue(&ready_queue);

	running = next;

	setcontext(next->context);
}

// Provides an uninitialized thread, the function that the thread should execute and the function arguments
int green_create(green_t *new, void *(*fun)(void *), void *arg) {

	// Create new context
	ucontext_t *cntx = (ucontext_t *)malloc(sizeof(ucontext_t));
	getcontext(cntx);

	void *stack = malloc(STACK_SIZE);

	// Assign proper values to context
	cntx->uc_stack.ss_sp = stack;
	cntx->uc_stack.ss_size = STACK_SIZE;
	makecontext(cntx, green_thread, 0);

	// Assign new context and function with arguments to the new thread
	new->context = cntx;
	new->fun = fun;
	new->arg = arg;
	new->next = NULL;
	new->join = NULL;
	new->retval = NULL;
	new->zombie = FALSE;

	// add new to the ready queue
	enqueue(&ready_queue, new);

	return 0;
}

// Suspend the current running thread and put it last in the queueu and assign the next thread from the ready list to running thread
int green_yield() {

	// Store the running thread in a temporary thread reference
	green_t *susp = running;

	// add suspended to ready queue
	enqueue(&ready_queue, susp);

	// select the next thread for execution
	green_t *next = dequeue(&ready_queue);

	// Assign the nextin line thread to the current running one
	running = next;

	// swap context
	swapcontext(susp->context, next->context);
	return 0;
}

int green_join(green_t *thread, void **res)
{
	if (!thread->zombie)
	{
		green_t *susp = running;

		// add as joining thread
		thread->join = susp;

		// select the next thread for execution
		green_t *next = dequeue(&ready_queue);

		running = next;
		swapcontext(susp->context, next->context);
	}

	// collect result
	if (thread->retval != NULL && res != NULL)
	{
		*res = thread->retval;
	}

	// free context
	free(thread->context);
	return 0;
}


// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< Conditions >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>



// Initialize a green condition variable
void green_cond_init(green_cond_t *cond)
{
	// block timer interrupt
	sigprocmask(SIG_BLOCK, &block, NULL);

	cond->queue = NULL;

	// Unblock timer interrupt
	sigprocmask(SIG_UNBLOCK, &block, NULL);
}

// Suspend the current thread on the condition
void green_cond_wait(green_cond_t *cond, green_mutex_t *mutex)
{
	// block timer interrupt
	sigprocmask(SIG_BLOCK, &block, NULL);

	// suspend the running thread on condition
	green_t *susp = running;
	assert(susp != NULL);

	enqueue(&cond->queue, susp);

	if (mutex != NULL) {

		// release the lock if we have a mutex
		mutex->taken = FALSE;
		green_t *susp = dequeue(&mutex->suspthreads);

		// move suspended thread to the ready queue
		enqueue(&ready_queue, susp);
		mutex->suspthreads = NULL;
	}

	// schedule the next thread
	green_t *next = dequeue(&ready_queue);
	assert(next != NULL);

	running = next;
	swapcontext(susp->context, next->context);

	if (mutex != NULL) {

		// try to take the lock
		if (mutex->taken) {

			// Bad luck, suspend
			green_t *susp = running;
			enqueue(&mutex->suspthreads, susp);

			green_t *next = dequeue(&ready_queue);
			running = next;
			swapcontext(susp->context, next->context);
		}
		else {

			// Take the lock
			mutex->taken = TRUE;
		}
	}
	// Unblock
	sigprocmask(SIG_UNBLOCK, &block, NULL);

	return;
}

// move the first suspended variable to the ready queue
void green_cond_signal(green_cond_t *cond) {

	sigprocmask(SIG_BLOCK, &block, NULL);

	// Do not do anything if the queue is empty
	if (cond->queue == NULL)
	{
		return;
	}
	// returning a previously suspended thread and then queueing it up to be used again.
	green_t *thread = dequeue(&cond->queue);
	enqueue(&ready_queue, thread);

	sigprocmask(SIG_UNBLOCK, &block, NULL);
}



// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< Adding timer interrupt >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>



void timer_handler(int sig) {

	green_t *susp = running;

	// add the running to the ready queue
	enqueue(&ready_queue, susp);

	// find the next thread for execution
	green_t *next = dequeue(&ready_queue);

	running = next;
	swapcontext(susp->context, next->context);
}

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< Adding mutex lock >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

int green_mutex_init(green_mutex_t *mutex) {

	sigprocmask(SIG_BLOCK, &block, NULL);

	// initialize fields
	mutex->taken = FALSE;
	mutex->suspthreads = NULL;

	sigprocmask(SIG_UNBLOCK, &block, NULL);
}

int green_mutex_lock(green_mutex_t *mutex) {

	// block timer interupt
	sigprocmask(SIG_BLOCK, &block, NULL);

	green_t *susp = running;

	if (mutex->taken) {
		
		// suspend the current thread
		enqueue(&mutex->suspthreads, susp);

		// find the next thread
		green_t *next = dequeue(&ready_queue);
		assert(next != NULL);

		running = next;
		swapcontext(susp->context, next->context);
	}
	else {

		// take the lock
		mutex->taken = TRUE;
	}
	// unblock
	sigprocmask(SIG_UNBLOCK, &block, NULL);

	return 0;
}

int green_mutex_unlock(green_mutex_t *mutex) {

	// block timer interrupt
	sigprocmask(SIG_BLOCK, &block, NULL);

	if (mutex->suspthreads != NULL)
	{
		// move suspended thread to ready queue
		green_t *suspthreads = dequeue(&mutex->suspthreads);
		enqueue(&ready_queue, suspthreads);
	}
	else
	{
		// release lock aka hard reset
		mutex->taken = FALSE;
		mutex->suspthreads = NULL;
	}
	// unblock
	sigprocmask(SIG_UNBLOCK, &block, NULL);
	return 0;
}