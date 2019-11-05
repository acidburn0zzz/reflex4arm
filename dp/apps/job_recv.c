/*
 * Copyright (c) 2018-2019, University of California, Santa Cruz
 *  
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *  * Redistributions of source code must retain the above copyright notice, 
 *    this list of conditions and the following disclaimer.
 * 
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 *  * Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * job_recv.c - a zeromq-based multi-thread job receving module
 */

#include "job_recv.h"


int req_jobs(void *req) {
    s_sendmore(req, "");
    struct job_req *jreq= malloc(sizeof *jreq);
    jreq->avail_nodes = 0x000f;
    zmq_send(req, jreq, sizeof(struct job_req), ZMQ_DONTWAIT);
    free(jreq);
    return 0;
}

int recv_jobs(void *req, int tid) {
    int job_nr = 0;
    int i, ret;
    struct job_ctx *new_jobs[MAX_NODES];
    // struct job_ctx *new_job;
    // new_jobs = malloc(MAX_NODES * sizeof(struct job_ctx));
    // new_job = malloc(sizeof(struct job_ctx));
    // free(s_recv(req)); // evelop the delimiter

    while (1) {
        // char *msg = malloc(10);
        // zmq_recv(req, msg, 10, 0);
        // zmq_recv(req, new_job, sizeof(struct job_ctx), 0);
        new_jobs[job_nr] = malloc(sizeof(struct job_ctx));
        ret = zmq_recv(req, new_jobs[job_nr], sizeof(struct job_ctx), 0);
        // printf("Received: %s\n", msg);
        // free(msg);
        if (ret < sizeof(struct job_ctx))
            break;
        
        // printf("Received JOB-%ld, IOPS_SLO is %ld\n", new_job->job_id, new_job->IOPS_SLO);
        printf("[%d]Received JOB-%ld-%ld, IOPS_SLO is %ld\n",
            tid,
            new_jobs[job_nr]->job_id,
            new_jobs[job_nr]->part_id,
            new_jobs[job_nr]->IOPS_SLO);
        // free(new_job);
        job_nr++;

        // do some random work
        s_sleep(randof(500) + 1);
    }

    // printf("We got %d jobs.\n", job_nr);
    for(i = 0; i < job_nr; i++) {
        free(new_jobs[i]);
    }
    return 0;
}

void *worker(void *arg) {
    int tid = * (int *)arg;
    printf("[%d]Connecting to the job server…\n", tid);
    void *context = zmq_ctx_new();
    void *requester = zmq_socket(context, ZMQ_DEALER);
    // zmq_connect(requester, "tcp://localhost:5555");
    s_set_id(requester);
    int ret;
    ret = zmq_connect(requester, "ipc:///tmp/job_backend");
    assert(ret == 0);
    
    int round;
    for (round = 0; round != NUM_OF_ROUND; round++) {
        // FIXME: make non-blocking request
        printf("[%d]Request some jobs... round-%d\n", tid, round);
        req_jobs(requester);
        recv_jobs(requester, tid);
    }
    zmq_close(requester);
    zmq_ctx_destroy(context);
}

int main() 
{ 
    unsigned long i; 
    pthread_t thread_id; 
  
    // Let us create eight threads 
    for (i = 0; i < NUM_OF_WORKER; i++) {
    // for (i = 0; i < 1; i++) {
        printf("Launching new thread %ld\n", i);
        pthread_create(&thread_id, NULL, worker, (void *)&i); 
        sleep(1);
    }
    pthread_exit(NULL); 
    return 0; 
} 