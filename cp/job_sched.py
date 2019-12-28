#!/usr/bin/env python

# Copyright (c) 2018-2019, University of California, Santa Cruz
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

import time
import zmq
import random
import copy

import ctypes

NUM_OF_WORKER = 64

APPROVED = 1
DECLINED = -1

class JobCtx(ctypes.Structure):
    _fields_ = [
        ('id', ctypes.c_ulong),
        ('part_id', ctypes.c_ulong),
        ('dst', ctypes.c_uint),
        # ('job_dst', ctypes.c_long), # 0xffffffff, not a good idea cos only support even job distribution
        ('latency_us_SLO', ctypes.c_uint),
        ('IOPS_SLO', ctypes.c_ulong), # splittable
        ('rw_ratio_SLO', ctypes.c_uint),
        ('req_size', ctypes.c_uint),
        ('replicas', ctypes.c_uint),
        ('duration', ctypes.c_ulong),
        ('start_addr', ctypes.c_ulong),
        # ('end_addr', ctypes.c_ulong),
        ('capacity', ctypes.c_ulong),
        ('MAX_IOPS', ctypes.c_ulong),
        ('MIN_IOPS', ctypes.c_ulong),
        ('delay_us', ctypes.c_ulong), # delay at scheduler or worker?
        ('unifom', ctypes.c_bool),
        ('sequential', ctypes.c_bool),
        ('read_once', ctypes.c_bool),
    ]

class JobReq(ctypes.Structure):
    _fields_ = [
        ('tid', ctypes.c_uint),
        ('avail_nodes', ctypes.c_long),
    ]
    
    def receiveSome(self, bytes):
        """ translate byte stream to this ctype
        """
        fit = min(len(bytes), ctypes.sizeof(self))
        ctypes.memmove(ctypes.addressof(self), bytes, fit)

class JobRep(ctypes.Structure):
    _fields_ = [
        ('command', ctypes.c_int),
        ('work_nodes', ctypes.c_long),
    ]

# def init_server(context, fair=False, sim=True):
#     socket = None
    
#     if fair:
#         socket = context.socket(zmq.PUSH) # not tested
#     else:
#         socket = context.socket(zmq.ROUTER)
#     if sim:
#         socket.bind("ipc:///tmp/job")
#     else:
#         socket.bind("tcp://*:5555")
#     return socket

def dummy_generate_jobs(job_nr):
    dummy_job_queue = []
    for i in range(job_nr):
        new_job = JobCtx()
        new_job.id = i
        new_job.part_id = 0
        new_job.req_size = random.randint(2, 32);
        new_job.latency_us_SLO = random.randint(100, 1000)
        new_job.IOPS_SLO = random.randint(500, 100000)
        new_job.capacity = random.randint(1024, 64*1024*1024) # from 1 MB to 64 GB
        new_job.unifom = True
        new_job.sequential = random.choice([True, False])
        if(i == 0):
            new_job.start_addr = 0;
        else:
            new_job.start_addr = dummy_job_queue[i-1].start_addr + dummy_job_queue[i-1].capacity
        new_job.read_once = False
        dummy_job_queue += [new_job]
    return dummy_job_queue

def job_assign(sock, id, meta, sub_jobs):
    """ send each job to its worker
    """
    job_nr = len(sub_jobs)
    # sock.send_multipart([id]+[bytes(sub_jobs[i]) for i in range(job_nr)])
    for sub_job in sub_jobs:
        print('sub job:','id', sub_job.id, 'part:', sub_job.part_id, 'job_dst', sub_job.dst, 'IOPS', sub_job.IOPS_SLO)
    # sock.send_multipart([id]+[bytes(sub_jobs[i]) for i in range(job_nr)]+[b'Done'])
    sock.send_multipart([id, bytes(meta)]+[bytes(sub_jobs[i]) for i in range(job_nr)])
    # sock.send_multipart([id, b'', b'TEST MSG'])
    # sock.send_multipart([id, bytes(sub_jobs[0])])
    # test_job = JobCtx()
    # sock.send_multipart([id, bytes(test_job)])

class JobGenerator():
    def __init__(self):
        self.ns_size = None # Read the capacity of our stoage

    def gen_new_job(self):
        new_job = JobCtx()

class JobQueue():
    def __init__(self, worker_id):
        # each job queue is binded with a worker (we assume each job has to reside in its compute node)
        self.id = worker_id
        self.dummy_job_queue = []
        self.generate_dummy(50)

    def pop(self):
        if not self.dummy_job_queue:
            return None
        return self.dummy_job_queue.pop()

    def generate_dummy(self, job_nr):
        for i in range(job_nr):
            new_job = JobCtx()
            new_job.id = i*(self.id+1)# different queues host different id
            new_job.req_size = random.randint(2, 16)
            new_job.latency_us_SLO = random.randint(100, 500)
            new_job.IOPS_SLO = random.randint(500, 100000)
            new_job.unifom = True
            new_job.sequential = random.choice([True, False])
            new_job.read_once = False
            self.dummy_job_queue += [new_job]
        return self.dummy_job_queue

    def next_jobs(self, job_nr):
        return self.dummy_job_queue[-job_nr]

class StorageNode():
    pass

class Scheduler():
    sched_dict = {
        "FCFS": "sched_fcfs",
        "SJF": "sched_sjf",
        "OPT": "sched_opt",
        "NEW": "sched_new",
    }

    def __init__(self, queues, avail_nodes, policy="FCFS", depth=4):
        self.sched = getattr(self, Scheduler.sched_dict[policy])
        self.depth = 4
        self.avail_nodes = avail_nodes
        self.split_weight = []
        self.queues = queues

    def node_join(self):
        pass

    def node_leave(self):
        pass

    def sched_fcfs(self, qid):
        """ (1) evenly split the first job to serveral sub-jobs
            (2) return the list of storage nodes
        """
        print("I am scheduling with fcfs")
        job = self.queues[qid].pop()
        return self.split_job(job)

    def sched_fcfs_no_split(self, qid):
        """
        """
        print("I am scheduling with fcfs")

    def sched_sjf(self, qid):
        """ (1) sort the first [depth] jobs by their 
            (2) evely split the first job to serveral sub-jobs
            (3) fix the starvation problem
            (4) return the list of storage nodes 
        """
        pass

    def sched_opt(self, qid):
        """ return the best sheduling results by experiments
        """
        pass
    
    def sched_new(self, avail_nodes):
        """ ()
            () change split_weight every time
        """
        pass

    def split_job(self, job):
        """ Split one job into serveral [evenly] sub-jobs
        """
        nodes_nr = len(self.avail_nodes)
        sub_jobs = [copy.deepcopy(job) for i in range(nodes_nr)]
        for i, sub_job in enumerate(sub_jobs):
            sub_job.part_id = i
            sub_job.IOPS_SLO = int(job.IOPS_SLO / nodes_nr)
            sub_job.capacity = int(job.capacity / nodes_nr)
            sub_job.start_addr = sub_job.start_addr + i * sub_job.capacity
            sub_job.dst = self.avail_nodes[i]
            # print('sub job:','id', sub_job.id, 'part:', sub_job.part_id, 'job_dst', sub_job.job_dst, 'IOPS', sub_job.IOPS_SLO)
        return sub_jobs

if __name__ == '__main__':
    queues = [JobQueue(i) for i in range(NUM_OF_WORKER)]
    avail_qid = 0
    context = zmq.Context()
    backend = context.socket(zmq.ROUTER)
    backend.bind("ipc:///tmp/job_backend")
    avail_nodes = [0, 1, 2, 3]
    poller = zmq.Poller()
    # Only poll for requests from backend until workers are available
    poller.register(backend, zmq.POLLIN)
    scheduler = Scheduler(queues, avail_nodes)
    workers = {}

    while True:
        #  Wait for next job request from workers
        sockets = dict(poller.poll())
        if backend in sockets:
            cur_qid = -1
            worker_id, empty, request = backend.recv_multipart()
            print("Worker-%s" % worker_id)
            # print("Received request: %s" % request)
            if worker_id not in workers:
                workers[worker_id] = avail_qid
                avail_qid += 1
                assert avail_qid <= NUM_OF_WORKER
            cur_qid = workers[worker_id]
            new_job_req = JobReq()
            new_job_req.receiveSome(request)
            # print("Avail nodes is: %s" % new_job_req.avail_nodes)
            job_rep = JobRep()
            job_rep.command = APPROVED
            # jon_rep.work_nodes = int("0x0000ff", 0)
            job_rep.work_nodes = new_job_req.avail_nodes
            # qid = new_job_req.tid
            print("The qid is", cur_qid)
            next_jobs = scheduler.sched(cur_qid)
            print("The job id is", next_jobs[0].id)
            #  Send reply back to client
            job_assign(backend, worker_id, job_rep, next_jobs)

    backend.close()
    context.term()