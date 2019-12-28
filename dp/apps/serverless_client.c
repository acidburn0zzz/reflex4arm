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

#include <fcntl.h>
#include <stdio.h>
#include "reflex.h"

#include <ix/bitmap.h>
#include <ix/cfg.h>
#include <ix/mempool.h>
#include <ixev.h>
#include <job/job_recv.h>
#include <job/load_gen.h>

#define BINARY_HEADER binary_header_blk_t
#define ROUND_UP(num, multiple) ((((num) + (multiple)-1) / (multiple)) * (multiple))
#define MAKE_IP_ADDR(a, b, c, d)                 \
    (((uint32_t)a << 24) | ((uint32_t)b << 16) | \
     ((uint32_t)c << 8) | (uint32_t)d)

#define MAX_SEND_SIZE 32
#define MAX_LATENCY 2000
#define MAX_THREAD_NUM 8
#define MAX_PORT_NUM 65536
#define MAX_JOB_PER_CORE 32
#define MAX_PAGES_PER_ACCESS 64
#define MAX_CONN_PER_CORE (MAX_JOB_PER_CORE * MAX_NODE_NUM)
#define MAX_RUNTIME_SECOND 1200
#define MAX_IOPS_PER_CORE 500000
#define MAX_THROUGHPUT_PER_CORE 8 * 400000  // 12 Gbps, in lbas
#define OUTSTANDING_REQS 512
#define PAGE_SIZE 4096

struct nvme_req {
    uint8_t cmd;
    unsigned long lba;
    unsigned int lba_count;
    // unsigned int num4k;
    struct ixev_nvme_req_ctx ctx;
    size_t size;
    struct pp_conn *conn;
    struct ixev_ref ref;  //for zero-copy
    struct list_node link;
    unsigned long sent_time;
    void *remote_req_handle;
    char *buf[MAX_PAGES_PER_ACCESS];
    int curr_buf;
};

struct pp_conn {  // separte for workload lib
    unsigned int id;
    struct ixev_ctx ctx;
    size_t rx_received;  //the amount of data received/sent for the current ReFlex request
    size_t tx_sent;
    bool rx_pending;  //is there a ReFlex req currently being received/sent
    bool tx_pending;
    bool alive;
    int nvme_pending;
    long in_flight_pkts;
    long sent_pkts;
    long list_len;
    long cycles_between_req;
    bool req_done;
    // bool receive_loop;
    unsigned long seq_count;  //aka seq_count when verify = 0
    unsigned long last_send;
    struct list_head pending_requests;
    struct job_ctx job;
    struct job_metrics metrics;
    long nvme_fg_handle;  //nvme flow group handle
    char data[4096 + sizeof(BINARY_HEADER)];
    char data_send[sizeof(BINARY_HEADER)];
};

static DEFINE_SPINLOCK(conn_id_bitmap_lock);

static int ns_sector_size = 512;
static int log_ns_sector_size = 9;
static unsigned long cycles_between_jobs = 10 * 1000UL * 1000UL;
static unsigned long total_job_nr;
static volatile unsigned long job_nr_per_core;

static char *ip = NULL;
struct ip_tuple *ip_tuple[MAX_NODE_NUM * 2];
static unsigned int dst_ports[MAX_NODE_NUM * 2] =
    {1234, 1235, 1236, 1237, 1238, 1239, 1240, 1241};

// static struct mempool_datastore job_datastore;
// static struct mempool_datastore job_metrics_datastore;
static struct mempool_datastore pp_conn_datastore;
static struct mempool_datastore nvme_req_datastore;
static struct mempool_datastore nvme_req_buf_datastore;

// static __thread struct mempool job_pool;
// static __thread struct mempool job_metrics_pool;
static __thread struct mempool pp_conn_pool;
static __thread struct mempool nvme_req_pool;
static __thread struct mempool nvme_req_buf_pool;

// static __thread int job_qdepth = 1;
static __thread int req_qdepth = 0;
static __thread int tid;
static __thread int conn_opened = 0;
static __thread int sent_job_reqs = 0;
static __thread int acked_job_reqs = 0;
// static __thread int acked_job_nr = 0;
static __thread int done_job_nr = 0;
static __thread bool job_done = false;
static __thread unsigned long run_time;
static __thread unsigned long current_throughput = 0;  // in lbas
static __thread struct list_head metrics_list;
static __thread unsigned long conn_id_bitmap[BITMAP_LONG_SIZE(MAX_CONN_PER_CORE)];
static __thread struct pp_conn *conns[MAX_CONN_PER_CORE];
// static __thread struct job_ctx *concur_jobs[MAX_CONN_PER_CORE];

static int parse_ip_addr(const char *str, uint32_t *addr) {
    unsigned char a, b, c, d;

    if (sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4)
        return -EINVAL;

    *addr = MAKE_IP_ADDR(a, b, c, d);
    return 0;
}

static inline void update_metrics(struct job_metrics *metrics, struct nvme_req *req) {
    unsigned long latency = (rdtsc() - req->sent_time) / cycles_per_us;
    if (latency >= MAX_LATENCY)
        (metrics->measurements[MAX_LATENCY - 1])++;
    else
        (metrics->measurements[latency])++;

    metrics->avg += latency;
    if (latency > metrics->max)
        metrics->max = latency;

    metrics->measured_num++;
}

void merge_metrics() {
    return;
}

inline void mempool_free_4k(struct nvme_req *req) {
    int i;
    int num4k = req->lba_count / 8;
    if (req->lba_count % 8 != 0)
        num4k++;

    for (i = 0; i < num4k; i++)
        mempool_free(&nvme_req_buf_pool, req->buf[i]);
}

static void receive_req(struct pp_conn *conn) {
    int ret;
    struct nvme_req *req;
    struct job_metrics *metrics = &conn->metrics;
    BINARY_HEADER *header;
    unsigned long start_time = metrics->start_time;
    unsigned long curr_time;

    while (1) {
        if (!conn->rx_pending) {
            ret = ixev_recv(&conn->ctx, &conn->data[conn->rx_received],
                            sizeof(BINARY_HEADER) - conn->rx_received);
            if (ret <= 0) {
                if (ret != -EAGAIN) {
                    if (!conn->nvme_pending) {
                        printf("Connection close 6\n");
                        if (conn->alive) {
                            ixev_close(&conn->ctx);
                            conn->alive = false;
                        }
                    }
                }
                break;  // exception
            } else
                conn->rx_received += ret;
            if (conn->rx_received < sizeof(BINARY_HEADER))
                return;
        }

        conn->rx_pending = true;
        header = (BINARY_HEADER *)&conn->data[0];
        assert(header->magic == sizeof(BINARY_HEADER));

        if (header->opcode == CMD_GET) {
            ret = ixev_recv(&conn->ctx,
                            &conn->data[conn->rx_received],
                            sizeof(BINARY_HEADER) +
                                header->lba_count * ns_sector_size - conn->rx_received);
            if (ret <= 0) {
                if (ret != -EAGAIN) {
                    assert(0);
                    if (!conn->nvme_pending) {
                        printf("Connection close 7\n");
                        if (conn->alive) {
                            ixev_close(&conn->ctx);
                            conn->alive = false;
                        }
                    }
                }
                break;
            }
            conn->rx_received += ret;

            if (conn->rx_received < (sizeof(BINARY_HEADER) +
                                     header->lba_count *
                                         ns_sector_size))
                return;  // terminate
        } else if (header->opcode == CMD_SET) {
        } else {
            printf("Received unsupported command, closing connection\n");
            if (conn->alive) {
                ixev_close(&conn->ctx);
                conn->alive = false;
            }
            return;
        }

        req = header->req_handle;
        update_metrics(&conn->metrics, req);

        mempool_free_4k(req);
        mempool_free(&nvme_req_pool, req);
        conn->rx_pending = false;
        conn->rx_received = 0;

        curr_time = rdtsc();

        conn->req_done =
            (metrics->sent >= conn->job.IOPS_SLO) || (curr_time - start_time > run_time);
        if (conn->req_done) {
            if (conn->alive) {
                printf("[%d] Connection normally closed.\n", percpu_get(cpu_id));
                ixev_close(&conn->ctx);
                conn->alive = false;
                return NULL;
            }
        }

        if (req_qdepth)  // close loop
            req_generator(&conn->ctx, 1);
    }
}

int send_a_req(struct nvme_req *req) {
    struct pp_conn *conn = req->conn;
    int ret = 0;
    BINARY_HEADER *header;

    assert(conn);

    if (!conn->tx_pending) {
        //setup header
        header = (BINARY_HEADER *)&conn->data_send[0];
        header->magic = sizeof(BINARY_HEADER);
        header->opcode = req->cmd;
        header->lba = req->lba;
        header->lba_count = req->lba_count;
        header->req_handle = req;

        while (conn->tx_sent < sizeof(BINARY_HEADER)) {
            ret = ixev_send(&conn->ctx, &conn->data_send[conn->tx_sent],
                            sizeof(BINARY_HEADER) - conn->tx_sent);
            if (ret == -EAGAIN || ret == -ENOBUFS) {
                return -1;
            } else if (ret < 0) {
                printf("ixev_send - ret is %d.\n", ret);
                if (!conn->nvme_pending) {
                    printf("[%d] Connection close 2\n", percpu_get(cpu_id));
                    if (conn->alive) {
                        ixev_close(&conn->ctx);
                        conn->alive = false;
                    }
                }
                return -2;
                ret = 0;
            }
            conn->tx_sent += ret;
        }
        if (conn->tx_sent != sizeof(BINARY_HEADER)) {
            printf("tx_sent is %d, header is %d, last ret is %d.\n", conn->tx_sent, sizeof(BINARY_HEADER), ret);
        }
        assert(conn->tx_sent == sizeof(BINARY_HEADER));
        conn->tx_pending = true;
        conn->tx_sent = 0;
    }
    ret = 0;
    // send request first, then send write data
    if (req->cmd == CMD_SET) {
        while (conn->tx_sent < req->lba_count * ns_sector_size) {
            assert(req->lba_count * ns_sector_size);
            ret = ixev_send_zc(&conn->ctx, &req->buf[req->curr_buf][conn->tx_sent],
                               req->lba_count * ns_sector_size - conn->tx_sent);
            if (ret < 0) {
                if (ret == -EAGAIN || ret == -ENOBUFS)
                    return -2;
                if (!conn->nvme_pending) {
                    printf("Connection close 3\n");
                    if (conn->alive) {
                        ixev_close(&conn->ctx);
                        conn->alive = false;
                    }
                }
                return -2;
            }
            if (ret == 0)
                printf("fhmm ret is zero\n");

            conn->tx_sent += ret;
            if ((conn->tx_sent % PAGE_SIZE) == 0)
                req->curr_buf++;
        }
        assert(req->curr_buf <= req->lba_count / 8 + 1);
    }
    conn->tx_sent = 0;
    conn->tx_pending = false;
    return 0;
}

int send_batched_reqs(struct pp_conn *conn) {
    int sent_reqs = 0;

    while (!list_empty(&conn->pending_requests)) {
        int ret;
        struct nvme_req *req = list_top(&conn->pending_requests, struct nvme_req, link);
        conn->metrics.sent_time = rdtsc();
        ret = send_a_req(req);
        if (!ret) {
            sent_reqs++;
            list_pop(&conn->pending_requests, struct nvme_req, link);
            conn->list_len--;
        } else
            return ret;
    }
    return sent_reqs;
}

inline void mempool_alloc_4k(struct nvme_req *req) {
    int i;
    int num4k = req->lba_count / 8;  // 4096 = 512*8
    assert(num4k <= MAX_PAGES_PER_ACCESS);
    if (num4k % 8 != 0)
        num4k++;

    void *req_buf_array[num4k];
    for (i = 0; i < num4k; i++) {
        req_buf_array[i] = mempool_alloc(&nvme_req_buf_pool);
        if (req_buf_array[i] == NULL) {
            printf("ERROR: alloc of nvme_req_buf failed\n");
            assert(0);
        }
        req->buf[i] = req_buf_array[i];
    }
}

void req_generator(void *arg, int num_req) {
    struct nvme_req *req;
    struct ixev_ctx *ctx = (struct ixev_ctx *)arg;
    struct pp_conn *conn = container_of(ctx, struct pp_conn, ctx);
    struct job_ctx *job = &conn->job;
    struct job_metrics *metrics = &conn->metrics;
    unsigned long now;
    unsigned long ns_size = CFG.ns_sizes[0];
    int ssents = 0;
    int send_cond;

    if (now = rdtsc() - conn->last_send < conn->cycles_between_req)
        return;
    if (metrics->sent == job->IOPS_SLO)
        return;

    while (send_cond) {
        ssents++;
        if (ssents > MAX_SEND_SIZE)
            break;

        req = mempool_alloc(&nvme_req_pool);
        if (!req) {
            receive_req(conn);
            break;
        }

        ixev_nvme_req_ctx_init(&req->ctx);
        req->lba_count = job->req_size;
        req->conn = conn;

        mempool_alloc_4k(req);

        if ((rand() % 99) < job->rw_ratio_SLO)
            req->cmd = CMD_GET;
        else
            req->cmd = CMD_SET;

        if (job->sequential) {
            req->lba = conn->seq_count;
            conn->seq_count += job->req_size;
        } else {
            req->lba = job->start_addr + (rand() % (job->capacity * job->req_size));
        }
        req->conn = conn;

        conn->list_len++;
        list_add_tail(&conn->pending_requests, &req->link);

        conn->last_send = now;
        metrics->sent++;

        send_cond =
            ((now - metrics->start_time) / conn->cycles_between_req) >= metrics->sent && metrics->sent < job->IOPS_SLO;
    }

    int ret = send_batched_reqs(conn);
}

static void main_handler(struct ixev_ctx *ctx, unsigned int reason) {
    struct pp_conn *conn = container_of(ctx, struct pp_conn, ctx);
    int ret;

    if (reason == IXEVOUT) {
        ret = send_batched_reqs(conn);
        if (ret)
            printf("IXEVOUT: CPU %d | Sent %d batched requests.\n", percpu_get(cpu_id), ret);
    } else if (reason == IXEVHUP) {
        printf("Connection close 5\n");
        if (conn->alive) {
            ixev_close(&conn->ctx);
            conn->alive = false;
        }
        return;
    }
    receive_req(conn);
}

static void pp_dialed(struct ixev_ctx *ctx, long ret) {
    struct pp_conn *conn = container_of(ctx, struct pp_conn, ctx);
    unsigned long now = rdtsc();

    ixev_set_handler(&conn->ctx, IXEVIN | IXEVOUT | IXEVHUP, &main_handler);
    conn->alive = true;

    conn_opened++;
    printf("Tenant %d is dialed. (conn_opened: %d).\n", tid, conn_opened);

    while (rdtsc() < now + 10000000) {
    }
    return;
}

static void pp_release(struct ixev_ctx *ctx) {
    struct pp_conn *conn = container_of(ctx, struct pp_conn, ctx);
    done_job_nr++;
    conn_opened--;
    // if(conn_opened==0)
    // 	printf("Tid: %lx All connections released handle %lx open conns still %i\n", pthread_self(), conn->ctx.handle, conn_opened);
    conn->req_done = true;
    pp_conn_close(conn);
}

static struct ixev_ctx *pp_accept(struct ip_tuple *id) {
    return NULL;
}

static struct ixev_conn_ops pp_conn_ops = {
    .accept = &pp_accept,
    .release = &pp_release,
    .dialed = &pp_dialed,
};

static inline void pp_conn_init(struct pp_conn **conn, unsigned int conn_id) {
    struct pp_conn *new_conn = mempool_alloc(&pp_conn_pool);
    *conn = new_conn;
    if (!conn) {
        printf("PP_CONN: MEMPOOL ALLOC FAILED !\n");
        return NULL;
    }
    printf("This new conn is %p.\n", new_conn);
    printf("This conn is %p.\n", conn);
    list_head_init(&new_conn->pending_requests);
    new_conn->id = conn_id;
    new_conn->rx_received = 0;
    new_conn->rx_pending = false;
    new_conn->tx_sent = 0;  // how many bytes were sent
    new_conn->tx_pending = false;
    new_conn->in_flight_pkts = 0x0UL;
    new_conn->sent_pkts = 0x0UL;
    new_conn->list_len = 0x0UL;
    // conn->job = new_job
    // srand(rdtsc());
    // conn->seq_count = rand() % (ns_size >> log_ns_sector_size); // random start, are they same for different threads?
    // conn->seq_count = new_job->start_addr;
    // conn->seq_count = conn->seq_count & ~7; // align

    ixev_ctx_init(&new_conn->ctx);

    new_conn->nvme_fg_handle = 0;  //set to this for now
    new_conn->alive = false;

    // conn->metrics = mempool_alloc(&job_metrics_pool);
    new_conn->metrics.start_time = rdtsc();
}

inline void pp_conn_close(struct pp_conn *conn) {
    list_add_tail(&metrics_list, &conn->metrics);
    current_throughput -= conn->job.IOPS_SLO * conn->job.req_size;
    // mempool_free(&job_pool, conn->job);
    mempool_free(&pp_conn_pool, conn);
    spin_lock(&conn_id_bitmap_lock);
    bitmap_clear(conn_id_bitmap, conn->id);
    spin_unlock(&conn_id_bitmap_lock);
    // free datastore
}

static void *recv_loop(void *arg) {
    int conn_id;
    int flags;
    tid = *(int *)arg;
    void *requester = job_conn_init(tid);

    bitmap_init(conn_id_bitmap, MAX_CONN_PER_CORE, 0);

    int ret = ixev_init_thread();
    if (ret)
        ixev_exit(IXEV_EXIT_FAILURE, "unable to init IXEV.\n");

    // ret = mempool_create(&job_pool, &job_datastore, MEMPOOL_SANITY_GLOBAL, 0);
    // if (ret)
    //     ixev_exit(IXEV_EXIT_FAILURE, "unable to create job mempool\n");

    // ret = mempool_create(&job_metrics_pool, &job_metrics_datastore, MEMPOOL_SANITY_GLOBAL, 0);
    // if (ret)
    //     ixev_exit(IXEV_EXIT_FAILURE, "unable to create job metrics mempool\n");

    ret = mempool_create(&pp_conn_pool, &pp_conn_datastore, MEMPOOL_SANITY_GLOBAL, 0);
    if (ret)
        ixev_exit(IXEV_EXIT_FAILURE, "unable to create pp_conn mempool\n");

    ret = mempool_create(&nvme_req_pool, &nvme_req_datastore, MEMPOOL_SANITY_GLOBAL, 0);
    if (ret)
        ixev_exit(IXEV_EXIT_FAILURE, "unable to create req mempool\n");

    ret = mempool_create(&nvme_req_buf_pool, &nvme_req_buf_datastore, MEMPOOL_SANITY_GLOBAL, 0);
    if (ret)
        ixev_exit(IXEV_EXIT_FAILURE, "unable to create nvme_req_buf mempool\n");

    flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    unsigned long next_job_time = rdtsc();
    spin_lock(&conn_id_bitmap_lock);
    conn_id = bitmap_first_zero(conn_id_bitmap, MAX_CONN_PER_CORE);
    assert(conn_id >= 0);
    bitmap_set(conn_id_bitmap, conn_id);
    spin_unlock(&conn_id_bitmap_lock);
    pp_conn_init(&conns[conn_id], conn_id);
    struct job_ctx *new_job = &conns[conn_id]->job;
    // new_job = malloc(sizeof(struct job_ctx));

    // = mempool_alloc(&job_pool);
    // concur_jobs[conn_id] = new_job;

    ret = JOB_RECV_AGAIN;
    int current_job_nr = 0;
    int acked_job_nr = 0;
    struct job_rep *jrep = malloc(sizeof *jrep);
    while (1) {
        if (!job_done && MAX_THROUGHPUT_PER_CORE > current_throughput) {  // do we need a job qdepth to avoid too many job requests?
            unsigned long now = rdtsc();
            if (now > next_job_time) {
                req_a_job(requester);
                sent_job_reqs++;
                job_done = (sent_job_reqs >= job_nr_per_core);
                next_job_time += cycles_between_jobs;
                printf("Requested a job, next job time is %ld.\n", next_job_time);
                if (job_done)
                    printf("Sent enough job reqs, stop requesting.\n");
            }
        }
        if (acked_job_reqs < sent_job_reqs) {
            current_job_nr = recv_job_meta(requester, jrep);
            if (current_job_nr > 0) {
                printf("Got a new new job rep.\n");
                acked_job_reqs++;
                acked_job_nr = 0;
            }
        }

        if (acked_job_nr < current_job_nr)
            ret = recv_a_job(requester, tid, new_job);

        if (ret == JOB_RECV_NEW) {
            printf("Got a new JOB_RECV_NEW.\n");
            current_throughput += new_job->IOPS_SLO * new_job->req_size;
            struct ip_tuple *it = ip_tuple[new_job->dst];
            conns[conn_id]->seq_count = new_job->start_addr;
            it->src_port = (new_job->id * MAX_NODE_NUM + new_job->part_id + 1024) % MAX_PORT_NUM;  // avoid using duplicate src port
            printf("dialing with ip tuple: dst_port-%d, src_port-%d.\n", it->dst_port, it->src_port);
            ixev_dial(conns[conn_id], it);
            acked_job_nr++;

            // Setup next connection
            spin_lock(&conn_id_bitmap_lock);
            conn_id = bitmap_first_zero(conn_id_bitmap, MAX_CONN_PER_CORE);
            if (conn_id >= 0) {
                bitmap_set(conn_id_bitmap, conn_id);
                pp_conn_init(&conns[conn_id], conn_id);
                new_job = &conns[conn_id]->job;
                // new_job = malloc(sizeof(struct job_ctx));
                printf("Now the pointer of new_job is %p.\n", new_job);
                // new_job = mempool_alloc(&job_pool);
                // concur_jobs[conn_id] = new_job;

            } else {
                printf("[%d] Resouces unavailable, job rejected.\n", tid);
                exit(-1);
            }
            spin_unlock(&conn_id_bitmap_lock);
        }
        int i;
        for (i = 0; i < MAX_CONN_PER_CORE; i++)
            if (bitmap_test(conn_id_bitmap, i)) {
                assert(conns[i]);
                if (conns[i]->alive)
                    req_generator(&conns[i]->ctx, req_qdepth);
            }
        ixev_wait();

        if (job_done)
            if (bitmap_first_zero(conn_id_bitmap, MAX_CONN_PER_CORE) < 0)
                break;
    }

    job_conn_destroy(tid);
}

int serverless_client_main(int argc, char *argv[]) {
    int ret, opt;
    int tids[64];
    unsigned int lcore_id;
    unsigned int pp_conn_pool_entries, concurrent_job_nr;

    while ((opt = getopt(argc, argv, "s:n:h")) != -1) {
        switch (opt) {
            case 's':
                ip = malloc(sizeof(char) * strlen(optarg));
                strcpy(ip, optarg);
                break;
            case 'n':
                total_job_nr = atoi(optarg);
                job_nr_per_core = total_job_nr / cpus_active;
                break;
            case 'h':
                fprintf(stderr,
                        "\nUsage: \n"
                        "sudo ./ix\n"
                        "to run ReFlex server, no parameters required\n"
                        "to run ReFlex client, set the following options:\n"
                        "-s  server IP address\n"
                        "-n  total number of jobs\n"
                        "-l  load to generate\n");
                exit(1);
            default:
                fprintf(stderr, "invalid command option\n");
                exit(1);
        }
    }

    run_time = cycles_per_us * 1000UL * 1000UL * MAX_RUNTIME_SECOND;

    pp_conn_pool_entries = cpus_active * 4096;
    pp_conn_pool_entries = ROUND_UP(pp_conn_pool_entries, MEMPOOL_DEFAULT_CHUNKSIZE);
    concurrent_job_nr = cpus_active * MAX_CONN_PER_CORE;

    ixev_init(&pp_conn_ops);

    // ret = mempool_create_datastore(&job_datastore, concurrent_job_nr,
    //                                sizeof(struct job_ctx), "job_ctx");
    // if (ret)
    //     ixev_exit(IXEV_EXIT_FAILURE, "unable to create datastore.\n");

    // ret = mempool_create_datastore(&job_metrics_datastore, concurrent_job_nr,
    //                                sizeof(struct job_metrics), "job_metrics");
    // if (ret)
    //     ixev_exit(IXEV_EXIT_FAILURE, "unable to create datastore.\n");

    ret = mempool_create_datastore(
        &pp_conn_datastore,
        pp_conn_pool_entries,
        sizeof(struct pp_conn),
        "pp_conn");
    if (ret)
        ixev_exit(IXEV_EXIT_FAILURE, "unable to create datastore.\n");

    ret = mempool_create_datastore(
        &nvme_req_datastore,
        OUTSTANDING_REQS * 2 * MAX_CONN_PER_CORE,
        sizeof(struct nvme_req),
        "nvme_req");
    if (ret)
        ixev_exit(IXEV_EXIT_FAILURE, "unable to create datastore.\n");

    ret = mempool_create_datastore(&nvme_req_buf_datastore,
                                   OUTSTANDING_REQS * 2,
                                   PAGE_SIZE, "nvme_req_buf");
    if (ret)
        ixev_exit(IXEV_EXIT_FAILURE, "unable to create datastore.\n");

    int i;
    for (i = 0; i < MAX_NODE_NUM * 2; i++) {
        ip_tuple[i] = malloc(sizeof(struct ip_tuple[i]));
        ip_tuple[i]->dst_port = dst_ports[i];
        printf("Seting node %d, dst is %d.\n", i, dst_ports[i]);
        // ip_tuple[i]->src_ip = ;
        if (parse_ip_addr(ip, &ip_tuple[i]->dst_ip)) {
            fprintf(stderr, "Bad IP address '%s'", ip);
            exit(1);
        }
    }
    free(ip);

    for (lcore_id = 1; lcore_id < cpus_active; lcore_id++) {
        printf("Launching new thread %ld\n", lcore_id);
        tids[lcore_id] = lcore_id;

        ret = rte_eal_remote_launch(recv_loop, &tids[lcore_id], lcore_id);

        if (ret) {
            printf("init: unable to start app.\n");
            exit(1);
        }
    }

    printf("Launching new thread 0\n");
    tids[0] = 0;

    recv_loop(&tids[0]);
    return 0;
}