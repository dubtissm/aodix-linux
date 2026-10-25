// Minimal CLAP test instrument with a raw Xlib editor (for testing the Aodix CLAP host: X11 embedding, timer and fd support,
// resize requests, parameter edits from the editor, state). Build: see README.md
#include <clap/clap.h>
#include <X11/Xlib.h>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

static void tlog(const char* fmt,...)
{
	const char* p=getenv("ADXTEST_LOG");
	if(!p) return;
	FILE* f=fopen(p,"a");
	if(!f) return;
	va_list ap; va_start(ap,fmt); vfprintf(f,fmt,ap); va_end(ap);
	fputc('\n',f); fclose(f);
}

struct Synth
{
	clap_plugin_t plugin;
	const clap_host_t* host;
	const clap_host_timer_support_t* host_timer;
	const clap_host_posix_fd_support_t* host_fd;
	const clap_host_gui_t* host_gui;
	const clap_host_thread_check_t* host_thread;

	double sr=48000,phase=0;
	std::atomic<double> gain{0.8};
	int note=60; bool on=false;
	std::atomic<bool> gui_edit{false};	// gain changed in the editor, to be sent as output event
	int load_us=-1;

	// editor
	Display* dpy=nullptr; Window win=0; GC gc=0; unsigned w=300,h=200; clap_id timer=CLAP_INVALID_ID; int ticks=0;
};

static Synth* S(const clap_plugin_t* p) { return (Synth*)p->plugin_data; }

// ---------------------------------------------------------------------------------------------------------------------------
static bool sy_init(const clap_plugin_t* p)
{
	Synth* s=S(p);
	s->host_timer=(const clap_host_timer_support_t*)s->host->get_extension(s->host,CLAP_EXT_TIMER_SUPPORT);
	s->host_fd=(const clap_host_posix_fd_support_t*)s->host->get_extension(s->host,CLAP_EXT_POSIX_FD_SUPPORT);
	s->host_gui=(const clap_host_gui_t*)s->host->get_extension(s->host,CLAP_EXT_GUI);
	s->host_thread=(const clap_host_thread_check_t*)s->host->get_extension(s->host,CLAP_EXT_THREAD_CHECK);
	tlog("init timer=%d fd=%d gui=%d thread=%d",s->host_timer!=0,s->host_fd!=0,s->host_gui!=0,s->host_thread!=0);
	return true;
}
static void sy_destroy(const clap_plugin_t* p) { delete S(p); }
static bool sy_activate(const clap_plugin_t* p,double sr,uint32_t,uint32_t) { S(p)->sr=sr; tlog("activate %.0f",sr); return true; }
static void sy_deactivate(const clap_plugin_t*) { tlog("deactivate"); }
static bool sy_start(const clap_plugin_t* p)
{
	Synth* s=S(p);
	tlog("start_processing audio_thread=%d",s->host_thread?(int)s->host_thread->is_audio_thread(s->host):-1);
	return true;
}
static void sy_stop(const clap_plugin_t*) { tlog("stop_processing"); }
static void sy_reset(const clap_plugin_t*) {}

static void handle_event(Synth* s,const clap_event_header_t* e)
{
	if(e->space_id!=CLAP_CORE_EVENT_SPACE_ID) return;
	if(e->type==CLAP_EVENT_NOTE_ON) { const clap_event_note_t* n=(const clap_event_note_t*)e; s->note=n->key; s->on=true; }
	else if(e->type==CLAP_EVENT_NOTE_OFF) { const clap_event_note_t* n=(const clap_event_note_t*)e; if(n->key==-1 || n->key==s->note) s->on=false; }
	else if(e->type==CLAP_EVENT_PARAM_VALUE) { const clap_event_param_value_t* v=(const clap_event_param_value_t*)e; if(v->param_id==0) s->gain=v->value; }
}

static void send_gui_edit(Synth* s,const clap_output_events_t* out)
{
	if(!s->gui_edit.exchange(false)) return;
	clap_event_param_value_t ev; memset(&ev,0,sizeof(ev));
	ev.header.size=sizeof(ev); ev.header.type=CLAP_EVENT_PARAM_VALUE; ev.header.space_id=CLAP_CORE_EVENT_SPACE_ID;
	ev.param_id=0; ev.note_id=-1; ev.port_index=-1; ev.channel=-1; ev.key=-1; ev.value=s->gain;
	out->try_push(out,&ev.header);
}

static clap_process_status sy_process(const clap_plugin_t* p,const clap_process_t* pr)
{
	Synth* s=S(p);
	if(s->load_us<0) { const char* e=getenv("ADXTEST_LOAD_US"); s->load_us=e?atoi(e):0; }
	if(s->load_us>0)
	{
		struct timespec a,b; clock_gettime(CLOCK_MONOTONIC,&a); volatile double x=1.0;
		do { for(int k=0;k<200;k++) x=x*1.0000001+1e-9; clock_gettime(CLOCK_MONOTONIC,&b); }
		while((b.tv_sec-a.tv_sec)*1000000L+(b.tv_nsec-a.tv_nsec)/1000<s->load_us);
	}

	uint32_t const ne=pr->in_events->size(pr->in_events);
	uint32_t ei=0;
	float* l=pr->audio_outputs[0].data32[0];
	float* r=pr->audio_outputs[0].data32[1];
	for(uint32_t i=0;i<pr->frames_count;i++)
	{
		while(ei<ne)
		{
			const clap_event_header_t* e=pr->in_events->get(pr->in_events,ei);
			if(e->time>i) break;
			handle_event(s,e); ei++;
		}
		double const inc=440.0*pow(2.0,(s->note-69)/12.0)/s->sr;
		float const v=s->on?(float)(sin(s->phase*6.283185307)*s->gain*0.5):0.f;
		s->phase+=inc; if(s->phase>=1) s->phase-=1;
		l[i]=r[i]=v;
	}
	while(ei<ne) handle_event(s,pr->in_events->get(pr->in_events,ei++));
	send_gui_edit(s,pr->out_events);
	return CLAP_PROCESS_CONTINUE;
}

// audio ports: one stereo output
static uint32_t ap_count(const clap_plugin_t*,bool in) { return in?0:1; }
static bool ap_get(const clap_plugin_t*,uint32_t i,bool in,clap_audio_port_info_t* info)
{
	if(in || i) return false;
	memset(info,0,sizeof(*info)); info->id=0; snprintf(info->name,sizeof(info->name),"Out");
	info->flags=CLAP_AUDIO_PORT_IS_MAIN; info->channel_count=2; info->port_type=CLAP_PORT_STEREO; info->in_place_pair=CLAP_INVALID_ID;
	return true;
}
static const clap_plugin_audio_ports_t s_audio_ports={ap_count,ap_get};

// note ports: one clap note input
static uint32_t np_count(const clap_plugin_t*,bool in) { return in?1:0; }
static bool np_get(const clap_plugin_t*,uint32_t i,bool in,clap_note_port_info_t* info)
{
	if(!in || i) return false;
	memset(info,0,sizeof(*info)); info->id=0; snprintf(info->name,sizeof(info->name),"Notes");
	info->supported_dialects=CLAP_NOTE_DIALECT_CLAP; info->preferred_dialect=CLAP_NOTE_DIALECT_CLAP;
	return true;
}
static const clap_plugin_note_ports_t s_note_ports={np_count,np_get};

// params: gain
static uint32_t pa_count(const clap_plugin_t*) { return 1; }
static bool pa_info(const clap_plugin_t*,uint32_t i,clap_param_info_t* info)
{
	if(i) return false;
	memset(info,0,sizeof(*info)); info->id=0; info->flags=CLAP_PARAM_IS_AUTOMATABLE;
	snprintf(info->name,sizeof(info->name),"Gain"); info->min_value=0; info->max_value=1; info->default_value=0.8;
	return true;
}
static bool pa_value(const clap_plugin_t* p,clap_id id,double* v) { if(id) return false; *v=S(p)->gain; return true; }
static bool pa_to_text(const clap_plugin_t*,clap_id,double v,char* d,uint32_t n) { snprintf(d,n,"%.1f %%",v*100.0); return true; }
static bool pa_from_text(const clap_plugin_t*,clap_id,const char* t,double* v) { *v=atof(t)/100.0; return true; }
static void pa_flush(const clap_plugin_t* p,const clap_input_events_t* in,const clap_output_events_t* out)
{
	Synth* s=S(p);
	for(uint32_t i=0;i<in->size(in);i++) handle_event(s,in->get(in,i));
	send_gui_edit(s,out);
	tlog("flush");
}
static const clap_plugin_params_t s_params={pa_count,pa_info,pa_value,pa_to_text,pa_from_text,pa_flush};

// state
static bool st_save(const clap_plugin_t* p,const clap_ostream_t* o) { float g=(float)S(p)->gain; return o->write(o,&g,4)==4; }
static bool st_load(const clap_plugin_t* p,const clap_istream_t* i) { float g; if(i->read(i,&g,4)!=4) return false; S(p)->gain=g; tlog("state load %.3f",g); return true; }
static const clap_plugin_state_t s_state={st_save,st_load};

// ---------------------------------------------------------------------------------------------------------------------------
// editor
static void draw(Synth* s)
{
	if(!s->dpy) return;
	XSetForeground(s->dpy,s->gc,0x303020); XFillRectangle(s->dpy,s->win,s->gc,0,0,s->w,s->h);
	XSetForeground(s->dpy,s->gc,0xe0a030); XFillRectangle(s->dpy,s->win,s->gc,10,s->h-30,(int)((s->w-20)*s->gain),20);
	XSetForeground(s->dpy,s->gc,0xffffff);
	char buf[80]; snprintf(buf,sizeof(buf),"ADX Test CLAP  gain %.2f  tick %d",(double)s->gain,s->ticks);
	XDrawString(s->dpy,s->win,s->gc,10,20,buf,strlen(buf));
	XDrawString(s->dpy,s->win,s->gc,10,40,"left: set gain   right: grow",28);
	XFlush(s->dpy);
}
static void on_fd(const clap_plugin_t* p,int,clap_posix_fd_flags_t)
{
	Synth* s=S(p);
	while(s->dpy && XPending(s->dpy))
	{
		XEvent e; XNextEvent(s->dpy,&e);
		if(e.type==Expose) draw(s);
		else if(e.type==ButtonPress)
		{
			tlog("click %d %d button %d",e.xbutton.x,e.xbutton.y,e.xbutton.button);
			if(e.xbutton.button==1) { s->gain=e.xbutton.x/(double)s->w; s->gui_edit=true; draw(s); }
			else if(e.xbutton.button==3 && s->host_gui)
			{
				unsigned const nw=s->w+100,nh=s->h+50;
				bool ok=s->host_gui->request_resize(s->host,nw,nh);
				tlog("request_resize -> %d",ok);
				if(ok) { s->w=nw; s->h=nh; XResizeWindow(s->dpy,s->win,nw,nh); draw(s); }
			}
		}
	}
}
static const clap_plugin_posix_fd_support_t s_fd={on_fd};
static void on_timer(const clap_plugin_t* p,clap_id) { Synth* s=S(p); s->ticks++; if(s->ticks==1||s->ticks%20==0) tlog("timer %d",s->ticks); draw(s); }
static const clap_plugin_timer_support_t s_timer={on_timer};

static bool g_supported(const clap_plugin_t*,const char* api,bool floating) { return !floating && !strcmp(api,CLAP_WINDOW_API_X11); }
static bool g_preferred(const clap_plugin_t*,const char** api,bool* floating) { *api=CLAP_WINDOW_API_X11; *floating=false; return true; }
static bool g_create(const clap_plugin_t*,const char* api,bool floating) { tlog("gui create %s %d",api,floating); return !floating; }
static void g_destroy(const clap_plugin_t* p)
{
	Synth* s=S(p);
	tlog("gui destroy ticks=%d",s->ticks);
	if(s->timer!=CLAP_INVALID_ID && s->host_timer) s->host_timer->unregister_timer(s->host,s->timer);
	s->timer=CLAP_INVALID_ID;
	if(s->dpy) { if(s->host_fd) s->host_fd->unregister_fd(s->host,ConnectionNumber(s->dpy)); XFreeGC(s->dpy,s->gc); XDestroyWindow(s->dpy,s->win); XCloseDisplay(s->dpy); s->dpy=nullptr; }
}
static bool g_scale(const clap_plugin_t*,double) { return true; }
static bool g_size(const clap_plugin_t* p,uint32_t* w,uint32_t* h) { *w=S(p)->w; *h=S(p)->h; return true; }
static bool g_can_resize(const clap_plugin_t*) { return getenv("ADXTEST_FIXED_GUI")==nullptr; }
static bool g_hints(const clap_plugin_t*,clap_gui_resize_hints_t* h) { memset(h,0,sizeof(*h)); h->can_resize_horizontally=h->can_resize_vertically=true; return true; }
static bool g_adjust(const clap_plugin_t*,uint32_t* w,uint32_t* h) { if(*w<200) *w=200; if(*h<100) *h=100; return true; }
static bool g_set_size(const clap_plugin_t* p,uint32_t w,uint32_t h)
{
	Synth* s=S(p);
	if(getenv("ADXTEST_FIXED_GUI")) { tlog("HOST MISBEHAVING: set_size on a non-resizable gui"); return false; }
	s->w=w; s->h=h; tlog("set_size %u %u",w,h);
	if(s->dpy) { XResizeWindow(s->dpy,s->win,w,h); draw(s); }
	return true;
}
static bool g_set_parent(const clap_plugin_t* p,const clap_window_t* w)
{
	Synth* s=S(p);
	tlog("set_parent %lu",(unsigned long)w->x11);
	s->dpy=XOpenDisplay(nullptr);
	if(!s->dpy) return false;
	s->win=XCreateSimpleWindow(s->dpy,(Window)w->x11,0,0,s->w,s->h,0,0,0x303020);
	XSelectInput(s->dpy,s->win,ExposureMask|ButtonPressMask);
	s->gc=XCreateGC(s->dpy,s->win,0,nullptr);
	if(s->host_fd) s->host_fd->register_fd(s->host,ConnectionNumber(s->dpy),CLAP_POSIX_FD_READ);
	if(s->host_timer) s->host_timer->register_timer(s->host,50,&s->timer);
	return true;
}
static bool g_set_transient(const clap_plugin_t*,const clap_window_t*) { return false; }
static void g_title(const clap_plugin_t*,const char*) {}
static bool g_show(const clap_plugin_t* p) { Synth* s=S(p); if(s->dpy) { XMapWindow(s->dpy,s->win); XFlush(s->dpy); } tlog("gui show"); return true; }
static bool g_hide(const clap_plugin_t* p) { Synth* s=S(p); if(s->dpy) { XUnmapWindow(s->dpy,s->win); XFlush(s->dpy); } tlog("gui hide"); return true; }
static const clap_plugin_gui_t s_gui={g_supported,g_preferred,g_create,g_destroy,g_scale,g_size,g_can_resize,g_hints,g_adjust,g_set_size,g_set_parent,g_set_transient,g_title,g_show,g_hide};

static const void* sy_ext(const clap_plugin_t*,const char* id)
{
	if(!strcmp(id,CLAP_EXT_AUDIO_PORTS)) return &s_audio_ports;
	if(!strcmp(id,CLAP_EXT_NOTE_PORTS)) return &s_note_ports;
	if(!strcmp(id,CLAP_EXT_PARAMS)) return &s_params;
	if(!strcmp(id,CLAP_EXT_STATE)) return &s_state;
	if(!strcmp(id,CLAP_EXT_GUI)) return &s_gui;
	if(!strcmp(id,CLAP_EXT_POSIX_FD_SUPPORT)) return &s_fd;
	if(!strcmp(id,CLAP_EXT_TIMER_SUPPORT)) return &s_timer;
	return nullptr;
}
static void sy_main(const clap_plugin_t*) {}

// ---------------------------------------------------------------------------------------------------------------------------
static const char* s_features[]={CLAP_PLUGIN_FEATURE_INSTRUMENT,CLAP_PLUGIN_FEATURE_SYNTHESIZER,nullptr};
static const clap_plugin_descriptor_t s_desc={CLAP_VERSION_INIT,"org.aodix.test-synth","ADX Test CLAP","Aodix","","","","1.0","Aodix host test instrument",s_features};

static uint32_t f_count(const clap_plugin_factory_t*) { return 1; }
static const clap_plugin_descriptor_t* f_desc(const clap_plugin_factory_t*,uint32_t i) { return i==0?&s_desc:nullptr; }
static const clap_plugin_t* f_create(const clap_plugin_factory_t*,const clap_host_t* host,const char* id)
{
	if(strcmp(id,s_desc.id)) return nullptr;
	Synth* s=new Synth();
	s->host=host;
	clap_plugin_t& p=s->plugin;
	p.desc=&s_desc; p.plugin_data=s;
	p.init=sy_init; p.destroy=sy_destroy; p.activate=sy_activate; p.deactivate=sy_deactivate;
	p.start_processing=sy_start; p.stop_processing=sy_stop; p.reset=sy_reset; p.process=sy_process;
	p.get_extension=sy_ext; p.on_main_thread=sy_main;
	return &p;
}
static const clap_plugin_factory_t s_factory={f_count,f_desc,f_create};

static bool e_init(const char*) { return true; }
static void e_deinit(void) {}
static const void* e_factory(const char* id) { return strcmp(id,CLAP_PLUGIN_FACTORY_ID)?nullptr:&s_factory; }

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry={CLAP_VERSION_INIT,e_init,e_deinit,e_factory};
