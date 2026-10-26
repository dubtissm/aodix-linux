/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - routing work on several cpu cores
//
// The original routing loop processes the instances one after another: an instance works as soon as every instance with an
// audio or midi wire into it has worked ("bubble" passes over the instance slots). Right after its process call the outputs
// are mixed into the destination input pins and the midi output is appended to the destination event queues.
//
// Here the same processing order is determined first (by running exactly the same passes without processing). It defines
// the dependency graph: instances that do not depend on each other are processed in parallel by AdxDspPool. To get results
// that are bit-identical to the sequential original, every instance collects its own inputs right before it is processed
// (pull instead of push), in exactly the order in which the original mixed them: master input first (done before routing),
// then the sources in processing order, output by output, wire by wire. Midi output of a plugin is captured during its
// process call and delivered to the destinations in the same way.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AodixCore.h"
#include <ctime>

thread_local ADX_MIDI_CAPTURE* g_dsp_midi_capture=NULL;

// parallel processing has a price (waking threads, cache traffic): it is only used when the estimated gain is larger
#define DSP_PARALLEL_OVERHEAD_US	40.0f
#define DSP_PARALLEL_MIN_GAIN_US	20.0f

static inline double dsp_now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC,&ts);
	return ts.tv_sec*1000000.0+ts.tv_nsec*0.001;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int CAodixCore::dsp_auto_threads(void)
{
	// all cores (the jack thread counts as one), at most 16
	int const n=AdxDspPool::cpu_count();
	return n<16?n:16;
}

void CAodixCore::dsp_configure_threads(void)
{
	// cfg.dsp_threads: 0 = automatic, otherwise total number of dsp threads (1 = no parallel processing)
	int total=cfg.dsp_threads?cfg.dsp_threads:dsp_auto_threads();

	// environment override for testing
	if(const char* e=getenv("AODIX_DSP_THREADS"))
		total=atoi(e);

	if(total<1)
		total=1;
	if(total>64)
		total=64;

	dsp_pool.set_workers(total-1);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// append the captured midi output of instance 'src' to the event queue of instance 'dst' (as host_on_events did for one wire)
void CAodixCore::dsp_deliver_midi(int const src,int const dst,float const value)
{
	ADX_MIDI_CAPTURE const* const pc=&dsp_midi_capture[src];
	ADX_INSTANCE* const pi_mol=&instance[dst];

	for(int e=0;e<pc->num_events;e++)
	{
		if(pi_mol->midi_queue_size<MAX_BLOCK_EVENTS)
		{
			ADX_MIDI_EVENT* ptr_routed_event=&pi_mol->midi_event[pi_mol->midi_queue_size];
			*ptr_routed_event=pc->event[e];

			// scale velocity
			if((ptr_routed_event->midiData[0]&0xF0)==0x90)
				ptr_routed_event->midiData[2]=arg_tool_clipped_assign(int(float(ptr_routed_event->midiData[2])*value),1,127);

			pi_mol->midi_queue_size++;
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::dsp_node_work_cb(void* ctx,int node)
{
	((CAodixCore*)ctx)->dsp_node_work(node);
}

// collect the inputs of one instance and process it (runs on any dsp thread)
void CAodixCore::dsp_node_work(int const node)
{
	int const i=dsp_node_instance[node];
	ADX_INSTANCE* pi=&instance[i];
	double const t_start=dsp_now_us();

	// audio inputs from the source instances
	for(int m=dsp_node_mix_off[node];m<dsp_node_mix_off[node+1];m++)
	{
		ADX_DSP_MIX const& mx=dsp_node_mix[m];
		arg_dsp_gmix(mx.src,mx.dst,dsp_block_size,mx.gain);
	}

	// midi from the source instances: per source the captured process calls, per call the wires in their order
	for(int l=dsp_node_midi_off[node];l<dsp_node_midi_off[node+1];)
	{
		int const src=dsp_node_midi[l].src;
		int l_end=l;
		while(l_end<dsp_node_midi_off[node+1] && dsp_node_midi[l_end].src==src)
			l_end++;

		ADX_MIDI_CAPTURE const* const pc=&dsp_midi_capture[src];
		int seg_start=0;
		for(int sg=0;sg<pc->num_segments;sg++)
		{
			int const seg_end=pc->segment_end[sg];
			for(int k=l;k<l_end;k++)
			{
				ADX_INSTANCE* const pi_mol=pi;
				float const value=dsp_node_midi[k].value;
				for(int e=seg_start;e<seg_end;e++)
				{
					if(pi_mol->midi_queue_size<MAX_BLOCK_EVENTS)
					{
						ADX_MIDI_EVENT* ptr_routed_event=&pi_mol->midi_event[pi_mol->midi_queue_size];
						*ptr_routed_event=pc->event[e];
						if((ptr_routed_event->midiData[0]&0xF0)==0x90)
							ptr_routed_event->midiData[2]=arg_tool_clipped_assign(int(float(ptr_routed_event->midiData[2])*value),1,127);
						pi_mol->midi_queue_size++;
					}
				}
			}
			seg_start=seg_end;
		}

		l=l_end;
	}

	// sort events by delta (original code)
	if(pi->midi_queue_size>1)
	{
		bool all_sorted=false;

		while(!all_sorted)
		{
			all_sorted=true;

			for(int ev=0;ev<(pi->midi_queue_size-1);ev++)
			{
				if(pi->midi_event[ev].deltaFrames>pi->midi_event[ev+1].deltaFrames)
				{
					ADX_MIDI_EVENT tev=pi->midi_event[ev];
					pi->midi_event[ev]=pi->midi_event[ev+1];
					pi->midi_event[ev+1]=tev;
					all_sorted=false;
				}
			}
		}
	}

	int const num_block_events=pi->midi_queue_size;
	pi->midi_queue_size=0;

	// plugin process, midi output is captured for the destinations
	ADX_MIDI_CAPTURE* const pc=&dsp_midi_capture[i];
	pc->num_events=0;
	pc->num_segments=0;
	g_dsp_midi_capture=pc;
	pi->peffect->process(pi->pins,pi->pous,dsp_block_size,pi->midi_event,num_block_events);
	g_dsp_midi_capture=NULL;


	// instance process mute and thru (bypass)
	for(int o=0;o<pi->peffect->numOutputs;o++)
	{
		if(pi->process_mute)
			arg_dsp_zero(pi->pous[o],dsp_block_size);

		if(pi->process_thru)
		{
			if(o<pi->peffect->numInputs)
				arg_dsp_copy(pi->pins[o],pi->pous[o],dsp_block_size);
			else
				arg_dsp_zero(pi->pous[o],dsp_block_size);
		}
	}

	// smoothed processing time of this instance (decides about parallel processing)
	float const t=(float)(dsp_now_us()-t_start);
	dsp_instance_cost[i]+=(t-dsp_instance_cost[i])*0.05f;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// called by dsp_work() after the master input has been mixed into the instance input pins
void CAodixCore::dsp_route_and_process(int* hpc)
{
	// 1. processing order: the same bubble passes as the original routing loop, without processing
	bool i_worked[MAX_INSTANCES];
	for(int i=0;i<MAX_INSTANCES;i++)
	{
		i_worked[i]=false;
		dsp_instance_node[i]=-1;
	}
	dsp_num_nodes=0;

	bool all_worked=false;
	bool any_worked=true;

	while(!all_worked&&any_worked)
	{
		all_worked=true;
		any_worked=false;

		for(int i=0;i<MAX_INSTANCES;i++)
		{
			ADX_INSTANCE* pi=&instance[i];

			if(pi->peffect!=NULL && i_worked[i]==false)
			{
				bool can_work=true;

				for(int ii=0;ii<MAX_INSTANCES && can_work;ii++)
				{
					ADX_INSTANCE* pii=&instance[ii];

					if(pii->peffect!=NULL && ii!=i && i_worked[ii]==false)
					{
						for(int io=0;io<pii->peffect->numOutputs && can_work;io++)
						{
							ADX_PIN* pp=&pii->pout_pin[io];
							for(int w=0;w<pp->num_wires;w++)
								if(pp->pwire[w].instance_index==i)
									can_work=false;
						}

						for(int w=0;w<pii->mout_pin.num_wires;w++)
							if(pii->mout_pin.pwire[w].instance_index==i)
								can_work=false;
					}
				}

				if(can_work)
				{
					i_worked[i]=true;
					any_worked=true;
					dsp_instance_node[i]=dsp_num_nodes;
					dsp_node_instance[dsp_num_nodes++]=i;
				}
				else
				{
					all_worked=false;
				}
			}
		}
	}

	int const nn=dsp_num_nodes;

	// 2. dependencies and input lists (sources are visited in processing order, so the lists keep the original mix order)
	int mix_count[MAX_INSTANCES+1];
	int midi_count[MAX_INSTANCES+1];
	int succ_count[MAX_INSTANCES+1];
	int last_pred[MAX_INSTANCES];
	for(int n=0;n<=nn;n++)
	{
		mix_count[n]=midi_count[n]=succ_count[n]=0;
	}
	for(int n=0;n<nn;n++)
	{
		dsp_node_pending[n]=0;
		last_pred[n]=-1;
	}

	// count pass
	for(int sn=0;sn<nn;sn++)
	{
		ADX_INSTANCE* ps=&instance[dsp_node_instance[sn]];

		for(int o=0;o<ps->peffect->numOutputs;o++)
		{
			ADX_PIN* pp=&ps->pout_pin[o];
			for(int w=0;w<pp->num_wires;w++)
			{
				int const di=pp->pwire[w].instance_index;
				if(di<MASTER_INSTANCE && di!=dsp_node_instance[sn])
				{
					int const dn=dsp_instance_node[di];
					if(dn>=0)
					{
						mix_count[dn]++;
						if(last_pred[dn]!=sn)
						{
							last_pred[dn]=sn;
							dsp_node_pending[dn]++;
							succ_count[sn]++;
						}
					}
				}
			}
		}

		for(int w=0;w<ps->mout_pin.num_wires;w++)
		{
			int const di=ps->mout_pin.pwire[w].instance_index;
			if(di<MAX_INSTANCES && di!=dsp_node_instance[sn])
			{
				int const dn=dsp_instance_node[di];
				if(dn>=0)
				{
					midi_count[dn]++;
					if(last_pred[dn]!=sn)
					{
						last_pred[dn]=sn;
						dsp_node_pending[dn]++;
						succ_count[sn]++;
					}
				}
			}
		}
	}

	// offsets
	dsp_node_mix_off[0]=dsp_node_midi_off[0]=dsp_node_succ_off[0]=0;
	for(int n=0;n<nn;n++)
	{
		dsp_node_mix_off[n+1]=dsp_node_mix_off[n]+mix_count[n];
		dsp_node_midi_off[n+1]=dsp_node_midi_off[n]+midi_count[n];
		dsp_node_succ_off[n+1]=dsp_node_succ_off[n]+succ_count[n];
	}
	if((int)dsp_node_mix.size()<dsp_node_mix_off[nn])
		dsp_node_mix.resize(dsp_node_mix_off[nn]+256);
	if((int)dsp_node_midi.size()<dsp_node_midi_off[nn])
		dsp_node_midi.resize(dsp_node_midi_off[nn]+64);
	if((int)dsp_node_succ.size()<dsp_node_succ_off[nn])
		dsp_node_succ.resize(dsp_node_succ_off[nn]+256);

	// fill pass
	int mix_pos[MAX_INSTANCES];
	int midi_pos[MAX_INSTANCES];
	int succ_pos[MAX_INSTANCES];
	for(int n=0;n<nn;n++)
	{
		mix_pos[n]=dsp_node_mix_off[n];
		midi_pos[n]=dsp_node_midi_off[n];
		succ_pos[n]=dsp_node_succ_off[n];
		last_pred[n]=-1;
	}

	for(int sn=0;sn<nn;sn++)
	{
		int const si=dsp_node_instance[sn];
		ADX_INSTANCE* ps=&instance[si];

		for(int o=0;o<ps->peffect->numOutputs;o++)
		{
			ADX_PIN* pp=&ps->pout_pin[o];
			for(int w=0;w<pp->num_wires;w++)
			{
				ADX_WIRE* pw=&pp->pwire[w];
				int const di=pw->instance_index;
				if(di<MASTER_INSTANCE && di!=si)
				{
					int const dn=dsp_instance_node[di];
					if(dn>=0)
					{
						ADX_DSP_MIX& mx=dsp_node_mix[mix_pos[dn]++];
						mx.src=ps->pous[o];
						mx.dst=instance[di].pins[pw->pin_index];
						mx.gain=pw->value*pw->value;
						if(last_pred[dn]!=sn)
						{
							last_pred[dn]=sn;
							dsp_node_succ[succ_pos[sn]++]=dn;
						}
					}
				}
			}
		}

		for(int w=0;w<ps->mout_pin.num_wires;w++)
		{
			ADX_WIRE* pw=&ps->mout_pin.pwire[w];
			int const di=pw->instance_index;
			if(di<MAX_INSTANCES && di!=si)
			{
				int const dn=dsp_instance_node[di];
				if(dn>=0)
				{
					ADX_DSP_MIDI_LINK& lk=dsp_node_midi[midi_pos[dn]++];
					lk.src=si;
					lk.value=pw->value;
					if(last_pred[dn]!=sn)
					{
						last_pred[dn]=sn;
						dsp_node_succ[succ_pos[sn]++]=dn;
					}
				}
			}
		}
	}

	// 3. process (in parallel where the routing allows it and where it pays off)
	bool want_parallel=false;
	int const workers=dsp_pool.workers();
	if(workers>0 && nn>1)
	{
		// total work and longest dependency chain (critical path) from the measured instance costs
		float finish[MAX_INSTANCES];
		float start[MAX_INSTANCES];
		float total=0.0f;
		float critical=0.0f;
		for(int n=0;n<nn;n++)
			start[n]=0.0f;
		for(int n=0;n<nn;n++)
		{
			float const c=dsp_instance_cost[dsp_node_instance[n]];
			total+=c;
			finish[n]=start[n]+c;
			if(finish[n]>critical)
				critical=finish[n];
			for(int k=dsp_node_succ_off[n];k<dsp_node_succ_off[n+1];k++)
				if(finish[n]>start[dsp_node_succ[k]])
					start[dsp_node_succ[k]]=finish[n];
		}

		float const per_thread=total/float(workers+1);
		float const estimate=(critical>per_thread?critical:per_thread)+DSP_PARALLEL_OVERHEAD_US;
		want_parallel=(total-estimate)>DSP_PARALLEL_MIN_GAIN_US;
	}

	// switching between serial and parallel processing moves instances to other threads, so it only happens when the
	// other mode has been wanted for about half a second (plugins with own helper threads dislike thread changes)
	if(want_parallel!=dsp_parallel)
	{
		int const hold=(int)(cfg.audio_driver_sample_rate*0.5)/(dsp_block_size>0?dsp_block_size:256);
		if(++dsp_mode_blocks>=hold || workers==0 || nn<=1)
		{
			dsp_parallel=want_parallel;
			dsp_mode_blocks=0;
		}
	}
	else
		dsp_mode_blocks=0;
	if(workers==0 || nn<=1)
		dsp_parallel=false;

	// fixed threads: an instance keeps its thread; new instances go to the thread with the least work
	if(dsp_parallel)
	{
		if(dsp_slot_workers!=workers)
		{
			for(int i=0;i<MAX_INSTANCES;i++)
				dsp_instance_slot[i]=-1;
			dsp_slot_workers=workers;
		}
		float load[65];
		for(int t=0;t<=workers && t<65;t++)
			load[t]=0.0f;
		for(int n=0;n<nn;n++)
		{
			int const sl=dsp_instance_slot[dsp_node_instance[n]];
			if(sl>=0 && sl<=workers)
				load[sl]+=dsp_instance_cost[dsp_node_instance[n]];
		}
		for(int n=0;n<nn;n++)
		{
			int const si=dsp_node_instance[n];
			if(dsp_instance_slot[si]<0 || dsp_instance_slot[si]>workers)
			{
				int best=0;
				for(int t=1;t<=workers;t++)
					if(load[t]<load[best])
						best=t;
				dsp_instance_slot[si]=best;
				load[best]+=dsp_instance_cost[si]>1.0f?dsp_instance_cost[si]:1.0f;
			}
			dsp_node_slot[n]=dsp_instance_slot[si];
		}
	}

	dsp_pool.execute(nn,dsp_node_pending,dsp_node_succ_off,dsp_node_succ.data(),dsp_node_slot,dsp_node_work_cb,this,dsp_parallel);

	// 4. mix into the master outputs, in processing order (as the original did after each process call)
	for(int sn=0;sn<nn;sn++)
	{
		int const si=dsp_node_instance[sn];
		ADX_INSTANCE* ps=&instance[si];

		for(int o=0;o<ps->peffect->numOutputs;o++)
		{
			ADX_PIN* pp=&ps->pout_pin[o];
			for(int w=0;w<pp->num_wires;w++)
			{
				ADX_WIRE* pw=&pp->pwire[w];
				if(pw->instance_index==MASTER_INSTANCE)
				{
					arg_dsp_gmix(ps->pous[o],dsp_output_buffer[pw->pin_index],dsp_block_size,pw->value*pw->value);

					if(pw->pin_index>*hpc)
						*hpc=pw->pin_index;
				}
			}
		}

		// midi output to instances that do not work in this block (the original queued it there as well)
		ADX_MIDI_CAPTURE const* const pc=&dsp_midi_capture[si];
		if(pc->num_events>0)
		{
			for(int w=0;w<ps->mout_pin.num_wires;w++)
			{
				ADX_WIRE* pw=&ps->mout_pin.pwire[w];
				int const di=pw->instance_index;
				if(di<MAX_INSTANCES && (dsp_instance_node[di]<0 || di==si))
					dsp_deliver_midi(si,di,pw->value);
			}
		}
	}
}
