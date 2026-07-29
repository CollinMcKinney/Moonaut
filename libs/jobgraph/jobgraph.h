#ifndef JOBGRAPH_H
#define JOBGRAPH_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jobgraph_t jobgraph_t;
typedef struct job_t job_t;
typedef struct jobchain_t jobchain_t;
typedef struct joblink_t joblink_t;

typedef job_t* (*job_func_t)(void* context);

/* ==================== JOB SYSTEM ==================== */

jobgraph_t* jobgraph_create(int worker_threads);
void        jobgraph_destroy(jobgraph_t* js);
void        jobgraph_reset(jobgraph_t* js);          /* recycle job memory, free chains */

int          jobgraph_pending_count(jobgraph_t* js);
unsigned long jobgraph_total_completed(jobgraph_t* js);

/* ==================== JOB ==================== */

job_t*       job_create(jobgraph_t* js, job_func_t func, void* context);
unsigned int job_get_id(job_t* job);
void         job_wait(job_t* job);

/* ==================== CHAIN & LINK (the only way to build work) ========= */

jobchain_t* jobgraph_add_chain(jobgraph_t* js);      /* create a new chain in the graph */
joblink_t*  jobchain_add_link(jobchain_t* chain);    /* add an empty stage at the end */
void        joblink_add_job(joblink_t* link, job_t* job);
void        joblink_add_chain(joblink_t* link, jobchain_t* sub_chain); /* embed a sub‑chain */

unsigned int joblink_get_id(joblink_t* link);        /* for debugging */

/* ==================== ORDERING (cross‑chain) ==================== */
void jobchain_then(jobchain_t* first, jobchain_t* second);

/* ==================== EXECUTION ==================== */
void jobgraph_submit(jobgraph_t* js);    /* submit all chains at once */
void jobgraph_wait(jobgraph_t* js);      /* wait until everything finishes */

#ifdef __cplusplus
}
#endif

#endif /* JOBGRAPH_H */