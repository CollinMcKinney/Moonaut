#include "jobgraph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==================== PLATFORM DETECTION & THREADING ==================== */

#if defined(_WIN32) || defined(_WIN64)
    #define JG_WINDOWS 1
    #include <windows.h>
    #include <process.h>
#else
    #define JG_POSIX 1
    #include <pthread.h>
    #include <unistd.h>
    #include <errno.h>
#endif

#ifdef JG_WINDOWS
    typedef CRITICAL_SECTION   jg_mutex_t;
    typedef CONDITION_VARIABLE jg_cond_t;

    #define JG_MUTEX_INIT(m)    InitializeCriticalSection(m)
    #define JG_MUTEX_LOCK(m)    EnterCriticalSection(m)
    #define JG_MUTEX_UNLOCK(m)  LeaveCriticalSection(m)
    #define JG_MUTEX_DESTROY(m) DeleteCriticalSection(m)

    #define JG_COND_INIT(c)     InitializeConditionVariable(c)
    #define JG_COND_WAIT(c,m)   SleepConditionVariableCS(c, m, INFINITE)
    #define JG_COND_SIGNAL(c)   WakeConditionVariable(c)
    #define JG_COND_BROADCAST(c) WakeAllConditionVariable(c)
    #define JG_COND_DESTROY(c)  ((void)0)
#else
    typedef pthread_mutex_t jg_mutex_t;
    typedef pthread_cond_t  jg_cond_t;

    #define JG_MUTEX_INIT(m)    pthread_mutex_init(m, NULL)
    #define JG_MUTEX_LOCK(m)    pthread_mutex_lock(m)
    #define JG_MUTEX_UNLOCK(m)  pthread_mutex_unlock(m)
    #define JG_MUTEX_DESTROY(m) pthread_mutex_destroy(m)

    #define JG_COND_INIT(c)     pthread_cond_init(c, NULL)
    #define JG_COND_WAIT(c,m)   pthread_cond_wait(c, m)
    #define JG_COND_SIGNAL(c)   pthread_cond_signal(c)
    #define JG_COND_BROADCAST(c) pthread_cond_broadcast(c)
    #define JG_COND_DESTROY(c)  pthread_cond_destroy(c)
#endif

/* ==================== ATOMIC OPERATIONS ==================== */

#if defined(__GNUC__) || defined(__clang__)
  #define ATOMIC_ADD(ptr,val)   __sync_fetch_and_add((ptr), (val))
  #define ATOMIC_SUB(ptr,val)   __sync_fetch_and_sub((ptr), (val))
  #define ATOMIC_LOAD(ptr)      __sync_fetch_and_add((ptr), 0)
#elif defined(_MSC_VER)
  #define ATOMIC_ADD(ptr,val)   InterlockedExchangeAdd((LONG volatile*)(ptr), (val))
  #define ATOMIC_SUB(ptr,val)   InterlockedExchangeAdd((LONG volatile*)(ptr), -(LONG)(val))
  #define ATOMIC_LOAD(ptr)      InterlockedExchangeAdd((LONG volatile*)(ptr), 0)
#else
  #error "No atomics support - implement platform-specific atomics"
#endif

/* ==================== CONSTANTS ==================== */

#define JG_JOB_POOL_SIZE        4096
#define JG_MAX_WORKERS          256
#define JG_INITIAL_DEP_CAPACITY 4

/* ==================== JOB / LINK / CHAIN DEFINITIONS ==================== */

typedef enum {
    JOB_STATE_WAITING,
    JOB_STATE_READY,
    JOB_STATE_RUNNING,
    JOB_STATE_COMPLETE,
    JOB_STATE_CANCELLED
} job_state_t;

typedef struct {
    unsigned int needed;
    unsigned int satisfied;
    unsigned int signalled;
} job_deps_t;

struct joblink_t;   /* forward */

struct job_t {
    job_func_t        func;
    void*             context;
    job_deps_t        deps;
    job_state_t       state;
    struct job_t*     next_in_chain;
    struct jobchain_t* parent_chain;
    unsigned int      job_id;

    struct job_t**    dependents;
    unsigned int      dependent_count;
    unsigned int      dependent_capacity;

    int               is_standalone;   /* will be removed in later cleanup */
    int               is_barrier;
    struct joblink_t* my_link;
    struct job_t*     next_ready;      /* intrusive ready queue link */
};

struct joblink_t {
    unsigned int      id;
    struct job_t**    jobs;
    unsigned int      count;
    struct job_t*     barrier;
};

struct jobchain_t {
    joblink_t**       links;
    unsigned int      link_count;
    jobgraph_t*       system;
    int               submitted;
    int               completed;
};

/* ==================== READY QUEUE (intrusive) ==================== */

typedef struct {
    struct job_t*   head;
    struct job_t*   tail;
    jg_mutex_t      mutex;
    jg_cond_t       not_empty;
    unsigned int    count;
} ready_queue_t;

typedef struct {
    int                id;
    jobgraph_t*        system;
#ifdef JG_WINDOWS
    HANDLE             thread;
#else
    pthread_t          thread;
#endif
    int                running;
    unsigned long      jobs_executed;
} worker_t;

struct jobgraph_t {
    worker_t*          workers;
    unsigned int       worker_count;
    volatile int       running;

    ready_queue_t      ready_queue;
    jg_mutex_t         stats_mutex;
    jg_cond_t          all_complete;

    /* Atomic counters */
    unsigned int       jobs_remaining;        /* user jobs in flight */
    unsigned long      total_jobs_completed;
    unsigned int       total_jobs_created;

    /* Memory pool */
    struct job_t*      job_pool;
    unsigned int       job_pool_size;
    unsigned int       job_pool_used;          /* atomic index */
    jg_mutex_t         pool_mutex;             /* only for fallback malloc */

    /* ID generators */
    unsigned int       next_job_id;
    jg_mutex_t         id_mutex;

    /* Chain list (added by user, owned by graph) */
    jobchain_t**       chains;
    unsigned int       chain_count;
    unsigned int       chain_capacity;
};

/* ==================== QUEUE IMPLEMENTATION ==================== */

static void ready_queue_init(ready_queue_t* q) {
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
    JG_MUTEX_INIT(&q->mutex);
    JG_COND_INIT(&q->not_empty);
}

static void ready_queue_destroy(ready_queue_t* q) {
    JG_MUTEX_DESTROY(&q->mutex);
    JG_COND_DESTROY(&q->not_empty);
}

static void ready_queue_push(ready_queue_t* q, struct job_t* job) {
    job->next_ready = NULL;
    JG_MUTEX_LOCK(&q->mutex);
    if (q->tail) {
        q->tail->next_ready = job;
    } else {
        q->head = job;
    }
    q->tail = job;
    q->count++;
    JG_COND_SIGNAL(&q->not_empty);
    JG_MUTEX_UNLOCK(&q->mutex);
}

static struct job_t* ready_queue_pop(ready_queue_t* q, jobgraph_t* js) {
    struct job_t* job = NULL;
    JG_MUTEX_LOCK(&q->mutex);
    while (q->head == NULL) {
        if (js && !ATOMIC_LOAD(&js->running)) {
            JG_MUTEX_UNLOCK(&q->mutex);
            return NULL;
        }
        JG_COND_WAIT(&q->not_empty, &q->mutex);
        if (js && !ATOMIC_LOAD(&js->running)) {
            JG_MUTEX_UNLOCK(&q->mutex);
            return NULL;
        }
    }
    job = q->head;
    q->head = job->next_ready;
    if (!q->head) q->tail = NULL;
    q->count--;
    JG_MUTEX_UNLOCK(&q->mutex);
    return job;
}

/* ==================== JOB MEMORY POOL ==================== */

static struct job_t* pool_alloc_job(jobgraph_t* js) {
    unsigned int idx;
    struct job_t* job = NULL;
    idx = ATOMIC_ADD(&js->job_pool_used, 1);
    if (idx < js->job_pool_size) {
        job = &js->job_pool[idx];
        memset(job, 0, sizeof(struct job_t));
    } else {
        /* Fallback: malloc */
        JG_MUTEX_LOCK(&js->pool_mutex);
        job = (struct job_t*)calloc(1, sizeof(struct job_t));
        JG_MUTEX_UNLOCK(&js->pool_mutex);
    }
    return job;
}

static void pool_free_job(jobgraph_t* js, struct job_t* job) {
    if (job->dependents) {
        free(job->dependents);
        job->dependents = NULL;
    }
    job->func = NULL;
    job->context = NULL;
    job->deps.needed = 0;
    job->deps.satisfied = 0;
    job->deps.signalled = 0;
    job->state = JOB_STATE_WAITING;
    job->next_in_chain = NULL;
    job->parent_chain = NULL;
    job->dependent_count = 0;
    job->dependent_capacity = 0;
    job->is_standalone = 0;
    job->is_barrier = 0;
    job->my_link = NULL;
    job->next_ready = NULL;
}

/* ==================== DEPENDENCY HELPERS ==================== */

static void add_dependent(struct job_t* job, struct job_t* dependent) {
    if (job->dependent_count + 1 >= job->dependent_capacity) {
        unsigned int new_cap = job->dependent_capacity * 2;
        if (new_cap < JG_INITIAL_DEP_CAPACITY) new_cap = JG_INITIAL_DEP_CAPACITY;
        {
            struct job_t** tmp = (struct job_t**)realloc(job->dependents,
                                          new_cap * sizeof(struct job_t*));
            if (!tmp) return;
            job->dependents = tmp;
            job->dependent_capacity = new_cap;
        }
    }
    job->dependents[job->dependent_count++] = dependent;
    dependent->deps.needed++;
}

static void signal_job_complete(struct job_t* job);

static void signal_job_complete(struct job_t* job) {
    jobgraph_t* js = NULL;
    unsigned int i;

    if (job->parent_chain) {
        if (job->is_standalone)
            js = (jobgraph_t*)job->parent_chain;
        else
            js = job->parent_chain->system;
    }
    if (!js) return;

    for (i = 0; i < job->dependent_count; i++) {
        struct job_t* dep = job->dependents[i];
        dep->deps.satisfied++;
        if (dep->deps.satisfied >= dep->deps.needed &&
            dep->state == JOB_STATE_WAITING &&
            !dep->deps.signalled) {
            dep->deps.signalled = 1;
            if (dep->is_barrier) {
                dep->state = JOB_STATE_COMPLETE;
                signal_job_complete(dep);   /* inline barrier */
            } else {
                dep->state = JOB_STATE_READY;
                ready_queue_push(&js->ready_queue, dep);
            }
        }
    }
}

static void job_try_make_ready(jobgraph_t* js, struct job_t* job) {
    if (job->deps.signalled) return;
    if (job->deps.satisfied >= job->deps.needed) {
        job->deps.signalled = 1;
        job->state = JOB_STATE_READY;
        ready_queue_push(&js->ready_queue, job);
    }
}

/* ==================== WORKER THREAD ==================== */

#ifdef JG_WINDOWS
static unsigned int __stdcall worker_thread_func(void* arg) {
#else
static void* worker_thread_func(void* arg) {
#endif
    worker_t* worker = (worker_t*)arg;
    jobgraph_t* js = worker->system;
    worker->jobs_executed = 0;

    while (ATOMIC_LOAD(&js->running)) {
        struct job_t* job = ready_queue_pop(&js->ready_queue, js);
        if (!job || !ATOMIC_LOAD(&js->running)) break;

        {
            struct job_t* next_job = NULL;
            if (!job->is_barrier) {
                next_job = job->func(job->context);
            }

            {
                JG_MUTEX_LOCK(&js->stats_mutex);
                job->state = JOB_STATE_COMPLETE;
                if (job->is_barrier && job->parent_chain) {
                    jobchain_t* chain = job->parent_chain;
                    if (job->my_link == chain->links[chain->link_count - 1]) {
                        chain->completed = 1;
                    }
                }
                JG_MUTEX_UNLOCK(&js->stats_mutex);
            }

            signal_job_complete(job);

            if (!job->is_barrier) {
                if (ATOMIC_SUB(&js->jobs_remaining, 1) == 1) {
                    JG_MUTEX_LOCK(&js->stats_mutex);
                    JG_COND_BROADCAST(&js->all_complete);
                    JG_MUTEX_UNLOCK(&js->stats_mutex);
                }
            }

            if (next_job) {
                next_job->parent_chain = (jobchain_t*)js;
                next_job->is_standalone = 1;
                ATOMIC_ADD(&js->jobs_remaining, 1);
                job_try_make_ready(js, next_job);
            }

            ATOMIC_ADD(&js->total_jobs_completed, 1);
            worker->jobs_executed++;
        }
    }
    return 0;
}

/* ==================== SYSTEM API ==================== */

jobgraph_t* jobgraph_create(int worker_threads) {
    unsigned int i;
    jobgraph_t* js;

    if (worker_threads <= 0) worker_threads = 2;
    if (worker_threads > JG_MAX_WORKERS) worker_threads = JG_MAX_WORKERS;

    js = (jobgraph_t*)calloc(1, sizeof(jobgraph_t));
    if (!js) return NULL;

    js->worker_count = worker_threads;
    js->running = 1;

    ready_queue_init(&js->ready_queue);
    JG_MUTEX_INIT(&js->stats_mutex);
    JG_COND_INIT(&js->all_complete);
    JG_MUTEX_INIT(&js->pool_mutex);
    JG_MUTEX_INIT(&js->id_mutex);

    js->job_pool_size = JG_JOB_POOL_SIZE;
    js->job_pool = (struct job_t*)calloc(js->job_pool_size, sizeof(struct job_t));
    if (!js->job_pool) {
        free(js);
        return NULL;
    }

    js->workers = (worker_t*)calloc(worker_threads, sizeof(worker_t));
    if (!js->workers) {
        free(js->job_pool);
        free(js);
        return NULL;
    }

    for (i = 0; i < worker_threads; i++) {
        js->workers[i].id = i;
        js->workers[i].system = js;
        js->workers[i].running = 1;
#ifdef JG_WINDOWS
        js->workers[i].thread = (HANDLE)_beginthreadex(NULL, 0,
                             worker_thread_func, &js->workers[i], 0, NULL);
        if (!js->workers[i].thread) {
            js->worker_count = i;
            jobgraph_destroy(js);
            return NULL;
        }
#else
        if (pthread_create(&js->workers[i].thread, NULL,
                         worker_thread_func, &js->workers[i]) != 0) {
            js->worker_count = i;
            jobgraph_destroy(js);
            return NULL;
        }
#endif
    }
    return js;
}

void jobgraph_destroy(jobgraph_t* js) {
    unsigned int i;

    if (!js) return;
    js->running = 0;

    JG_MUTEX_LOCK(&js->ready_queue.mutex);
    JG_COND_BROADCAST(&js->ready_queue.not_empty);
    JG_MUTEX_UNLOCK(&js->ready_queue.mutex);

    for (i = 0; i < js->worker_count; i++) {
        if (js->workers[i].running) {
#ifdef JG_WINDOWS
            WaitForSingleObject(js->workers[i].thread, INFINITE);
            CloseHandle(js->workers[i].thread);
#else
            pthread_join(js->workers[i].thread, NULL);
#endif
        }
    }

    ready_queue_destroy(&js->ready_queue);
    JG_MUTEX_DESTROY(&js->stats_mutex);
    JG_COND_DESTROY(&js->all_complete);
    JG_MUTEX_DESTROY(&js->pool_mutex);
    JG_MUTEX_DESTROY(&js->id_mutex);

    /* Free any chains that were never reset (should not happen in normal use) */
    for (i = 0; i < js->chain_count; i++) {
        jobchain_t* chain = js->chains[i];
        unsigned int j;
        for (j = 0; j < chain->link_count; j++) {
            free(chain->links[j]->jobs);
            free(chain->links[j]);
        }
        free(chain->links);
        free(chain);
    }
    free(js->chains);

    free(js->workers);
    free(js->job_pool);
    free(js);
}

void jobgraph_reset(jobgraph_t* js) {
    unsigned int i;
    if (!js) return;

    /* Free all chains created since last reset */
    for (i = 0; i < js->chain_count; i++) {
        jobchain_t* chain = js->chains[i];
        unsigned int j;
        for (j = 0; j < chain->link_count; j++) {
            free(chain->links[j]->jobs);
            free(chain->links[j]);
        }
        free(chain->links);
        free(chain);
    }
    free(js->chains);
    js->chains = NULL;
    js->chain_count = 0;
    js->chain_capacity = 0;

    /* Reset the job pool */
    ATOMIC_SUB(&js->job_pool_used, ATOMIC_LOAD(&js->job_pool_used));
}

int jobgraph_pending_count(jobgraph_t* js) {
    if (!js) return 0;
    return (int)ATOMIC_LOAD(&js->jobs_remaining);
}

unsigned long jobgraph_total_completed(jobgraph_t* js) {
    if (!js) return 0;
    return ATOMIC_LOAD(&js->total_jobs_completed);
}

/* ==================== JOB CREATION ==================== */

job_t* job_create(jobgraph_t* js, job_func_t func, void* context) {
    struct job_t* job;
    if (!js || !func) return NULL;
    job = pool_alloc_job(js);
    if (!job) return NULL;

    JG_MUTEX_LOCK(&js->id_mutex);
    job->job_id = js->next_job_id++;
    JG_MUTEX_UNLOCK(&js->id_mutex);

    job->func = func;
    job->context = context;
    return job;
}

unsigned int job_get_id(job_t* job) {
    return job ? job->job_id : 0;
}

void job_wait(job_t* job) {
    jobgraph_t* js;
    if (!job || !job->parent_chain) return;
    if (job->is_standalone)
        js = (jobgraph_t*)job->parent_chain;
    else
        js = job->parent_chain->system;
    if (!js) return;

    JG_MUTEX_LOCK(&js->stats_mutex);
    while (job->state != JOB_STATE_COMPLETE && job->state != JOB_STATE_CANCELLED) {
        JG_COND_WAIT(&js->all_complete, &js->stats_mutex);
    }
    JG_MUTEX_UNLOCK(&js->stats_mutex);
}

/* ==================== CHAIN & LINK MANAGEMENT ==================== */

static int ensure_links(jobchain_t* chain, int required_index) {
    jobgraph_t* js = chain->system;
    while ((int)chain->link_count <= required_index) {
        joblink_t* link = (joblink_t*)calloc(1, sizeof(joblink_t));
        if (!link) return -1;

        {
            struct job_t* barrier = pool_alloc_job(js);
            if (!barrier) {
                free(link);
                return -1;
            }
            barrier->is_barrier = 1;
            barrier->parent_chain = chain;
            barrier->my_link = link;
            barrier->state = JOB_STATE_WAITING;
            link->barrier = barrier;
            link->id = chain->link_count;
        }

        {
            joblink_t** tmp = (joblink_t**)realloc(chain->links,
                                    (chain->link_count + 1) * sizeof(joblink_t*));
            if (!tmp) {
                pool_free_job(js, link->barrier);
                free(link);
                return -1;
            }
            chain->links = tmp;
            chain->links[chain->link_count++] = link;
        }
    }
    return 0;
}

static int link_add_job(jobchain_t* chain, joblink_t* link, struct job_t* job) {
    if (!job) return -1;
    {
        jobgraph_t* js = chain->system;
        (void)js;   /* unused */
    }

    job->parent_chain = chain;
    job->is_standalone = 0;
    job->my_link = link;

    {
        struct job_t** tmp = (struct job_t**)realloc(link->jobs,
                              (link->count + 1) * sizeof(struct job_t*));
        if (!tmp) return -1;
        link->jobs = tmp;
        link->jobs[link->count++] = job;
    }

    if (link->id > 0) {
        struct job_t* prev_barrier = chain->links[link->id - 1]->barrier;
        add_dependent(prev_barrier, job);
    }
    add_dependent(job, link->barrier);

    return 0;
}

/* ==================== PUBLIC CHAIN / LINK API ==================== */

jobchain_t* jobgraph_add_chain(jobgraph_t* js) {
    jobchain_t* chain;
    if (!js) return NULL;

    chain = (jobchain_t*)calloc(1, sizeof(jobchain_t));
    if (!chain) return NULL;
    chain->system = js;

    /* Add to graph's chain list */
    if (js->chain_count >= js->chain_capacity) {
        unsigned int new_cap = js->chain_capacity ? js->chain_capacity * 2 : 4;
        jobchain_t** tmp = (jobchain_t**)realloc(js->chains, new_cap * sizeof(jobchain_t*));
        if (!tmp) { free(chain); return NULL; }
        js->chains = tmp;
        js->chain_capacity = new_cap;
    }
    js->chains[js->chain_count++] = chain;
    return chain;
}

joblink_t* jobchain_add_link(jobchain_t* chain) {
    if (!chain || chain->submitted) return NULL;
    if (ensure_links(chain, (int)chain->link_count) != 0) return NULL;
    return chain->links[chain->link_count - 1];
}

void joblink_add_job(joblink_t* link, job_t* job) {
    jobchain_t* chain;
    if (!link || !job) return;
    chain = link->barrier->parent_chain;
    if (!chain || chain->submitted) return;
    if (job->parent_chain != NULL || job->is_standalone) return;
    if (link->id >= chain->link_count || chain->links[link->id] != link) return;

    link_add_job(chain, link, job);
}

void joblink_add_chain(joblink_t* link, jobchain_t* sub_chain) {
    jobchain_t* parent_chain;
    unsigned int i;

    if (!link || !sub_chain) return;
    parent_chain = link->barrier->parent_chain;
    if (!parent_chain || parent_chain->submitted) return;
    if (link->id >= parent_chain->link_count ||
        parent_chain->links[link->id] != link) return;

    if (sub_chain->link_count == 0) return;

    if (link->id > 0) {
        struct job_t* prev_barrier = parent_chain->links[link->id - 1]->barrier;
        joblink_t* first_sub_link = sub_chain->links[0];
        for (i = 0; i < first_sub_link->count; i++) {
            add_dependent(prev_barrier, first_sub_link->jobs[i]);
        }
    }

    {
        struct job_t* sub_last_barrier =
            sub_chain->links[sub_chain->link_count - 1]->barrier;
        add_dependent(sub_last_barrier, link->barrier);
    }
}

unsigned int joblink_get_id(joblink_t* link) {
    return link ? link->id : 0;
}

/* ==================== CROSS‑CHAIN ORDERING ==================== */

void jobchain_then(jobchain_t* first, jobchain_t* second) {
    struct job_t* last_barrier;
    unsigned int i;
    joblink_t* first_link_second;

    if (!first || !second) return;
    if (first->link_count == 0 || second->link_count == 0) return;

    last_barrier = first->links[first->link_count - 1]->barrier;
    first_link_second = second->links[0];
    for (i = 0; i < first_link_second->count; i++) {
        add_dependent(last_barrier, first_link_second->jobs[i]);
    }
}

/* ==================== EXECUTION ==================== */

void jobgraph_submit(jobgraph_t* js) {
    unsigned int i, j;
    unsigned int total_user_jobs = 0;

    if (!js) return;

    /* Count user jobs in all chains (including sub‑chains, which are also in the list) */
    for (i = 0; i < js->chain_count; i++) {
        jobchain_t* chain = js->chains[i];
        if (chain->submitted) continue;
        chain->submitted = 1;
        if (chain->link_count == 0) {
            chain->completed = 1;
            continue;
        }
        for (j = 0; j < chain->link_count; j++) {
            total_user_jobs += chain->links[j]->count;
        }
    }

    ATOMIC_ADD(&js->jobs_remaining, total_user_jobs);
    ATOMIC_ADD(&js->total_jobs_created, total_user_jobs);

    /* Make first‑link jobs ready (only those with all dependencies satisfied will actually run) */
    for (i = 0; i < js->chain_count; i++) {
        jobchain_t* chain = js->chains[i];
        joblink_t* first_link;
        if (chain->completed || chain->link_count == 0) continue;
        first_link = chain->links[0];
        for (j = 0; j < first_link->count; j++) {
            job_try_make_ready(js, first_link->jobs[j]);
        }
    }
}

void jobgraph_wait(jobgraph_t* js) {
    if (!js) return;
    JG_MUTEX_LOCK(&js->stats_mutex);
    while (ATOMIC_LOAD(&js->jobs_remaining) > 0) {
        JG_COND_WAIT(&js->all_complete, &js->stats_mutex);
    }
    JG_MUTEX_UNLOCK(&js->stats_mutex);
}