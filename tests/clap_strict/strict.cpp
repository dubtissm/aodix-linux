// Strict CLAP test plugin: built on clap-helpers with MisbehaviourHandler::Terminate and CheckingLevel::Maximal
// (like u-he plugins): any host rule violation terminates the process. Non-resizable Xlib editor that requests a resize.
#include <clap/helpers/plugin.hh>
#include <clap/helpers/plugin.hxx>
#include <clap/helpers/host-proxy.hxx>
#include <X11/Xlib.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>

static void tlog(const char* fmt,...)
{
	const char* p=getenv("ADXTEST_LOG"); if(!p) return;
	FILE* f=fopen(p,"a"); if(!f) return;
	va_list ap; va_start(ap,fmt); fprintf(f,"[strict] "); vfprintf(f,fmt,ap); va_end(ap); fputc('\n',f); fclose(f);
}

using Base=clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,clap::helpers::CheckingLevel::Maximal>;

static const char* s_features[]={CLAP_PLUGIN_FEATURE_INSTRUMENT,nullptr};
static const clap_plugin_descriptor_t s_desc={CLAP_VERSION_INIT,"org.aodix.strict","ADX Strict CLAP","Aodix","","","","1.0","",s_features};

class Strict : public Base
{
public:
	Strict(const clap_host* h):Base(&s_desc,h) {}

	bool activate(double sr,uint32_t,uint32_t) noexcept override { rate=sr; tlog("activate"); return true; }
	void deactivate() noexcept override { tlog("deactivate"); }
	bool startProcessing() noexcept override { tlog("start"); return true; }
	void stopProcessing() noexcept override { tlog("stop"); }
	clap_process_status process(const clap_process* p) noexcept override
	{
		uint32_t const n=p->in_events->size(p->in_events);
		for(uint32_t i=0;i<n;i++)
		{
			auto e=p->in_events->get(p->in_events,i);
			if(e->type==CLAP_EVENT_NOTE_ON) { note=((const clap_event_note*)e)->key; on=true; }
			if(e->type==CLAP_EVENT_NOTE_OFF) on=false;
			if(e->type==CLAP_EVENT_PARAM_VALUE) gain=((const clap_event_param_value*)e)->value;
		}
		for(uint32_t i=0;i<p->frames_count;i++)
		{
			float v=on?(float)(sin(phase*6.283185307)*gain*0.5):0.f;
			phase+=440.0*pow(2.0,(note-69)/12.0)/rate; if(phase>=1) phase-=1;
			p->audio_outputs[0].data32[0][i]=p->audio_outputs[0].data32[1][i]=v;
		}
		return CLAP_PROCESS_CONTINUE;
	}

	bool implementsAudioPorts() const noexcept override { return true; }
	uint32_t audioPortsCount(bool in) const noexcept override { return in?0:1; }
	bool audioPortsInfo(uint32_t i,bool in,clap_audio_port_info* info) const noexcept override
	{
		if(in||i) return false;
		memset(info,0,sizeof(*info)); info->channel_count=2; info->flags=CLAP_AUDIO_PORT_IS_MAIN; info->port_type=CLAP_PORT_STEREO; info->in_place_pair=CLAP_INVALID_ID;
		snprintf(info->name,sizeof(info->name),"Out"); return true;
	}
	bool implementsNotePorts() const noexcept override { return true; }
	uint32_t notePortsCount(bool in) const noexcept override { return in?1:0; }
	bool notePortsInfo(uint32_t i,bool in,clap_note_port_info* info) const noexcept override
	{
		if(!in||i) return false;
		memset(info,0,sizeof(*info)); info->supported_dialects=CLAP_NOTE_DIALECT_CLAP|CLAP_NOTE_DIALECT_MIDI; info->preferred_dialect=CLAP_NOTE_DIALECT_CLAP;
		snprintf(info->name,sizeof(info->name),"Notes"); return true;
	}
	bool implementsParams() const noexcept override { return true; }
	uint32_t paramsCount() const noexcept override { return 1; }
	bool paramsInfo(uint32_t i,clap_param_info* info) const noexcept override
	{
		if(i) return false;
		memset(info,0,sizeof(*info)); info->id=0; info->flags=CLAP_PARAM_IS_AUTOMATABLE; info->min_value=0; info->max_value=1; info->default_value=0.8;
		snprintf(info->name,sizeof(info->name),"Gain"); return true;
	}
	bool paramsValue(clap_id,double* v) noexcept override { *v=gain; return true; }
	bool paramsValueToText(clap_id,double v,char* d,uint32_t n) noexcept override { snprintf(d,n,"%.0f %%",v*100); return true; }
	void paramsFlush(const clap_input_events* in,const clap_output_events*) noexcept override
	{
		for(uint32_t i=0;i<in->size(in);i++) { auto e=in->get(in,i); if(e->type==CLAP_EVENT_PARAM_VALUE) gain=((const clap_event_param_value*)e)->value; }
		tlog("flush");
	}
	bool implementsState() const noexcept override { return true; }
	bool stateSave(const clap_ostream* o) noexcept override { float g=(float)gain; return o->write(o,&g,4)==4; }
	bool stateLoad(const clap_istream* i) noexcept override { float g; if(i->read(i,&g,4)!=4) return false; gain=g; tlog("state load"); return true; }

	// fixed size editor
	bool implementsGui() const noexcept override { return true; }
	bool guiIsApiSupported(const char* api,bool fl) noexcept override { return !fl && !strcmp(api,CLAP_WINDOW_API_X11); }
	bool guiCreate(const char*,bool) noexcept override { tlog("gui create"); return true; }
	void guiDestroy() noexcept override
	{
		tlog("gui destroy");
		if(timer!=CLAP_INVALID_ID) _host.timerSupportUnregister(timer);
		timer=CLAP_INVALID_ID;
		if(dpy) { _host.posixFdSupportUnregister(ConnectionNumber(dpy)); XDestroyWindow(dpy,win); XCloseDisplay(dpy); dpy=nullptr; }
	}
	bool guiGetSize(uint32_t* w,uint32_t* h) noexcept override { *w=width; *h=height; return true; }
	bool guiCanResize() const noexcept override { return false; }
	bool guiSetParent(const clap_window* w) noexcept override
	{
		dpy=XOpenDisplay(nullptr); if(!dpy) return false;
		win=XCreateSimpleWindow(dpy,(Window)w->x11,0,0,width,height,0,0,0x204020);
		XSelectInput(dpy,win,ExposureMask|ButtonPressMask);
		_host.posixFdSupportRegister(ConnectionNumber(dpy),CLAP_POSIX_FD_READ);
		_host.timerSupportRegister(40,&timer);
		// like u-he: after attaching, the editor wants a different size (its own gui scale)
		width=360; height=240; XResizeWindow(dpy,win,width,height);
		bool ok=_host.guiRequestResize(width,height); tlog("request_resize after set_parent -> %d",ok);
		return true;
	}
	bool guiShow() noexcept override { if(dpy) { XMapWindow(dpy,win); XFlush(dpy); } tlog("gui show"); return true; }
	bool guiHide() noexcept override { if(dpy) { XUnmapWindow(dpy,win); XFlush(dpy); } tlog("gui hide"); return true; }
	bool implementsTimerSupport() const noexcept override { return true; }
	void onTimer(clap_id) noexcept override { ticks++; if(ticks%25==1) tlog("timer %d",ticks); }
	bool implementsPosixFdSupport() const noexcept override { return true; }
	void onPosixFd(int,clap_posix_fd_flags_t) noexcept override
	{
		while(dpy && XPending(dpy))
		{
			XEvent e; XNextEvent(dpy,&e);
			if(e.type==ButtonPress && e.xbutton.button==3)
			{
				width+=80; height+=40; XResizeWindow(dpy,win,width,height);
				bool ok=_host.guiRequestResize(width,height); tlog("request_resize -> %d (%ux%u)",ok,width,height);
			}
		}
	}

	double rate=48000,phase=0,gain=0.8; int note=60; bool on=false;
	Display* dpy=nullptr; Window win=0; uint32_t width=300,height=200; clap_id timer=CLAP_INVALID_ID; int ticks=0;
};

static uint32_t f_count(const clap_plugin_factory*) { return 1; }
static const clap_plugin_descriptor* f_desc(const clap_plugin_factory*,uint32_t i) { return i==0?&s_desc:nullptr; }
static const clap_plugin* f_create(const clap_plugin_factory*,const clap_host* host,const char* id)
{
	if(strcmp(id,s_desc.id)) return nullptr;
	return (new Strict(host))->clapPlugin();
}
static const clap_plugin_factory s_factory={f_count,f_desc,f_create};
static bool e_init(const char*) { return true; }
static void e_deinit() {}
static const void* e_factory(const char* id) { return strcmp(id,CLAP_PLUGIN_FACTORY_ID)?nullptr:&s_factory; }
extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry={CLAP_VERSION_INIT,e_init,e_deinit,e_factory};
