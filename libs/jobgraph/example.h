

#include "jobgraph.h"


jobgraph_t* g_jobgraph;

#define IDEAL_THREAD_COUNT 4
#define NULL ((void*)0)

void simulation_finalize();

void runtime_update();

void runtime_start() {
    g_jobgraph = jobgraph_create(IDEAL_THREAD_COUNT);

    while (1) {
        jobchain_t* frame_chain = jobgraph_add_chain(g_jobgraph);
        
        joblink_t* simulation_link = jobchain_add_link(frame_chain);
        
        job_t* input_job = job_create(g_jobgraph, NULL, 0);
        job_t* network_job = job_create(g_jobgraph, NULL, 0);
        job_t* scripts_job = job_create(g_jobgraph, NULL, 0);
        job_t* physics_job = job_create(g_jobgraph, NULL, 0);
        job_t* audio_job = job_create(g_jobgraph, NULL, 0);
        job_t* render_job = job_create(g_jobgraph, NULL, 0);

        joblink_add_job(simulation_link, input_job);
        joblink_add_job(simulation_link, scripts_job);
        joblink_add_job(simulation_link, network_job);
        joblink_add_job(simulation_link, physics_job);
        joblink_add_job(simulation_link, audio_job);
        joblink_add_job(simulation_link, render_job);

        joblink_t* finalize_link = jobchain_add_link(frame_chain);
        job_t* finalize_job = job_create(g_jobgraph, NULL, 0);
        joblink_add_job(finalize_link, finalize_job);
        
        jobchain_submit(frame_chain);
        jobchain_wait(frame_chain);
    }
}

