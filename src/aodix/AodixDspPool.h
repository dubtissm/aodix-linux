/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - parallel dsp processing
//
// A small real-time task graph executor: per audio block the caller describes the nodes (plugin instances) that have to be
// processed and their dependencies (routing wires). Nodes whose sources are finished run in parallel on the worker threads,
// the calling thread (the jack process thread) takes part in the work. No allocations and no blocking system calls happen
// on the calling thread; idle workers sleep on a semaphore and are woken when work becomes ready.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <atomic>
#include <thread>
#include <vector>
#include <semaphore.h>

class AdxDspPool
{
public:
	typedef void (*work_fn)(void* ctx,int node);

	AdxDspPool();
	~AdxDspPool();

	// number of additional worker threads (0 = everything runs on the calling thread). not real-time safe.
	void	set_workers(int num_workers);
	int		workers(void) const { return (int)threads.size(); }

	// real-time priority for the workers (SCHED_FIFO, <=0 = normal scheduling). applied when the workers wake up.
	void	set_realtime_priority(int priority);

	// process a graph of num_nodes nodes. pending[n] = number of predecessors of node n,
	// succ[succ_off[n]..succ_off[n+1]) = successors of node n. returns when all nodes are processed.
	// parallel=false processes the nodes in their numbering order on the calling thread (must be a valid order).
	void	execute(int num_nodes,const int* pending,const int* succ_off,const int* succ,work_fn fn,void* ctx,bool parallel=true);

	// wake the sleeping workers ahead of a parallel job (they look for work for a short while)
	void	prewake(void);

	// number of logical cpus
	static int cpu_count(void);

private:
	void	worker_main(void);
	bool	pop(int* node);
	void	push(int node);
	void	run_node(int node);
	void	drain(bool may_sleep);
	void	apply_thread_settings(void);
	void	lock(void);
	void	unlock(void);

	std::vector<std::thread>	threads;
	std::atomic<bool>			quit;
	sem_t						wake;
	std::atomic<int>			sleepers;

	// current job
	std::atomic<int>*			node_pending;
	int							node_capacity;
	const int*					job_succ_off;
	const int*					job_succ;
	work_fn						job_fn;
	void*						job_ctx;
	std::atomic<int>			remaining;

	// ready queue (spin lock protected, each node is pushed exactly once per job)
	std::atomic_flag			queue_lock;
	int*						queue;
	int							queue_head;
	int							queue_tail;

	// thread settings copied from the calling thread (denormal handling) and real-time priority
	std::atomic<unsigned int>	job_mxcsr;
	std::atomic<int>			rt_priority;
};
