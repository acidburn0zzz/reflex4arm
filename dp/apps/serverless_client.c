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
 * serverless_client.c - serverless simulation client modified from reflex_client.c
 */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>

#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>

#include <ixev.h>
#include <ix/mempool.h>
#include <ix/list.h>
#include <ix/timer.h>

#include "reflex.h"
#include "job_recv.h"
#include "load_gen.h"

#include <netinet/in.h>

struct nvme_req {
	uint8_t cmd;
	unsigned long lba;
	unsigned int lba_count;
	struct ixev_nvme_req_ctx ctx;
	size_t size;
	struct job_conn *conn;
	struct ixev_ref ref; 		//for zero-copy
	struct list_node link;
	unsigned long sent_time;
	void *remote_req_handle;
	char *buf;					//nvme buffer to read/write data into
};

struct job_conn {
	struct ixev_ctx ctx;
	size_t rx_received;			//the amount of data received/sent for the current ReFlex request
	size_t tx_sent;
	bool rx_pending;			//is there a ReFlex req currently being received/sent
	bool tx_pending;
	int nvme_pending;
	long in_flight_pkts;
	long sent_pkts;
	long list_len;
	bool receive_loop;
	unsigned long seq_count;
	struct list_head pending_requests;
	long nvme_fg_handle; 		//nvme flow group handle
	char data[4096 + sizeof(BINARY_HEADER)];
	char data_send[sizeof(BINARY_HEADER)];
};

static struct mempool_datastore job_conn_datastore;
static __thread struct mempool job_conn_pool;

static void receive_req(struct job_conn *conn) {

}

int send_client_req(struct nvme_req *req) {

}

int send_pending_client_reqs(struct job_conn *conn) {

}

void send_handler(void * arg, int num_req) {

}

static void main_handler(struct ixev_ctx *ctx, unsigned int reason) {

}

static void job_release(struct ixev_ctx *ctx) {

}

static void job_dialed(struct ixev_ctx *ctx, long ret) {

}

static struct ixev_ctx *job_accept(struct ip_tuple *id) {
	return NULL;
}


static struct ixev_conn_ops job_conn_ops = {
	.accept		= &job_accept,
	.release	= &job_release,
	.dialed     = &job_dialed,
};

static void* receive_loop(void *arg) {

}

int serverless_client_main(int argc, char *argv[]) {

	return 0;
}