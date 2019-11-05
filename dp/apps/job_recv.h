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

#include "zhelpers.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>

#define MAX_NODES 4
#define NUM_OF_WORKER 8
#define NUM_OF_ROUND 2

// struct __attribute__((__packed__)) job_ctx {
struct job_ctx {
    unsigned long job_id;
    unsigned long part_id;
    unsigned short job_dst;
    // long job_dst; // 0xffffffff, support up to 64 storage node
    unsigned int latency_us_SLO;
    unsigned long IOPS_SLO;
    unsigned int rw_ratio_SLO;
    unsigned int replicas;
    unsigned long duration;
    unsigned long capacity;
    unsigned long MAX_IOPS;
    unsigned long MIN_IOPS;
    unsigned long delay;
    bool unifom;
    bool sequential;
    bool read_once;
};

struct job_req {
    unsigned int tid;
    long avail_nodes; // 0xffffffff, prepresenting up to 64 stroage nodes
};

int req_jobs(void *req);
int recv_jobs(void *req, int tid);
void *worker(void *arg);