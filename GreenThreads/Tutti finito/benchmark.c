#define _GNU_SOURCE
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>
#include <sched.h>
#include "green.h"

green_cond_t cond;
green_mutex_t mutex;
pthread_cond_t emptyP, fullP;
pthread_mutex_t mutexP;
int THREADS_AMOUNT = 2;
unsigned long long processTimeGreen = 0, processTimeP = 0;

int counter = 0;

unsigned long long cpumSecond() {

	struct timeval tp;
	gettimeofday(&tp, NULL);
	return ((((double)tp.tv_sec * 1000000 + (double)tp.tv_usec) / 1000));
}

int buffer;
int productions;
green_cond_t full, empty;

void produce() {

	for (int i = 0; i < productions / (THREADS_AMOUNT / 2); i++)
	{
		green_mutex_lock(&mutex);
		
		while (buffer == 1) // wait for consumer before producing more
			green_cond_wait(&empty, &mutex);

		buffer = 1;

		green_cond_signal(&full);
		green_mutex_unlock(&mutex);
	}
}

void consume()
{
	for (int i = 0; i < productions / (THREADS_AMOUNT / 2); i++)
	{
		green_mutex_lock(&mutex);
		while (buffer == 0) // wait for producer before consuming
			green_cond_wait(&full, &mutex);

		buffer = 0;

		green_cond_signal(&empty);
		green_mutex_unlock(&mutex);
	}
}

void produceP()
{
	for (int i = 0; i < productions / (THREADS_AMOUNT / 2); i++)
	{
		pthread_mutex_lock(&mutexP);
		while (buffer == 1) // wait for consumer before producing more
			pthread_cond_wait(&emptyP, &mutexP);
		buffer = 1;

		pthread_cond_signal(&fullP);
		pthread_mutex_unlock(&mutexP);
	}
}

void consumeP()
{
	for (int i = 0; i < productions / (THREADS_AMOUNT / 2); i++)
	{
		pthread_mutex_lock(&mutexP);
		while (buffer == 0) // wait for producer before consuming
			pthread_cond_wait(&fullP, &mutexP);
		buffer = 0;

		pthread_cond_signal(&emptyP);
		pthread_mutex_unlock(&mutexP);
	}
}

void *testConsumerProducer(void *arg)
{
	int id = *(int *)arg;
	if (id % 2 == 0)
	{ // producer

		produce();
	}
	else
	{ // consumer

		consume();
	}
}

void *testConsumerProducerP(void *arg)
{
	int id = *(int *)arg;
	if (id % 2 == 0)
	{ // producer

		produceP();
	}
	else
	{ // consumer

		consumeP();
	}
}

void testGreen(int *args)
{
	green_t threads[THREADS_AMOUNT];

	unsigned long long start = cpumSecond();
	for (int i = 0; i < THREADS_AMOUNT; i++) {

		green_create(&threads[i], testConsumerProducer, &args[i]);
	}

	for (int i = 0; i < THREADS_AMOUNT; i++) {

		green_join(&threads[i], NULL);
	}
	processTimeGreen = cpumSecond() - start;
}

void testPthread(int *args) {

	pthread_t threads[THREADS_AMOUNT];
	cpu_set_t cpuset;

	CPU_ZERO(&cpuset);
	for (int i = 0; i < 1; ++i) // Change this to change how many physical threads that pthreads can use.
	{
		CPU_SET(i, &cpuset);
	}

	for (int i = 0; i < THREADS_AMOUNT; ++i)
	{
		threads[i] = pthread_self();
		pthread_setaffinity_np(threads[i], sizeof(cpuset), &cpuset);
	}

	unsigned long long start = cpumSecond();
	for (int i = 0; i < THREADS_AMOUNT; i++)
	{
		pthread_create(&threads[i], NULL, testConsumerProducerP, &args[i]);
	}
	for (int i = 0; i < THREADS_AMOUNT; i++)
	{
		pthread_join(threads[i], NULL);
	}
	processTimeP = cpumSecond() - start;
}

int main()
{
	green_cond_init(&cond);
	green_cond_init(&full);
	green_cond_init(&empty);
	green_mutex_init(&mutex);

	pthread_cond_init(&fullP, NULL);
	pthread_cond_init(&emptyP, NULL);
	pthread_mutex_init(&mutexP, NULL);
	printf("#Benchmark, creating and producing/consuming with threads!\n#\n#\n");
	printf("#{#productions\ttimeGreen(ms)\ttimePthreads(ms)\n");

	int numRuns = 50;
	for (int run = 1; run <= numRuns; run++) {

		buffer = 0;
		productions = 1000 * run; // Must be multiple of 2

		int args[THREADS_AMOUNT];
		for (int i = 0; i < THREADS_AMOUNT; i++)
			args[i] = i;

		testGreen(args);

		testPthread(args);
		printf("\t%d;\t\t%llu;\t\t%llu\n", productions, processTimeGreen, processTimeP);
	}

	return 0;
}