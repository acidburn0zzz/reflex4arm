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

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "job/zhelpers.h"

#define MAX_NODE_NUM 4
#define NUM_OF_WORKER 4
#define NUM_OF_ROUND 20
#define MAX_REQ_LATENCY

#define JOB_RECV_NEW 0x00
#define JOB_RECV_DONE 0x01
#define JOB_RECV_AGAIN -EAGAIN
#define JOB_RECV_NONE -1

// struct __attribute__((__packed__)) job_ctx {
struct job_ctx {
    unsigned long id;
    unsigned long part_id;
    unsigned int dst;  // dst id, from 0 to MAX_NODE_NUM
    // long job_dst; // 0xffffffff, support up to 64 storage node
    unsigned int latency_us_SLO;
    unsigned long IOPS_SLO;
    unsigned int rw_ratio_SLO;
    unsigned int req_size;  // in lbas, 512 bytes in default
    unsigned int replicas;
    unsigned long duration;
    unsigned long start_addr;
    // unsigned long end_addr;
    unsigned long capacity;  // in lbas, 512 bytes in default
    unsigned long MAX_IOPS;
    unsigned long MIN_IOPS;
    unsigned long delay;
    bool unifom;
    bool sequential;
    bool read_once;
};

struct job_metrics {
    unsigned long job_id;
    unsigned long part_id;
    unsigned long avg;
    unsigned long max;
    unsigned long sent;
    unsigned long measured_num;
    unsigned long missed_sends;
    unsigned long sent_time;
    unsigned long start_time;
    unsigned long measurements[MAX_REQ_LATENCY];
};

struct job_req {
    unsigned int tid;
    long avail_nodes;  // 0xffffffff, prepresenting up to 64 stroage nodes
                       // struct job_metrics metrics;
};

struct job_rep {
    int command;
    long work_nodes;
};

void req_a_job(void *req);
int recv_job_meta(void *req, struct job_rep *jrep);
int recv_a_job(void *req, int tid, struct job_ctx *next_job);
// void job_worker_init(struct job_worker_ops ops);
void *job_worker(void *arg);
void *job_conn_init(int tid);
void job_conn_destroy(int tid);