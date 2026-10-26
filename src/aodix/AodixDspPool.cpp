/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - parallel dsp processing (see AodixDspPool.h)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AodixDspPool.h"

#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <ctime>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
static inline void cpu_relax(void) { _mm_pause(); }
static inline unsigned int get_fp_mode(void) { return _mm_getcsr(); }
static inline void set_fp_mode(unsigned int m) { _mm_setcsr(m); }
#elif defined(__aarch64__)
static inline void cpu_relax(void) { asm volatile("yield"); }
static inline unsigned int get_fp_mode(void) { unsigned long v; asm volatile("mrs %0, fpcr" : "=r"(v)); return (unsigned int)v; }
static inline void set_fp_mode(unsigned int m) { unsigned long v=m; asm volatile("msr fpcr, %0" :: "r"(v)); }
#else
static inline void cpu_relax(void) {}
static inline unsigned int get_fp_mode(void) { return 0; }
static inline void set_fp_mode(unsigned int) {}
#endif

// how long an idle worker keeps looking for new work before it goes to sleep (microseconds)
#define SPIN_BEFORE_SLEEP_US 300

static inline long long now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC,&ts);
	return (long long)ts.tv_sec*1000000ll+ts.tv_nsec/1000;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
AdxDspPool::AdxDspPool()
{
	quit=false;
	num_slots=1;
	slots=new Worker[1];
	sem_init(&slots[0].wake,0,0);
	slots[0].sleeping=false;
	slots[0].queue=nullptr;
	slots[0].head=slots[0].tail=0;
	node_capacity=0;
	node_pending=nullptr;
	job_succ_off=nullptr;
	job_succ=nullptr;
	job_slot=nullptr;
	job_fn=nullptr;
	job_ctx=nullptr;
	remaining=0;
	queue_lock.clear();
	job_mxcsr=get_fp_mode();
	rt_priority=0;
	rt_acquire=nullptr;
}

AdxDspPool::~AdxDspPool()
{
	set_workers(0);
	for(int i=0;i<num_slots;i++)
	{
		sem_destroy(&slots[i].wake);
		delete[] slots[i].queue;
	}
	delete[] slots;
	delete[] node_pending;
}

int AdxDspPool::cpu_count(void)
{
	unsigned int const n=std::thread::hardware_concurrency();
	return n>0?(int)n:1;
}

void AdxDspPool::set_workers(int num_workers)
{
	if(num_workers<0)
		num_workers=0;
	if(num_workers==(int)threads.size())
		return;

	// stop current workers
	quit=true;
	for(int i=1;i<num_slots;i++)
		sem_post(&slots[i].wake);
	for(std::thread& t:threads)
		t.join();
	threads.clear();
	quit=false;

	// new slots (queues sized for the current node capacity)
	for(int i=0;i<num_slots;i++)
	{
		sem_destroy(&slots[i].wake);
		delete[] slots[i].queue;
	}
	delete[] slots;
	num_slots=num_workers+1;
	slots=new Worker[num_slots];
	for(int i=0;i<num_slots;i++)
	{
		sem_init(&slots[i].wake,0,0);
		slots[i].sleeping=false;
		slots[i].queue=node_capacity>0?new int[node_capacity]:nullptr;
		slots[i].head=slots[i].tail=0;
	}

	for(int i=1;i<num_slots;i++)
		threads.emplace_back(&AdxDspPool::worker_main,this,i);
}

void AdxDspPool::set_realtime_priority(int priority)
{
	rt_priority=priority;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void AdxDspPool::lock(void)
{
	while(queue_lock.test_and_set(std::memory_order_acquire))
		cpu_relax();
}

void AdxDspPool::unlock(void)
{
	queue_lock.clear(std::memory_order_release);
}

void AdxDspPool::push(int node)
{
	int const sl=job_slot[node];
	Worker& w=slots[sl];
	lock();
	w.queue[w.tail++]=node;
	unlock();

	// wake the worker of this slot if it sleeps
	if(sl>0 && w.sleeping.load(std::memory_order_seq_cst))
		sem_post(&w.wake);
}

bool AdxDspPool::pop(int slot,int* node)
{
	Worker& w=slots[slot];
	bool ok=false;
	lock();
	if(w.head<w.tail)
	{
		*node=w.queue[w.head++];
		ok=true;
	}
	unlock();
	return ok;
}

void AdxDspPool::run_node(int node)
{
	job_fn(job_ctx,node);

	// release the successors
	for(int s=job_succ_off[node];s<job_succ_off[node+1];s++)
	{
		int const n=job_succ[s];
		if(node_pending[n].fetch_sub(1,std::memory_order_acq_rel)==1)
			push(n);
	}

	remaining.fetch_sub(1,std::memory_order_acq_rel);
}

// work on the nodes of this slot while the current job is not finished
void AdxDspPool::drain(int slot,bool may_sleep)
{
	int spins=0;
	long long idle_since=0;
	while(remaining.load(std::memory_order_acquire)>0)
	{
		int node;
		if(pop(slot,&node))
		{
			run_node(node);
			spins=0;
			idle_since=0;
			continue;
		}

		// nothing ready for this slot right now: another thread is still working on a predecessor
		cpu_relax();
		if(may_sleep && (++spins&63)==0)
		{
			long long const t=now_us();
			if(!idle_since)
				idle_since=t;
			else if(t-idle_since>SPIN_BEFORE_SLEEP_US)
				return;
		}
	}
}

void AdxDspPool::apply_thread_settings(void)
{
	// same floating point mode (flush denormals to zero etc.) as the jack thread: identical results and no denormal slowdowns
	unsigned int const m=job_mxcsr.load(std::memory_order_relaxed);
	if(get_fp_mode()!=m)
		set_fp_mode(m);

	// real-time priority
	static thread_local int applied_priority=0;
	int const prio=rt_priority.load(std::memory_order_relaxed);
	if(prio!=applied_priority)
	{
		applied_priority=prio;
		bool ok=false;
		if(prio>0 && rt_acquire)
		{
			// through the audio system (pipewire: rtkit, which may limit the priority)
			int const tries[]={prio,20,10,5};
			for(int t:tries)
				if(t<=prio && rt_acquire(pthread_self(),t)==0)
				{
					ok=true;
					break;
				}
		}
		if(!ok)
		{
			sched_param sp;
			memset(&sp,0,sizeof(sp));
			sp.sched_priority=prio>0?prio:0;
			ok=pthread_setschedparam(pthread_self(),prio>0?SCHED_FIFO:SCHED_OTHER,&sp)==0;
		}
		if(!ok && prio>0)
		{
			static std::atomic<bool> warned(false);
			if(!warned.exchange(true))
				fprintf(stderr,"aodix: dsp worker threads could not get real-time priority (check the rtprio limits of your user)\n");
		}
	}
}

void AdxDspPool::worker_main(int slot)
{
	Worker& w=slots[slot];
	while(!quit.load(std::memory_order_acquire))
	{
		// sleep until a node of this slot is ready (or a prewake)
		w.sleeping.store(true,std::memory_order_seq_cst);
		lock();
		bool const skip_wait=w.head<w.tail;
		unlock();
		if(!skip_wait)
			while(sem_wait(&w.wake)!=0) {}
		w.sleeping.store(false,std::memory_order_seq_cst);

		if(quit.load(std::memory_order_acquire))
			break;

		apply_thread_settings();
		drain(slot,true);

		// keep looking for nodes of the next job a little while (offline rendering, short gaps between blocks)
		long long const t0=now_us();
		int spins=0;
		while(!quit.load(std::memory_order_relaxed))
		{
			if(remaining.load(std::memory_order_acquire)>0)
			{
				lock();
				bool const mine=w.head<w.tail;
				unlock();
				if(mine)
				{
					apply_thread_settings();
					drain(slot,true);
					spins=0;
					continue;
				}
			}
			cpu_relax();
			if((++spins&63)==0 && now_us()-t0>SPIN_BEFORE_SLEEP_US)
				break;
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void AdxDspPool::prewake(void)
{
	for(int i=1;i<num_slots;i++)
		if(slots[i].sleeping.load(std::memory_order_acquire))
			sem_post(&slots[i].wake);
}

void AdxDspPool::grow(int num_nodes)
{
	// under the queue lock: an idle worker may still be looking into its queue
	lock();
	delete[] node_pending;
	node_capacity=num_nodes+64;
	node_pending=new std::atomic<int>[node_capacity];
	for(int i=0;i<num_slots;i++)
	{
		delete[] slots[i].queue;
		slots[i].queue=new int[node_capacity];
		slots[i].head=slots[i].tail=0;
	}
	unlock();
}

void AdxDspPool::execute(int num_nodes,const int* pending,const int* succ_off,const int* succ,const int* slot,work_fn fn,void* ctx,bool parallel)
{
	if(num_nodes<=0)
		return;

	// no workers: process in order on the calling thread (the node numbering is a valid processing order)
	if(!parallel || threads.empty() || num_nodes==1 || !slot)
	{
		for(int n=0;n<num_nodes;n++)
			fn(ctx,n);
		return;
	}

	// grow buffers (only happens when more instances are used than ever before)
	if(num_nodes>node_capacity)
		grow(num_nodes);

	// set up the job
	job_succ_off=succ_off;
	job_succ=succ;
	job_slot=slot;
	job_fn=fn;
	job_ctx=ctx;
	job_mxcsr.store(get_fp_mode(),std::memory_order_relaxed);
	for(int n=0;n<num_nodes;n++)
		node_pending[n].store(pending[n],std::memory_order_relaxed);
	remaining.store(num_nodes,std::memory_order_release);

	bool ready[65];
	lock();
	for(int i=0;i<num_slots;i++)
		slots[i].head=slots[i].tail=0;
	for(int n=0;n<num_nodes;n++)
		if(pending[n]==0)
		{
			Worker& w=slots[slot[n]];
			w.queue[w.tail++]=n;
		}
	for(int i=0;i<num_slots && i<65;i++)
		ready[i]=slots[i].head<slots[i].tail;
	unlock();

	// wake the workers that have nodes ready now
	for(int i=1;i<num_slots && i<65;i++)
		if(ready[i] && slots[i].sleeping.load(std::memory_order_seq_cst))
			sem_post(&slots[i].wake);

	// take part in the work until everything is done
	drain(0,false);
}
