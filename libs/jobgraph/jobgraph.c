#include "jobgraph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Platform detection */
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

/* ==================== CONFIGURATION ==================== */

#define JG_JOB_POOL_SIZE 4096
#define JG_MAX_WORKERS 256
#define JG_INITIAL_DEP_CAPACITY 4

/* ==================== SYNCHRONIZATION ==================== */

#ifdef JG_WINDOWS
    typedef CRITICAL_SECTION jg_mutex_t;
    typedef CONDITION_VARIABLE jg_cond_t;
    
    #define JG_MUTEX_INIT(m) InitializeCriticalSection(m)
    #define JG_MUTEX_LOCK(m) EnterCriticalSection(m)
    #define JG_MUTEX_UNLOCK(m) LeaveCriticalSection(m)
    #define JG_MUTEX_DESTROY(m) DeleteCriticalSection(m)
    
    #define JG_COND_INIT(c) InitializeConditionVariable(c)
    #define JG_COND_WAIT(c, m) SleepConditionVariableCS(c, m, INFINITE)
    #define JG_COND_SIGNAL(c) WakeConditionVariable(c)
    #define JG_COND_BROADCAST(c) WakeAllConditionVariable(c)
    #define JG_COND_DESTROY(c) ((void)0)
#else
    typedef pthread_mutex_t jg_mutex_t;
    typedef pthread_cond_t jg_cond_t;
    
    #define JG_MUTEX_INIT(m) pthread_mutex_init(m, NULL)
    #define JG_MUTEX_LOCK(m) pthread_mutex_lock(m)
    #define JG_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
    #define JG_MUTEX_DESTROY(m) pthread_mutex_destroy(m)
    
    #define JG_COND_INIT(c) pthread_cond_init(c, NULL)
    #define JG_COND_WAIT(c, m) pthread_cond_wait(c, m)
    #define JG_COND_SIGNAL(c) pthread_cond_signal(c)
    #define JG_COND_BROADCAST(c) pthread_cond_broadcast(c)
    #define JG_COND_DESTROY(c) pthread_cond_destroy(c)
#endif

/* ==================== JOB TYPES ==================== */

typedef enum {
    JOB_STATE_WAITING,
    JOB_STATE_READY,
    JOB_STATE_RUNNING,
    JOB_STATE_COMPLETE,
    JOB_STATE_CANCELLED
} job_state_t;

/* Job dependency tracking */
typedef struct {
    unsigned int needed;
    unsigned int satisfied;
    unsigned int signalled;
} job_deps_t;

/* Forward declarations */
struct jobchain_t;

/* Individual job */
struct job_t {
    job_func_t func;
    void* context;
    job_deps_t deps;
    job_state_t state;
    struct job_t* next_in_chain;
    struct jobchain_t* parent_chain;
    unsigned int job_id;
    
    struct job_t** dependents;
    unsigned int dependent_count;
    unsigned int dependent_capacity;
    
    int is_standalone;
};

/* Job chain - sequential jobs */
struct jobchain_t {
    struct job_t* first_job;
    struct job_t* last_job;
    struct job_t* current_job;
    struct jobgraph_system_t* system;
    unsigned int chain_id;
    int submitted;
    int completed;
};

/* Ready queue node */
typedef struct ready_node_t {
    struct job_t* job;
    struct ready_node_t* next;
} ready_node_t;

/* Ready queue */
typedef struct {
    ready_node_t* head;
    ready_node_t* tail;
    jg_mutex_t mutex;
    jg_cond_t not_empty;
    unsigned int count;
} ready_queue_t;

/* Worker thread */
typedef struct {
    int id;
    struct jobgraph_system_t* system;
#ifdef JG_WINDOWS
    HANDLE thread;
#else
    pthread_t thread;
#endif
    int running;
    unsigned long jobs_executed;
} worker_t;

/* Main system */
struct jobgraph_system_t {
    worker_t* workers;
    unsigned int worker_count;
    volatile int running;
    
    ready_queue_t ready_queue;
    jg_mutex_t stats_mutex;
    jg_cond_t all_complete;
    
    volatile unsigned int total_jobs_created;
    volatile unsigned long total_jobs_completed;
    volatile unsigned int pending_jobs;
    volatile unsigned int running_jobs;
    
    struct job_t* job_pool;
    unsigned int job_pool_size;
    unsigned int job_pool_used;
    jg_mutex_t pool_mutex;
    
    unsigned int next_job_id;
    unsigned int next_chain_id;
    jg_mutex_t id_mutex;
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
    ready_node_t* node;
    ready_node_t* next;
    
    node = q->head;
    while (node != NULL) {
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
    if (q->tail != NULL) {
        q->tail->next = node;
        q->tail = node;
    } else {
        q->head = node;
        q->tail = node;
    }
    q->count++;
    JG_COND_SIGNAL(&q->not_empty);
    JG_MUTEX_UNLOCK(&q->mutex);
}

static struct job_t* ready_queue_pop(ready_queue_t* q) {
    ready_node_t* node;
    struct job_t* job;
    
    JG_MUTEX_LOCK(&q->mutex);
    
    while (q->head == NULL) {
        JG_COND_WAIT(&q->not_empty, &q->mutex);
    }
    
    node = q->head;
    q->head = node->next;
    if (q->head == NULL) {
        q->tail = NULL;
    }
    q->count--;
    
    job = node->job;
    free(node);
    
    JG_MUTEX_UNLOCK(&q->mutex);
    return job;
}

/* ==================== JOB MEMORY POOL ==================== */

static struct job_t* pool_alloc_job(jobgraph_system_t* js) {
    struct job_t* job = NULL;
    
    JG_MUTEX_LOCK(&js->pool_mutex);
    
    if (js->job_pool_used < js->job_pool_size) {
        job = &js->job_pool[js->job_pool_used];
        js->job_pool_used++;
        memset(job, 0, sizeof(struct job_t));
    }
    
    JG_MUTEX_UNLOCK(&js->pool_mutex);
    
    if (job == NULL) {
        job = (struct job_t*)calloc(1, sizeof(struct job_t));
    }
    
    return job;
}

static void pool_free_job(jobgraph_system_t* js, struct job_t* job) {
    if (job->dependents != NULL) {
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
}

/* ==================== DEPENDENCY MANAGEMENT ==================== */

static void add_dependent(struct job_t* job, struct job_t* dependent) {
    struct job_t** new_list;
    unsigned int new_cap;
    
    if (job->dependent_count + 1 >= job->dependent_capacity) {
        new_cap = job->dependent_capacity * 2;
        if (new_cap < JG_INITIAL_DEP_CAPACITY) {
            new_cap = JG_INITIAL_DEP_CAPACITY;
        }
        
        new_list = (struct job_t**)realloc(job->dependents, new_cap * sizeof(struct job_t*));
        if (new_list != NULL) {
            job->dependents = new_list;
            job->dependent_capacity = new_cap;
        } else {
            return;
        }
    }
    
    job->dependents[job->dependent_count] = dependent;
    job->dependent_count++;
    dependent->deps.needed++;
}

static void signal_job_complete(struct job_t* job) {
    jobgraph_system_t* js;
    unsigned int i;
    
    if (job->parent_chain == NULL) return;
    if (job->is_standalone) {
        js = (jobgraph_system_t*)job->parent_chain;
    } else {
        js = job->parent_chain->system;
    }
    if (js == NULL) return;
    
    for (i = 0; i < job->dependent_count; i++) {
        struct job_t* dep = job->dependents[i];
        
        JG_MUTEX_LOCK(&js->stats_mutex);
        dep->deps.satisfied++;
        
        if (dep->deps.satisfied >= dep->deps.needed && 
            dep->state == JOB_STATE_WAITING &&
            !dep->deps.signalled) {
            dep->deps.signalled = 1;
            dep->state = JOB_STATE_READY;
            ready_queue_push(&js->ready_queue, dep);
        }
        JG_MUTEX_UNLOCK(&js->stats_mutex);
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
    struct job_t* current_job;
    struct job_t* next_job;
    
    worker->jobs_executed = 0;
    
    while (js->running) {
        current_job = ready_queue_pop(&js->ready_queue);
        
        if (current_job == NULL || !js->running) {
            continue;
        }
        
        JG_MUTEX_LOCK(&js->stats_mutex);
        current_job->state = JOB_STATE_RUNNING;
        js->running_jobs++;
        if (js->pending_jobs > 0) js->pending_jobs--;
        JG_MUTEX_UNLOCK(&js->stats_mutex);
        
        next_job = current_job->func(current_job->context);
        
        JG_MUTEX_LOCK(&js->stats_mutex);
        current_job->state = JOB_STATE_COMPLETE;
        js->running_jobs--;
        js->total_jobs_completed++;
        worker->jobs_executed++;
        
        signal_job_complete(current_job);
        
        if (next_job != NULL) {
            next_job->state = JOB_STATE_READY;
            next_job->deps.signalled = 1;
            ready_queue_push(&js->ready_queue, next_job);
        } else if (!current_job->is_standalone && current_job->parent_chain != NULL) {
            jobchain_t* chain = current_job->parent_chain;
            if (chain->current_job == current_job) {
                chain->current_job = current_job->next_in_chain;
                if (chain->current_job != NULL) {
                    chain->current_job->deps.signalled = 1;
                    chain->current_job->state = JOB_STATE_READY;
                    ready_queue_push(&js->ready_queue, chain->current_job);
                } else {
                    chain->completed = 1;
                }
            }
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

/* ==================== PUBLIC API ==================== */

jobgraph_system_t* jobgraph_system_create(int worker_threads) {
    jobgraph_system_t* js;
    int i;
    
    if (worker_threads <= 0) {
        worker_threads = 2;
    }
    if (worker_threads > JG_MAX_WORKERS) {
        worker_threads = JG_MAX_WORKERS;
    }
    
    js = (jobgraph_system_t*)calloc(1, sizeof(jobgraph_system_t));
    if (js == NULL) return NULL;
    
    js->worker_count = worker_threads;
    js->running = 1;
    
    ready_queue_init(&js->ready_queue);
    JG_MUTEX_INIT(&js->stats_mutex);
    JG_COND_INIT(&js->all_complete);
    JG_MUTEX_INIT(&js->pool_mutex);
    JG_MUTEX_INIT(&js->id_mutex);
    
    js->job_pool_size = JG_JOB_POOL_SIZE;
    js->job_pool = (struct job_t*)calloc(js->job_pool_size, sizeof(struct job_t));
    if (js->job_pool == NULL) {
        free(js);
        return NULL;
    }
    js->job_pool_used = 0;
    
    js->workers = (worker_t*)calloc(worker_threads, sizeof(worker_t));
    if (js->workers == NULL) {
        free(js->job_pool);
        free(js);
        return NULL;
    }
    
    for (i = 0; i < worker_threads; i++) {
        js->workers[i].id = i;
        js->workers[i].system = js;
        js->workers[i].running = 1;
        
#ifdef JG_WINDOWS
        js->workers[i].thread = (HANDLE)_beginthreadex(NULL, 0, worker_thread_func, &js->workers[i], 0, NULL);
        if (js->workers[i].thread == NULL) {
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
    unsigned int i;
    
    if (js == NULL) return;
    
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
    
    free(js->workers);
    free(js->job_pool);
    free(js);
}

void jobgraph_wait_all(jobgraph_system_t* js) {
    if (js == NULL) return;
    
    JG_MUTEX_LOCK(&js->stats_mutex);
    while (js->pending_jobs > 0 || js->running_jobs > 0) {
        JG_COND_WAIT(&js->all_complete, &js->stats_mutex);
    }
    JG_MUTEX_UNLOCK(&js->stats_mutex);
}

int jobgraph_pending_count(jobgraph_system_t* js) {
    int count;
    if (js == NULL) return 0;
    JG_MUTEX_LOCK(&js->stats_mutex);
    count = js->pending_jobs;
    JG_MUTEX_UNLOCK(&js->stats_mutex);
    return count;
}

int jobgraph_running_count(jobgraph_system_t* js) {
    int count;
    if (js == NULL) return 0;
    JG_MUTEX_LOCK(&js->stats_mutex);
    count = js->running_jobs;
    JG_MUTEX_UNLOCK(&js->stats_mutex);
    return count;
}

unsigned long jobgraph_total_completed(jobgraph_system_t* js) {
    unsigned long count;
    if (js == NULL) return 0;
    JG_MUTEX_LOCK(&js->stats_mutex);
    count = js->total_jobs_completed;
    JG_MUTEX_UNLOCK(&js->stats_mutex);
    return count;
}

/* Job Chain API */
jobchain_t* jobchain_create(jobgraph_system_t* js, job_func_t start_func, void* context) {
    jobchain_t* chain;
    struct job_t* first_job;
    
    if (js == NULL || start_func == NULL) return NULL;
    
    chain = (jobchain_t*)calloc(1, sizeof(jobchain_t));
    if (chain == NULL) return NULL;
    
    first_job = pool_alloc_job(js);
    if (first_job == NULL) {
        free(chain);
        return NULL;
    }
    
    JG_MUTEX_LOCK(&js->id_mutex);
    first_job->job_id = js->next_job_id;
    js->next_job_id++;
    chain->chain_id = js->next_chain_id;
    js->next_chain_id++;
    JG_MUTEX_UNLOCK(&js->id_mutex);
    
    first_job->func = start_func;
    first_job->context = context;
    first_job->parent_chain = chain;
    first_job->is_standalone = 0;
    
    chain->first_job = first_job;
    chain->last_job = first_job;
    chain->current_job = first_job;
    chain->system = js;
    chain->submitted = 0;
    chain->completed = 0;
    
    return chain;
}

struct job_t* jobchain_add_job(jobchain_t* chain, job_func_t func, void* context) {
    struct job_t* new_job;
    jobgraph_system_t* js;
    
    if (chain == NULL || func == NULL) return NULL;
    
    js = chain->system;
    new_job = pool_alloc_job(js);
    if (new_job == NULL) return NULL;
    
    JG_MUTEX_LOCK(&js->id_mutex);
    new_job->job_id = js->next_job_id;
    js->next_job_id++;
    JG_MUTEX_UNLOCK(&js->id_mutex);
    
    new_job->func = func;
    new_job->context = context;
    new_job->deps.needed = 1;
    new_job->parent_chain = chain;
    new_job->is_standalone = 0;
    
    if (chain->last_job != NULL) {
        chain->last_job->next_in_chain = new_job;
    }
    chain->last_job = new_job;
    
    return new_job;
}

void jobchain_submit(jobchain_t* chain) {
    struct job_t* first_job;
    jobgraph_system_t* js;
    
    if (chain == NULL || chain->submitted) return;
    
    first_job = chain->first_job;
    js = chain->system;
    
    if (first_job == NULL) return;
    
    JG_MUTEX_LOCK(&js->stats_mutex);
    js->total_jobs_created++;
    js->pending_jobs++;
    
    first_job->deps.signalled = 1;
    first_job->state = JOB_STATE_READY;
    ready_queue_push(&js->ready_queue, first_job);
    
    chain->submitted = 1;
    JG_MUTEX_UNLOCK(&js->stats_mutex);
}

void jobchain_wait(jobchain_t* chain) {
    if (chain == NULL) return;
    
    JG_MUTEX_LOCK(&chain->system->stats_mutex);
    while (!chain->completed) {
        JG_COND_WAIT(&chain->system->all_complete, &chain->system->stats_mutex);
    }
    JG_MUTEX_UNLOCK(&chain->system->stats_mutex);
}

int jobchain_is_complete(jobchain_t* chain) {
    if (chain == NULL) return 1;
    return chain->completed;
}

unsigned int jobchain_get_id(jobchain_t* chain) {
    if (chain == NULL) return 0;
    return chain->chain_id;
}

/* Dependency API */
void job_add_dependency(struct job_t* job_a, struct job_t* job_b) {
    if (job_a == NULL || job_b == NULL) return;
    add_dependent(job_a, job_b);
}

void jobchain_add_dependency(struct job_t* job_a, jobchain_t* chain_b) {
    if (job_a == NULL || chain_b == NULL) return;
    if (chain_b->first_job != NULL) {
        add_dependent(job_a, chain_b->first_job);
    }
}

void jobchain_add_dependency_chain(jobchain_t* chain_a, jobchain_t* chain_b) {
    if (chain_a == NULL || chain_b == NULL) return;
    if (chain_a->last_job != NULL && chain_b->first_job != NULL) {
        add_dependent(chain_a->last_job, chain_b->first_job);
    }
}

void job_wait(struct job_t* job) {
    jobgraph_system_t* js;
    
    if (job == NULL) return;
    if (job->parent_chain == NULL) return;
    if (job->is_standalone) {
        js = (jobgraph_system_t*)job->parent_chain;
    } else {
        js = job->parent_chain->system;
    }
    if (js == NULL) return;
    
    JG_MUTEX_LOCK(&js->stats_mutex);
    while (job->state != JOB_STATE_COMPLETE && job->state != JOB_STATE_CANCELLED) {
        JG_COND_WAIT(&js->all_complete, &js->stats_mutex);
    }
    JG_MUTEX_UNLOCK(&js->stats_mutex);
}

unsigned int job_get_id(struct job_t* job) {
    if (job == NULL) return 0;
    return job->job_id;
}

/* Standalone jobs */
struct job_t* job_create_standalone(jobgraph_system_t* js, job_func_t func, void* context)
{
    struct job_t* job;
    
    if (js == NULL || func == NULL) return NULL;
    
    job = pool_alloc_job(js);
    if (job == NULL) return NULL;
    
    JG_MUTEX_LOCK(&js->id_mutex);
    job->job_id = js->next_job_id;
    js->next_job_id++;
    JG_MUTEX_UNLOCK(&js->id_mutex);
    
    job->func = func;
    job->context = context;
    job->parent_chain = (jobchain_t*)js;
    job->is_standalone = 1;
    
    return job;
}

void job_submit(struct job_t* job) {
    jobgraph_system_t* js;
    
    if (job == NULL) return;
    if (job->is_standalone) {
        js = (jobgraph_system_t*)job->parent_chain;
    } else {
        js = job->parent_chain->system;
    }
    if (js == NULL) return;
    
    JG_MUTEX_LOCK(&js->stats_mutex);
    js->total_jobs_created++;
    js->pending_jobs++;
    
    job->deps.signalled = 1;
    job->state = JOB_STATE_READY;
    ready_queue_push(&js->ready_queue, job);
    JG_MUTEX_UNLOCK(&js->stats_mutex);
}
