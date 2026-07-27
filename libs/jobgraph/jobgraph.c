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
    #include <sched.h>
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

    int               is_standalone;
    int               is_barrier;
    struct joblink_t* my_link;
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
    jobgraph_system_t* system;
    int               submitted;
    int               completed;
};

/* ==================== READY QUEUE ==================== */

typedef struct ready_node_t {
    struct job_t*          job;
    struct ready_node_t*   next;
} ready_node_t;

typedef struct {
    ready_node_t* head;
    ready_node_t* tail;
    jg_mutex_t    mutex;
    jg_cond_t     not_empty;
    unsigned int  count;
} ready_queue_t;

typedef struct {
    int                id;
    jobgraph_system_t* system;
#ifdef JG_WINDOWS
    HANDLE             thread;
#else
    pthread_t          thread;
#endif
    int                running;
    unsigned long      jobs_executed;
} worker_t;

struct jobgraph_system_t {
    worker_t*          workers;
    unsigned int       worker_count;
    volatile int       running;

    ready_queue_t      ready_queue;
    jg_mutex_t         stats_mutex;
    jg_cond_t          all_complete;

    volatile unsigned int    total_jobs_created;
    volatile unsigned long   total_jobs_completed;
    volatile unsigned int    pending_jobs;
    volatile unsigned int    running_jobs;

    struct job_t*      job_pool;
    unsigned int       job_pool_size;
    unsigned int       job_pool_used;
    jg_mutex_t         pool_mutex;

    unsigned int       next_job_id;
    unsigned int       next_chain_id;
    jg_mutex_t         id_mutex;
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
    ready_node_t *node = q->head, *next;
    while (node) {
        next = node->next;
        free(node);
        node = next;
    }
    JG_MUTEX_DESTROY(&q->mutex);
    JG_COND_DESTROY(&q->not_empty);
}

static void ready_queue_push(ready_queue_t* q, struct job_t* job) {
    ready_node_t* node = (ready_node_t*)malloc(sizeof(ready_node_t));
    if (!node) return;
    node->job = job;
    node->next = NULL;

    JG_MUTEX_LOCK(&q->mutex);
    if (q->tail) {
        q->tail->next = node;
        q->tail = node;
    } else {
        q->head = q->tail = node;
    }
    q->count++;
    JG_COND_SIGNAL(&q->not_empty);
    JG_MUTEX_UNLOCK(&q->mutex);
}

static struct job_t* ready_queue_pop(ready_queue_t* q, jobgraph_system_t* js) {
    JG_MUTEX_LOCK(&q->mutex);
    while (q->head == NULL) {
        if (js && !js->running) {
            JG_MUTEX_UNLOCK(&q->mutex);
            return NULL;
        }
        JG_COND_WAIT(&q->not_empty, &q->mutex);
        if (js && !js->running) {
            JG_MUTEX_UNLOCK(&q->mutex);
            return NULL;
        }
    }
    ready_node_t* node = q->head;
    q->head = node->next;
    if (!q->head) q->tail = NULL;
    q->count--;
    struct job_t* job = node->job;
    free(node);
    JG_MUTEX_UNLOCK(&q->mutex);
    return job;
}

/* ==================== JOB MEMORY POOL ==================== */

static struct job_t* pool_alloc_job(jobgraph_system_t* js) {
    struct job_t* job = NULL;
    JG_MUTEX_LOCK(&js->pool_mutex);
    if (js->job_pool_used < js->job_pool_size) {
        job = &js->job_pool[js->job_pool_used++];
        memset(job, 0, sizeof(struct job_t));
    }
    JG_MUTEX_UNLOCK(&js->pool_mutex);
    if (!job) job = (struct job_t*)calloc(1, sizeof(struct job_t));
    return job;
}

static void pool_free_job(jobgraph_system_t* js, struct job_t* job) {
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
}

/* ==================== DEPENDENCY HELPERS ==================== */

static void add_dependent(struct job_t* job, struct job_t* dependent) {
    if (job->dependent_count + 1 >= job->dependent_capacity) {
        unsigned int new_cap = job->dependent_capacity * 2;
        if (new_cap < JG_INITIAL_DEP_CAPACITY) new_cap = JG_INITIAL_DEP_CAPACITY;
        struct job_t** tmp = (struct job_t**)realloc(job->dependents, new_cap * sizeof(struct job_t*));
        if (!tmp) return;
        job->dependents = tmp;
        job->dependent_capacity = new_cap;
    }
    job->dependents[job->dependent_count++] = dependent;
    dependent->deps.needed++;
}

static void signal_job_complete(struct job_t* job) {
    jobgraph_system_t* js = NULL;
    if (job->parent_chain) {
        if (job->is_standalone)
            js = (jobgraph_system_t*)job->parent_chain;
        else
            js = job->parent_chain->system;
    }
    if (!js) return;

    for (unsigned int i = 0; i < job->dependent_count; i++) {
        struct job_t* dep = job->dependents[i];
        dep->deps.satisfied++;
        if (dep->deps.satisfied >= dep->deps.needed &&
            dep->state == JOB_STATE_WAITING &&
            !dep->deps.signalled) {
            dep->deps.signalled = 1;
            dep->state = JOB_STATE_READY;
            ready_queue_push(&js->ready_queue, dep);
        }
    }
}

static void job_try_make_ready(jobgraph_system_t* js, struct job_t* job) {
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
    jobgraph_system_t* js = worker->system;
    worker->jobs_executed = 0;

    while (js->running) {
        struct job_t* job = ready_queue_pop(&js->ready_queue, js);
        if (!job || !js->running) break;

        JG_MUTEX_LOCK(&js->stats_mutex);
        job->state = JOB_STATE_RUNNING;
        js->running_jobs++;
        if (js->pending_jobs > 0) js->pending_jobs--;
        JG_MUTEX_UNLOCK(&js->stats_mutex);

        struct job_t* next_job = NULL;
        if (!job->is_barrier) {
            next_job = job->func(job->context);
        }

        JG_MUTEX_LOCK(&js->stats_mutex);
        job->state = JOB_STATE_COMPLETE;
        js->running_jobs--;
        js->total_jobs_completed++;
        worker->jobs_executed++;

        signal_job_complete(job);

        if (job->is_barrier && job->parent_chain) {
            jobchain_t* chain = job->parent_chain;
            if (job->my_link == chain->links[chain->link_count - 1]) {
                chain->completed = 1;
            }
        }

        if (next_job) {
            next_job->parent_chain = (jobchain_t*)js;
            next_job->is_standalone = 1;
            job_try_make_ready(js, next_job);
        }

        if (js->pending_jobs == 0 && js->running_jobs == 0) {
            JG_COND_SIGNAL(&js->all_complete);
        }
        JG_MUTEX_UNLOCK(&js->stats_mutex);

#ifdef JG_WINDOWS
        SwitchToThread();
#else
        sched_yield();
#endif
    }
    return 0;
}

/* ==================== SYSTEM API ==================== */

jobgraph_system_t* jobgraph_system_create(int worker_threads) {
    if (worker_threads <= 0) worker_threads = 2;
    if (worker_threads > JG_MAX_WORKERS) worker_threads = JG_MAX_WORKERS;

    jobgraph_system_t* js = (jobgraph_system_t*)calloc(1, sizeof(jobgraph_system_t));
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

    for (unsigned int i = 0; i < worker_threads; i++) {
        js->workers[i].id = i;
        js->workers[i].system = js;
        js->workers[i].running = 1;
#ifdef JG_WINDOWS
        js->workers[i].thread = (HANDLE)_beginthreadex(NULL, 0, worker_thread_func, &js->workers[i], 0, NULL);
        if (!js->workers[i].thread) {
            js->worker_count = i;
            jobgraph_system_destroy(js);
            return NULL;
        }
#else
        if (pthread_create(&js->workers[i].thread, NULL, worker_thread_func, &js->workers[i]) != 0) {
            js->worker_count = i;
            jobgraph_system_destroy(js);
            return NULL;
        }
#endif
    }
    return js;
}

void jobgraph_system_destroy(jobgraph_system_t* js) {
    if (!js) return;
    js->running = 0;

    JG_MUTEX_LOCK(&js->ready_queue.mutex);
    JG_COND_BROADCAST(&js->ready_queue.not_empty);
    JG_MUTEX_UNLOCK(&js->ready_queue.mutex);

    for (unsigned int i = 0; i < js->worker_count; i++) {
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

    free(js->workers);
    free(js->job_pool);
    free(js);
}

void jobgraph_wait_all(jobgraph_system_t* js) {
    if (!js) return;
    JG_MUTEX_LOCK(&js->stats_mutex);
    while (js->pending_jobs > 0 || js->running_jobs > 0) {
        JG_COND_WAIT(&js->all_complete, &js->stats_mutex);
    }
    JG_MUTEX_UNLOCK(&js->stats_mutex);
}

int jobgraph_pending_count(jobgraph_system_t* js) {
    if (!js) return 0;
    int c;
    JG_MUTEX_LOCK(&js->stats_mutex);
    c = js->pending_jobs;
    JG_MUTEX_UNLOCK(&js->stats_mutex);
    return c;
}

int jobgraph_running_count(jobgraph_system_t* js) {
    if (!js) return 0;
    int c;
    JG_MUTEX_LOCK(&js->stats_mutex);
    c = js->running_jobs;
    JG_MUTEX_UNLOCK(&js->stats_mutex);
    return c;
}

unsigned long jobgraph_total_completed(jobgraph_system_t* js) {
    if (!js) return 0;
    unsigned long c;
    JG_MUTEX_LOCK(&js->stats_mutex);
    c = js->total_jobs_completed;
    JG_MUTEX_UNLOCK(&js->stats_mutex);
    return c;
}

/* ==================== JOB CREATION ==================== */

job_t* job_create(jobgraph_system_t* js, job_func_t func, void* context) {
    if (!js || !func) return NULL;
    struct job_t* job = pool_alloc_job(js);
    if (!job) return NULL;

    JG_MUTEX_LOCK(&js->id_mutex);
    job->job_id = js->next_job_id++;
    JG_MUTEX_UNLOCK(&js->id_mutex);

    job->func = func;
    job->context = context;
    return job;
}

job_t* job_create_standalone(jobgraph_system_t* js, job_func_t func, void* context) {
    struct job_t* job = job_create(js, func, context);
    if (job) {
        job->is_standalone = 1;
        job->parent_chain = (jobchain_t*)js;   /* store system pointer */
    }
    return job;
}

/* ==================== CHAIN LINK MANAGEMENT ==================== */

static int ensure_links(jobchain_t* chain, int required_index) {
    jobgraph_system_t* js = chain->system;
    while ((int)chain->link_count <= required_index) {
        joblink_t* link = (joblink_t*)calloc(1, sizeof(joblink_t));
        if (!link) return -1;

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

        joblink_t** tmp = (joblink_t**)realloc(chain->links, (chain->link_count + 1) * sizeof(joblink_t*));
        if (!tmp) {
            pool_free_job(js, barrier);
            free(link);
            return -1;
        }
        chain->links = tmp;
        chain->links[chain->link_count++] = link;
    }
    return 0;
}

/* Helper: add an already created job to a specific link (must be unassigned) */
static int link_add_job(jobchain_t* chain, joblink_t* link, struct job_t* job) {
    if (!job) return -1;
    jobgraph_system_t* js = chain->system;

    job->parent_chain = chain;
    job->is_standalone = 0;
    job->my_link = link;

    struct job_t** tmp = (struct job_t**)realloc(link->jobs, (link->count + 1) * sizeof(struct job_t*));
    if (!tmp) return -1;
    link->jobs = tmp;
    link->jobs[link->count++] = job;

    if (link->id > 0) {
        struct job_t* prev_barrier = chain->links[link->id - 1]->barrier;
        add_dependent(prev_barrier, job);
    }
    add_dependent(job, link->barrier);

    return 0;
}

/* ==================== CHAIN PUBLIC API ==================== */

jobchain_t* jobchain_create(jobgraph_system_t* js) {
    if (!js) return NULL;
    jobchain_t* chain = (jobchain_t*)calloc(1, sizeof(jobchain_t));
    if (!chain) return NULL;
    chain->system = js;
    return chain;
}

joblink_t* jobchain_add_link(jobchain_t* chain, job_t* job) {
    if (!chain || !job) return NULL;
    if (chain->submitted) return NULL;
    if (job->parent_chain != NULL || job->is_standalone) return NULL;

    int idx = (int)chain->link_count;
    if (ensure_links(chain, idx) != 0) return NULL;
    if (link_add_job(chain, chain->links[idx], job) != 0) return NULL;
    return chain->links[idx];
}

job_t* joblink_add_job(joblink_t* link, job_t* job) {
    if (!link || !job) return NULL;
    /* We need the parent chain to validate and wire dependencies.
       The link's barrier already holds a pointer to the chain, so we can get it. */
    jobchain_t* chain = link->barrier->parent_chain;
    if (!chain || chain->submitted) return NULL;
    if (job->parent_chain != NULL || job->is_standalone) return NULL;
    if (link->id >= chain->link_count || chain->links[link->id] != link) return NULL;

    if (link_add_job(chain, link, job) != 0) return NULL;
    return job;
}

job_t* jobchain_add_job_at(jobchain_t* chain, int link_index, job_t* job) {
    if (!chain || !job) return NULL;
    if (chain->submitted) return NULL;
    if (job->parent_chain != NULL || job->is_standalone) return NULL;
    if (ensure_links(chain, link_index) != 0) return NULL;

    if (link_add_job(chain, chain->links[link_index], job) != 0) return NULL;
    return job;
}

void jobchain_add_chain_to_link(jobchain_t* parent_chain, joblink_t* link,
                                jobchain_t* sub_chain) {
    if (!parent_chain || !link || !sub_chain) return;
    if (parent_chain->submitted) return;
    if (link->id >= parent_chain->link_count ||
        parent_chain->links[link->id] != link) return;

    if (sub_chain->link_count == 0) return;

    if (link->id > 0) {
        struct job_t* prev_barrier = parent_chain->links[link->id - 1]->barrier;
        joblink_t* first_sub_link = sub_chain->links[0];
        for (unsigned int i = 0; i < first_sub_link->count; i++) {
            add_dependent(prev_barrier, first_sub_link->jobs[i]);
        }
    }

    struct job_t* sub_last_barrier = sub_chain->links[sub_chain->link_count - 1]->barrier;
    add_dependent(sub_last_barrier, link->barrier);
}

unsigned int joblink_get_id(joblink_t* link) {
    return link ? link->id : 0;
}

void jobchain_submit(jobchain_t* chain) {
    if (!chain || chain->submitted) return;
    jobgraph_system_t* js = chain->system;

    JG_MUTEX_LOCK(&js->stats_mutex);
    chain->submitted = 1;

    if (chain->link_count == 0) {
        chain->completed = 1;
        JG_MUTEX_UNLOCK(&js->stats_mutex);
        return;
    }

    joblink_t* first_link = chain->links[0];
    for (unsigned int i = 0; i < first_link->count; i++) {
        struct job_t* job = first_link->jobs[i];
        js->total_jobs_created++;
        js->pending_jobs++;
        job_try_make_ready(js, job);
    }
    js->total_jobs_created++;
    js->pending_jobs++;
    JG_MUTEX_UNLOCK(&js->stats_mutex);
}

void jobchain_wait(jobchain_t* chain) {
    if (!chain) return;
    jobgraph_system_t* js = chain->system;
    JG_MUTEX_LOCK(&js->stats_mutex);
    while (!chain->completed) {
        JG_COND_WAIT(&js->all_complete, &js->stats_mutex);
    }
    JG_MUTEX_UNLOCK(&js->stats_mutex);
}

int jobchain_is_complete(jobchain_t* chain) {
    return chain ? chain->completed : 1;
}

/* ==================== CROSS DEPENDENCIES ==================== */

void job_add_dependency(job_t* job_a, job_t* job_b) {
    if (!job_a || !job_b) return;
    add_dependent(job_a, job_b);
}

void jobchain_add_dependency(job_t* job_a, jobchain_t* chain_b) {
    if (!job_a || !chain_b) return;
    if (chain_b->link_count == 0) return;
    joblink_t* first_link = chain_b->links[0];
    for (unsigned int i = 0; i < first_link->count; i++) {
        add_dependent(job_a, first_link->jobs[i]);
    }
}

void jobchain_add_dependency_chain(jobchain_t* chain_a, jobchain_t* chain_b) {
    if (!chain_a || !chain_b) return;
    if (chain_a->link_count == 0) return;
    struct job_t* last_barrier = chain_a->links[chain_a->link_count - 1]->barrier;
    jobchain_add_dependency(last_barrier, chain_b);
}

void job_wait(job_t* job) {
    if (!job || !job->parent_chain) return;
    jobgraph_system_t* js;
    if (job->is_standalone)
        js = (jobgraph_system_t*)job->parent_chain;
    else
        js = job->parent_chain->system;
    if (!js) return;

    JG_MUTEX_LOCK(&js->stats_mutex);
    while (job->state != JOB_STATE_COMPLETE && job->state != JOB_STATE_CANCELLED) {
        JG_COND_WAIT(&js->all_complete, &js->stats_mutex);
    }
    JG_MUTEX_UNLOCK(&js->stats_mutex);
}

unsigned int job_get_id(job_t* job) {
    return job ? job->job_id : 0;
}

/* ==================== STANDALONE JOB SUBMISSION ==================== */

void job_submit(job_t* job) {
    if (!job) return;
    jobgraph_system_t* js;
    if (job->is_standalone)
        js = (jobgraph_system_t*)job->parent_chain;
    else
        js = job->parent_chain->system;
    if (!js) return;

    JG_MUTEX_LOCK(&js->stats_mutex);
    js->total_jobs_created++;
    js->pending_jobs++;
    job_try_make_ready(js, job);
    JG_MUTEX_UNLOCK(&js->stats_mutex);
}