// ADX Test LV2 Synth: a small sine synth exercising the LV2 host features Aodix provides.
// MIDI atom input, time:Position, control ports (linear, logarithmic, toggled, integer), a control output, worker,
// state interface and a MIDI echo (notes +12) on the atom output. Writes a trace to $ADXTEST_LOG if set.
// MIT licence, part of Aodix Linux.
#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <lv2/state/state.h>
#include <lv2/time/time.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADX_URI "urn:aodix:adxtest-lv2"

static void tlog(const char* fmt,...)
{
	const char* p=getenv("ADXTEST_LOG"); if(!p) return;
	FILE* f=fopen(p,"a"); if(!f) return;
	va_list ap; va_start(ap,fmt); fprintf(f,"[lv2] "); vfprintf(f,fmt,ap); va_end(ap); fputc('\n',f); fclose(f);
}

enum { P_GAIN, P_CUTOFF, P_MUTE, P_WAVE, P_LEVEL, P_EVENTS, P_NOTIFY, P_OUTL, P_OUTR };

typedef struct
{
	const float* gain; const float* cutoff; const float* mute; const float* wave; float* level;
	const LV2_Atom_Sequence* events; LV2_Atom_Sequence* notify; float* out[2];
	LV2_URID_Map* map; LV2_Worker_Schedule* sched; LV2_Atom_Forge forge;
	LV2_URID midi_Event, time_Position, time_bpm, time_speed, time_barBeat, atom_Float, atom_Long, atom_Int, atom_Object, state_count;
	double rate, phase, lp; int note, on; float env; int count; int responses; float last_gain; float last_cutoff; float last_wave; float last_mute;
} Synth;

static LV2_Handle instantiate(const LV2_Descriptor* d,double rate,const char* bundle,const LV2_Feature* const* f)
{
	(void)d; (void)bundle;
	Synth* s=(Synth*)calloc(1,sizeof(Synth));
	for(int i=0;f[i];i++)
	{
		if(!strcmp(f[i]->URI,LV2_URID__map)) s->map=(LV2_URID_Map*)f[i]->data;
		if(!strcmp(f[i]->URI,LV2_WORKER__schedule)) s->sched=(LV2_Worker_Schedule*)f[i]->data;
	}
	if(!s->map || !s->sched) { free(s); return NULL; }
	LV2_URID_Map* m=s->map;
	s->midi_Event=m->map(m->handle,LV2_MIDI__MidiEvent);
	s->time_Position=m->map(m->handle,LV2_TIME__Position);
	s->time_bpm=m->map(m->handle,LV2_TIME__beatsPerMinute);
	s->time_speed=m->map(m->handle,LV2_TIME__speed);
	s->time_barBeat=m->map(m->handle,LV2_TIME__barBeat);
	s->atom_Float=m->map(m->handle,LV2_ATOM__Float);
	s->atom_Long=m->map(m->handle,LV2_ATOM__Long);
	s->atom_Int=m->map(m->handle,LV2_ATOM__Int);
	s->atom_Object=m->map(m->handle,LV2_ATOM__Object);
	s->state_count=m->map(m->handle,ADX_URI "#count");
	lv2_atom_forge_init(&s->forge,m);
	s->rate=rate; s->note=60; s->last_gain=s->last_cutoff=s->last_wave=s->last_mute=-1;
	tlog("instantiate rate=%.0f",rate);
	return s;
}

static void connect_port(LV2_Handle h,uint32_t port,void* data)
{
	Synth* s=(Synth*)h;
	switch(port)
	{
	case P_GAIN: s->gain=(const float*)data; break;
	case P_CUTOFF: s->cutoff=(const float*)data; break;
	case P_MUTE: s->mute=(const float*)data; break;
	case P_WAVE: s->wave=(const float*)data; break;
	case P_LEVEL: s->level=(float*)data; break;
	case P_EVENTS: s->events=(const LV2_Atom_Sequence*)data; break;
	case P_NOTIFY: s->notify=(LV2_Atom_Sequence*)data; break;
	case P_OUTL: s->out[0]=(float*)data; break;
	case P_OUTR: s->out[1]=(float*)data; break;
	}
}

static void activate(LV2_Handle h) { tlog("activate"); ((Synth*)h)->phase=0; }
static void deactivate(LV2_Handle h) { (void)h; tlog("deactivate"); }

static void render(Synth* s,uint32_t a,uint32_t b)
{
	double const f=440.0*pow(2.0,(s->note-69)/12.0);
	double const k=1.0-exp(-6.283185307*(*s->cutoff)/s->rate);
	int const w=(int)*s->wave;
	for(uint32_t i=a;i<b;i++)
	{
		s->env+=((s->on?1.0f:0.0f)-s->env)*0.01f;
		double v;
		switch(w)
		{
		case 1: v=s->phase<0.5?1.0:-1.0; break;
		case 2: v=2.0*s->phase-1.0; break;
		case 3: v=4.0*fabs(s->phase-0.5)-1.0; break;
		default: v=sin(s->phase*6.283185307); break;
		}
		s->lp+=(v-s->lp)*k;
		float o=(*s->mute>0.5f)?0.0f:(float)(s->lp*s->env*(*s->gain)*0.5);
		s->out[0][i]=s->out[1][i]=o;
		s->phase+=f/s->rate; if(s->phase>=1.0) s->phase-=1.0;
	}
}

static void run(LV2_Handle h,uint32_t n)
{
	Synth* s=(Synth*)h;
	if(*s->gain!=s->last_gain || *s->cutoff!=s->last_cutoff || *s->wave!=s->last_wave || *s->mute!=s->last_mute)
	{
		tlog("params gain=%.3f cutoff=%.1f mute=%.0f wave=%.0f",*s->gain,*s->cutoff,*s->mute,*s->wave);
		s->last_gain=*s->gain; s->last_cutoff=*s->cutoff; s->last_wave=*s->wave; s->last_mute=*s->mute;
	}

	uint32_t const cap=s->notify->atom.size;
	lv2_atom_forge_set_buffer(&s->forge,(uint8_t*)s->notify,cap);
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_sequence_head(&s->forge,&frame,0);

	uint32_t pos=0;
	LV2_ATOM_SEQUENCE_FOREACH(s->events,ev)
	{
		uint32_t const t=(uint32_t)ev->time.frames;
		if(t>pos && t<=n) { render(s,pos,t); pos=t; }
		if(ev->body.type==s->midi_Event)
		{
			const uint8_t* m=(const uint8_t*)(ev+1);
			if((m[0]&0xF0)==0x90 && m[2]>0)
			{
				s->note=m[1]; s->on=1; s->count++;
				tlog("note on %d at %u",m[1],t);
				int32_t req=m[1];
				s->sched->schedule_work(s->sched->handle,sizeof(req),&req);
			}
			else if((m[0]&0xF0)==0x80 || ((m[0]&0xF0)==0x90 && m[2]==0))
			{
				if(m[1]==s->note) s->on=0;
				tlog("note off %d at %u",m[1],t);
			}
			// echo +12
			uint8_t e[3]={m[0],(uint8_t)(m[1]+12),m[2]};
			if(ev->body.size==3 && ((m[0]&0xE0)==0x80) && m[1]<116)
			{
				lv2_atom_forge_frame_time(&s->forge,t);
				lv2_atom_forge_atom(&s->forge,3,s->midi_Event);
				lv2_atom_forge_write(&s->forge,e,3);
			}
		}
		else if(ev->body.type==s->atom_Object)
		{
			const LV2_Atom_Object* obj=(const LV2_Atom_Object*)&ev->body;
			if(obj->body.otype==s->time_Position)
			{
				const LV2_Atom *bpm=NULL,*speed=NULL,*bb=NULL;
				lv2_atom_object_get(obj,s->time_bpm,&bpm,s->time_speed,&speed,s->time_barBeat,&bb,0);
				tlog("time bpm=%.2f speed=%.1f barBeat=%.2f",bpm&&bpm->type==s->atom_Float?((const LV2_Atom_Float*)bpm)->body:-1.0,
					speed&&speed->type==s->atom_Float?((const LV2_Atom_Float*)speed)->body:-1.0,bb&&bb->type==s->atom_Float?((const LV2_Atom_Float*)bb)->body:-1.0);
			}
		}
	}
	if(pos<n) render(s,pos,n);
	lv2_atom_forge_pop(&s->forge,&frame);

	float peak=0; for(uint32_t i=0;i<n;i++) peak=fmaxf(peak,fabsf(s->out[0][i]));
	*s->level=peak;
}

static void cleanup(LV2_Handle h) { tlog("cleanup"); free(h); }

// worker
static LV2_Worker_Status work(LV2_Handle h,LV2_Worker_Respond_Function respond,LV2_Worker_Respond_Handle rh,uint32_t size,const void* data)
{
	(void)h;
	int32_t note=size==4?*(const int32_t*)data:-1;
	tlog("work note %d",note);
	int32_t r=note*2;
	return respond(rh,sizeof(r),&r);
}
static LV2_Worker_Status work_response(LV2_Handle h,uint32_t size,const void* data)
{
	Synth* s=(Synth*)h;
	s->responses++;
	tlog("work response %d (#%d)",size==4?*(const int32_t*)data:-1,s->responses);
	return LV2_WORKER_SUCCESS;
}

// state
static LV2_State_Status save(LV2_Handle h,LV2_State_Store_Function store,LV2_State_Handle sh,uint32_t flags,const LV2_Feature* const* f)
{
	(void)flags; (void)f;
	Synth* s=(Synth*)h;
	tlog("state save count=%d",s->count);
	return store(sh,s->state_count,&s->count,sizeof(int32_t),s->atom_Int,LV2_STATE_IS_POD|LV2_STATE_IS_PORTABLE);
}
static LV2_State_Status restore(LV2_Handle h,LV2_State_Retrieve_Function retrieve,LV2_State_Handle sh,uint32_t flags,const LV2_Feature* const* f)
{
	(void)flags; (void)f;
	Synth* s=(Synth*)h;
	size_t size; uint32_t type,fl;
	const void* v=retrieve(sh,s->state_count,&size,&type,&fl);
	if(v && size==4 && type==s->atom_Int) s->count=*(const int32_t*)v;
	tlog("state restore count=%d",s->count);
	return LV2_STATE_SUCCESS;
}

static const void* extension_data(const char* uri)
{
	static const LV2_Worker_Interface wi={work,work_response,NULL};
	static const LV2_State_Interface si={save,restore};
	if(!strcmp(uri,LV2_WORKER__interface)) return &wi;
	if(!strcmp(uri,LV2_STATE__interface)) return &si;
	return NULL;
}

static const LV2_Descriptor desc={ADX_URI,instantiate,connect_port,activate,run,deactivate,cleanup,extension_data};
LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t i) { return i==0?&desc:NULL; }
