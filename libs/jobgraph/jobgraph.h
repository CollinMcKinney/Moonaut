#ifndef JOBGRAPH_H
#define JOBGRAPH_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jobgraph_system_t jobgraph_system_t;
typedef struct job_t job_t;
typedef struct jobchain_t jobchain_t;
typedef struct joblink_t joblink_t;               /* renamed */

typedef job_t* (*job_func_t)(void* context);

/* ==================== SYSTEM MANAGEMENT ==================== */

jobgraph_system_t* jobgraph_system_create(int worker_threads);
void jobgraph_system_destroy(jobgraph_system_t* js);
void jobgraph_wait_all(jobgraph_system_t* js);
int  jobgraph_pending_count(jobgraph_system_t* js);
int  jobgraph_running_count(jobgraph_system_t* js);
unsigned long jobgraph_total_completed(jobgraph_system_t* js);

/* ==================== JOB CREATION ==================== */

job_t* job_create(jobgraph_system_t* js, job_func_t func, void* context);
job_t* job_create_standalone(jobgraph_system_t* js, job_func_t func, void* context);

/* ==================== JOB CHAINS (link-based fork/join) ==================== */

jobchain_t* jobchain_create(jobgraph_system_t* js);

/* Append a new stage (link) with the given job, return the link handle */
joblink_t* jobchain_add_link(jobchain_t* chain, job_t* job);

/* Add an extra job to an existing link (all jobs in a link run in parallel) */
job_t*     joblink_add_job(joblink_t* link, job_t* job);

/* Add a job at a specific numeric link index (advanced usage) */
job_t*     jobchain_add_job_at(jobchain_t* chain, int link_index, job_t* job);

/* Add an entire sub‑chain as a parallel element inside a link */
void       jobchain_add_chain_to_link(jobchain_t* chain, joblink_t* link,
                                      jobchain_t* sub_chain);

unsigned int joblink_get_id(joblink_t* link);

void       jobchain_submit(jobchain_t* chain);
void       jobchain_wait(jobchain_t* chain);
int        jobchain_is_complete(jobchain_t* chain);

/* ==================== DEPENDENCIES ==================== */

void         job_add_dependency(job_t* job_a, job_t* job_b);
void         jobchain_add_dependency(job_t* job_a, jobchain_t* chain_b);
void         jobchain_add_dependency_chain(jobchain_t* chain_a, jobchain_t* chain_b);
void         job_wait(job_t* job);
unsigned int job_get_id(job_t* job);

/* ==================== STANDALONE JOB SUBMISSION ==================== */

void job_submit(job_t* job);

#ifdef __cplusplus
}
#endif

#endif /* JOBGRAPH_H */