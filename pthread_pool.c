#include "pthread_pool.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

/*
*	single task node struct in thread pool queue
*/
struct pool_queue {
	void *arg;	//the argument to pass to the thread worker function.
	char free;	//whether you wanna free the argument after the task has completed.
	struct pool_queue *next;	//point to next task in the queue
};

/*
*	the thread pool struct
*/
struct pool {
	char cancelled;					// if the pool is cancelled
	void *(*fn)(void *);		// worker function
	unsigned int remaining;	// number of remaining threads in the thread pool
	unsigned int nthreads;	// initial number of threads
	struct pool_queue *q;		// the header node of the thread pool queue
	struct pool_queue *end;	// the end node of the thread pool queue
	pthread_mutex_t q_mtx;	// thread lock
	pthread_cond_t q_cnd;		// condition var
	pthread_t threads[1];		//	thread id
};

static void * thread(void *arg);

void * pool_start(void * (*thread_func)(void *), unsigned int threads) {
	struct pool *p = (struct pool *) malloc(sizeof(struct pool) + (threads-1) * sizeof(pthread_t));
	int i;

	pthread_mutex_init(&p->q_mtx, NULL);
	pthread_cond_init(&p->q_cnd, NULL);
	p->nthreads = threads;
	p->fn = thread_func;
	p->cancelled = 0;
	p->remaining = 0;
	p->end = NULL;
	p->q = NULL;

	for (i = 0; i < threads; i++) {
		pthread_create(&p->threads[i], NULL, &thread, p);
	}

	return p;
}

void pool_enqueue(void *pool, void *arg, char free) {
	struct pool *p = (struct pool *) pool;
	struct pool_queue *q = (struct pool_queue *) malloc(sizeof(struct pool_queue));
	q->arg = arg;
	q->next = NULL;
	q->free = free;

	pthread_mutex_lock(&p->q_mtx);
	if (p->end != NULL) p->end->next = q;
	if (p->q == NULL) p->q = q;
	p->end = q;
	p->remaining++;
	pthread_cond_signal(&p->q_cnd);
	pthread_mutex_unlock(&p->q_mtx);
}

void pool_wait(void *pool) {
	struct pool *p = (struct pool *) pool;

	pthread_mutex_lock(&p->q_mtx);
	while (!p->cancelled && p->remaining) {
		pthread_cond_wait(&p->q_cnd, &p->q_mtx);
	}
	pthread_mutex_unlock(&p->q_mtx);
}

void pool_end(void *pool) {
	struct pool *p = (struct pool *) pool;
	struct pool_queue *q;
	int i;

	p->cancelled = 1;

	pthread_mutex_lock(&p->q_mtx);
	pthread_cond_broadcast(&p->q_cnd);
	pthread_mutex_unlock(&p->q_mtx);

	for (i = 0; i < p->nthreads; i++) {
		pthread_join(p->threads[i], NULL);
	}

	while (p->q != NULL) {
		q = p->q;
		p->q = q->next;

		if (q->free) free(q->arg);
		free(q);
	}

	free(p);
}

static void * thread(void *arg) {
	struct pool_queue *q;
	struct pool *p = (struct pool *) arg;

	while (!p->cancelled) {
		pthread_mutex_lock(&p->q_mtx);
		while (!p->cancelled && p->q == NULL) {
			pthread_cond_wait(&p->q_cnd, &p->q_mtx);
		}
		if (p->cancelled) {
			pthread_mutex_unlock(&p->q_mtx);
			return NULL;
		}
		q = p->q;
		p->q = q->next;
		p->end = (q == p->end ? NULL : p->end);
		pthread_mutex_unlock(&p->q_mtx);

		p->fn(q->arg);

		if (q->free) free(q->arg);
		free(q);
		q = NULL;

		pthread_mutex_lock(&p->q_mtx);
		p->remaining--;
		pthread_cond_broadcast(&p->q_cnd);
		pthread_mutex_unlock(&p->q_mtx);
	}

	return NULL;
}
