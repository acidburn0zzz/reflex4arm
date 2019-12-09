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

#include "job/job_recv.h"

static void *zmq_requesters[NUM_OF_WORKER];
static void *zmq_contexts[NUM_OF_WORKER];

int req_jobs(void *req, int job_nr) {
    s_sendmore(req, "");
    struct job_req *jreq = malloc(sizeof *jreq);
    jreq->avail_nodes = 0x000f;
    while (job_nr--)
        zmq_send(req, jreq, sizeof(struct job_req), ZMQ_DONTWAIT);

    free(jreq);
    return 0;
}

// Non-blocking
int recv_jobs(void *req, int tid, struct job_ctx *next_job) {
    int i, ret;
    char *test;

    ret = zmq_recv(req, next_job, sizeof(struct job_ctx), ZMQ_DONTWAIT);
    switch (ret) {
        case sizeof(struct job_ctx):
            printf("[%d]Received JOB-%ld-%ld, IOPS_SLO is %ld, Job Dst is %d, req_size is %d\n",
                   tid,
                   next_job->id,
                   next_job->part_id,
                   next_job->IOPS_SLO,
                   next_job->dst,
                   next_job->req_size);
            return JOB_RECV_NEW;
            break;
        case JOB_RECV_AGAIN:
            return JOB_RECV_AGAIN;
            break;

        case JOB_RECV_NONE:
            break;

        default:  // recv_less than a job
            test = malloc(5);
            test = &next_job;
            printf("Ret: %d; Recv done msg - %s\n", ret, test);
            return JOB_RECV_DONE;
            break;
    }
    return JOB_RECV_AGAIN;
}

void *job_conn_init(int tid) {
    printf("[%d]Connecting to the job server…\n", tid);
    zmq_contexts[tid] = zmq_ctx_new();
    zmq_requesters[tid] = zmq_socket(zmq_contexts[tid], ZMQ_DEALER);
    // zmq_connect(requester, "tcp://localhost:5555");
    s_set_id(zmq_requesters[tid]);
    int ret;
    ret = zmq_connect(zmq_requesters[tid], "ipc:///tmp/job_backend");
    assert(ret == 0);
    return zmq_requesters[tid];
}

void job_conn_destroy(int tid) {
    printf("Cleaning up the job sockets for thread-%d\n", tid);
    zmq_close(zmq_requesters[tid]);
    zmq_ctx_destroy(zmq_contexts[tid]);
}

void *job_worker(void *arg) {
    int tid = *(int *)arg;
    void *requester = job_conn_init(tid);
    int round, i;
    int ret;
    for (round = 0; round != NUM_OF_ROUND; round++) {
        // FIXME: make non-blocking request
        printf("[%d]Request some jobs... round-%d\n", tid, round);
        req_jobs(requester, 1);
        struct job_ctx *new_jobs[MAX_NODE_NUM];
        int job_nr = 0;
        new_jobs[job_nr] = malloc(sizeof(struct job_ctx));
        while (1) {
            ret = recv_jobs(requester, tid, new_jobs[job_nr]);
            if (ret == JOB_RECV_NEW) {
                job_nr++;
                new_jobs[job_nr] = malloc(sizeof(struct job_ctx));
            }
            if (ret == JOB_RECV_DONE)
                break;
        }
        for (i = 0; i < job_nr; ++i) {
            free(new_jobs[i]);
        }
    }
    job_conn_destroy(tid);
}

// int main()
// {
//     unsigned long i;
//     pthread_t thread_id;

//     // Let us create eight threads
//     for (i = 0; i < NUM_OF_WORKER; i++) {
//     // for (i = 0; i < 1; i++) {
//         printf("Launching new thread %ld\n", i);
//         pthread_create(&thread_id, NULL, job_worker, (void *)&i);
//         sleep(1);
//     }
//     pthread_exit(NULL);
//     return 0;
// }