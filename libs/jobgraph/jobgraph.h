#ifndef JOBGRAPH_H
#define JOBGRAPH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles */
typedef struct jobgraph_system_t jobgraph_system_t;
typedef struct job_t job_t;
typedef struct jobchain_t jobchain_t;

/* Job function signature - returns next job in chain or NULL */
typedef job_t* (*job_func_t)(void* context);

/* ==================== SYSTEM MANAGEMENT ==================== */

/**
 * Create a job graph system with specified worker threads
 * @param worker_threads Number of worker threads (0 = auto-detect to CPU count)
 */
jobgraph_system_t* jobgraph_system_create(int worker_threads);

/**
 * Destroy the system (waits for all jobs to complete)
 */
void jobgraph_system_destroy(jobgraph_system_t* js);

/**
 * Wait for all submitted jobs to complete
 */
void jobgraph_wait_all(jobgraph_system_t* js);

/**
 * Get number of pending jobs (not yet started)
 */
int jobgraph_pending_count(jobgraph_system_t* js);

/**
 * Get number of currently executing jobs
 */
int jobgraph_running_count(jobgraph_system_t* js);

/**
 * Get total jobs completed since system creation
 */
unsigned long jobgraph_total_completed(jobgraph_system_t* js);

/* ==================== JOB CHAINS ==================== */

/**
 * Create a new job chain (sequential jobs that execute in order)
 * @param js System handle
 * @param start_func First function in the chain
 * @param context User data passed to the function
 */
jobchain_t* jobchain_create(jobgraph_system_t* js, job_func_t start_func, void* context);

/**
 * Add a job to the end of a chain
 * @param chain Chain to add to
 * @param func Job function
 * @param context User data
 * @return Job handle for dependency setup
 */
job_t* jobchain_add_job(jobchain_t* chain, job_func_t func, void* context);

/**
 * Submit a chain for execution
 */
void jobchain_submit(jobchain_t* chain);

/**
 * Wait for a specific chain to complete
 */
void jobchain_wait(jobchain_t* chain);

/**
 * Check if chain is complete (non-blocking)
 * @return 1 if complete, 0 otherwise
 */
int jobchain_is_complete(jobchain_t* chain);

/* ==================== DEPENDENCIES ==================== */

/**
 * Add dependency: job_b waits for job_a to complete
 */
void job_add_dependency(job_t* job_a, job_t* job_b);

/**
 * Add dependency: entire chain_b waits for job_a to complete
 */
void jobchain_add_dependency(job_t* job_a, jobchain_t* chain_b);

/**
 * Add dependency: chain_b waits for chain_a to complete
 */
void jobchain_add_dependency_chain(jobchain_t* chain_a, jobchain_t* chain_b);

/**
 * Wait for a specific job to complete
 */
void job_wait(job_t* job);

/**
 * Get job ID for debugging (returns 0 if invalid)
 */
unsigned int job_get_id(job_t* job);

/**
 * Get chain ID for debugging
 */
unsigned int jobchain_get_id(jobchain_t* chain);

/* ==================== STANDALONE JOBS ==================== */

/**
 * Create a standalone job (not part of a chain)
 */
job_t* job_create_standalone(jobgraph_system_t* js, job_func_t func, void* context);

/**
 * Submit standalone job
 */
void job_submit(job_t* job);

#ifdef __cplusplus
}
#endif

#endif /* JOBGRAPH_H */