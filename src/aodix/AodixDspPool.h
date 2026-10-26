/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - parallel dsp processing
//
// A small real-time task graph executor: per audio block the caller describes the nodes (plugin instances) that have to be
// processed and their dependencies (routing wires). Nodes whose sources are finished run in parallel on the worker threads,
// the calling thread (the jack process thread) takes part in the work. No allocations and no blocking system calls happen
// on the calling thread; idle workers sleep on a semaphore and are woken when work becomes ready.
//
// Every node has a fixed thread ("slot": 0 = calling thread, 1..n = workers): a plugin instance is always processed on
// the same thread. Plugins with their own helper threads (e.g. u-he "Multicore") can hang when the thread that calls
// process() changes from block to block.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <atomic>
#include <thread>
#include <vector>
#include <semaphore.h>
#include <pthread.h>

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

	// real-time scheduling through the audio system (jack_acquire_real_time_scheduling: under PipeWire this goes through
	// RTKit, so it works without rtprio limits). returns 0 on success. without it pthread_setschedparam is used.
	typedef int (*rt_fn)(pthread_t thread,int priority);
	void	set_realtime_function(rt_fn fn) { rt_acquire=fn; }

	// process a graph of num_nodes nodes. pending[n] = number of predecessors of node n,
	// succ[succ_off[n]..succ_off[n+1]) = successors of node n, slot[n] = thread of node n (0 = calling thread,
	// 1..workers() = worker). returns when all nodes are processed.
	// parallel=false processes the nodes in their numbering order on the calling thread (must be a valid order).
	void	execute(int num_nodes,const int* pending,const int* succ_off,const int* succ,const int* slot,work_fn fn,void* ctx,bool parallel=true);

	// wake the sleeping workers ahead of a parallel job (they look for work for a short while)
	void	prewake(void);

	// number of logical cpus
	static int cpu_count(void);

private:
	struct Worker
	{
		sem_t				wake;
		std::atomic<bool>	sleeping;
		int*				queue;			// ready nodes of this slot (each node is pushed exactly once per job)
		int					head,tail;
	};

	void	worker_main(int slot);
	bool	pop(int slot,int* node);
	void	push(int node);
	void	run_node(int node);
	void	drain(int slot,bool may_sleep);
	void	apply_thread_settings(void);
	void	lock(void);
	void	unlock(void);
	void	grow(int num_nodes);

	std::vector<std::thread>	threads;
	Worker*						slots;			// [0] = calling thread, [1..] = workers
	int							num_slots;
	std::atomic<bool>			quit;

	// current job
	std::atomic<int>*			node_pending;
	int							node_capacity;
	const int*					job_succ_off;
	const int*					job_succ;
	const int*					job_slot;
	work_fn						job_fn;
	void*						job_ctx;
	std::atomic<int>			remaining;

	// protects the ready queues
	std::atomic_flag			queue_lock;

	// thread settings copied from the calling thread (denormal handling) and real-time priority
	std::atomic<unsigned int>	job_mxcsr;
	std::atomic<int>			rt_priority;
	rt_fn						rt_acquire;
};
