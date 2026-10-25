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
	sleepers=0;
	sem_init(&wake,0,0);
	node_capacity=0;
	node_pending=nullptr;
	queue=nullptr;
	job_succ_off=nullptr;
	job_succ=nullptr;
	job_fn=nullptr;
	job_ctx=nullptr;
	remaining=0;
	queue_lock.clear();
	queue_head=queue_tail=0;
	job_mxcsr=get_fp_mode();
	rt_priority=0;
}

AdxDspPool::~AdxDspPool()
{
	set_workers(0);
	sem_destroy(&wake);
	delete[] node_pending;
	delete[] queue;
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
	for(size_t i=0;i<threads.size();i++)
		sem_post(&wake);
	for(std::thread& t:threads)
		t.join();
	threads.clear();
	quit=false;

	// drain stale wake ups
	while(sem_trywait(&wake)==0) {}

	for(int i=0;i<num_workers;i++)
		threads.emplace_back(&AdxDspPool::worker_main,this);
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
	lock();
	queue[queue_tail++]=node;
	unlock();

	// wake a sleeping worker
	if(sleepers.load(std::memory_order_acquire)>0)
		sem_post(&wake);
}

bool AdxDspPool::pop(int* node)
{
	bool ok=false;
	lock();
	if(queue_head<queue_tail)
	{
		*node=queue[queue_head++];
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

// work while there is something left in the current job
void AdxDspPool::drain(bool may_sleep)
{
	int spins=0;
	long long idle_since=0;
	while(remaining.load(std::memory_order_acquire)>0)
	{
		int node;
		if(pop(&node))
		{
			run_node(node);
			spins=0;
			idle_since=0;
			continue;
		}

		// nothing ready right now: another thread is still working on a predecessor
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
		sched_param sp;
		memset(&sp,0,sizeof(sp));
		sp.sched_priority=prio>0?prio:0;
		if(pthread_setschedparam(pthread_self(),prio>0?SCHED_FIFO:SCHED_OTHER,&sp)!=0 && prio>0)
		{
			static std::atomic<bool> warned(false);
			if(!warned.exchange(true))
				fprintf(stderr,"aodix: dsp worker threads could not get real-time priority (check the rtprio limits of your user)\n");
		}
	}
}

void AdxDspPool::worker_main(void)
{
	while(!quit.load(std::memory_order_acquire))
	{
		// sleep until there is work
		sleepers.fetch_add(1,std::memory_order_acq_rel);
		bool skip_wait=false;
		lock();
		skip_wait=queue_head<queue_tail;
		unlock();
		if(!skip_wait)
			while(sem_wait(&wake)!=0) {}
		sleepers.fetch_sub(1,std::memory_order_acq_rel);

		if(quit.load(std::memory_order_acquire))
			break;

		apply_thread_settings();
		drain(true);

		// keep looking for the next job a little while (offline rendering, short gaps between blocks)
		long long const t0=now_us();
		int spins=0;
		while(!quit.load(std::memory_order_relaxed))
		{
			if(remaining.load(std::memory_order_acquire)>0)
			{
				apply_thread_settings();
				drain(true);
				spins=0;
				continue;
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
	int n=sleepers.load(std::memory_order_acquire);
	if(n>(int)threads.size())
		n=(int)threads.size();
	for(int i=0;i<n;i++)
		sem_post(&wake);
}

void AdxDspPool::execute(int num_nodes,const int* pending,const int* succ_off,const int* succ,work_fn fn,void* ctx,bool parallel)
{
	if(num_nodes<=0)
		return;

	// no workers: process in order on the calling thread (the node numbering is a valid processing order)
	if(!parallel || threads.empty() || num_nodes==1)
	{
		for(int n=0;n<num_nodes;n++)
			fn(ctx,n);
		return;
	}

	// grow buffers (only happens when more instances are used than ever before)
	if(num_nodes>node_capacity)
	{
		// under the queue lock: an idle worker may still be looking into the queue of the previous job
		lock();
		delete[] node_pending;
		delete[] queue;
		node_capacity=num_nodes+64;
		node_pending=new std::atomic<int>[node_capacity];
		queue=new int[node_capacity];
		queue_head=queue_tail=0;
		unlock();
	}

	// set up the job
	job_succ_off=succ_off;
	job_succ=succ;
	job_fn=fn;
	job_ctx=ctx;
	job_mxcsr.store(get_fp_mode(),std::memory_order_relaxed);
	int num_ready=0;
	for(int n=0;n<num_nodes;n++)
	{
		node_pending[n].store(pending[n],std::memory_order_relaxed);
		if(pending[n]==0)
			num_ready++;
	}
	remaining.store(num_nodes,std::memory_order_release);

	lock();
	queue_head=queue_tail=0;
	for(int n=0;n<num_nodes;n++)
		if(pending[n]==0)
			queue[queue_tail++]=n;
	unlock();

	// wake workers for the nodes that are ready now (the calling thread takes one itself)
	int wake_count=num_ready-1;
	int const sl=sleepers.load(std::memory_order_acquire);
	if(wake_count>sl)
		wake_count=sl;
	for(int i=0;i<wake_count;i++)
		sem_post(&wake);

	// take part in the work until everything is done
	drain(false);
}
