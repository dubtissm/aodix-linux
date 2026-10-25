/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - CLAP plugin hosting
//
// CLAP plugins are shared libraries (*.clap) exporting clap_entry; the plugin factory lists one or more plugins, identified by
// a reverse-domain id string. Aodix stores a 32-bit hash of that id in its projects (like the vst3 class ids).
//
// Threads: "main thread" is the aodix gui thread, "audio thread" is whichever dsp thread is processing the instance
// (instances may be processed in parallel). Parameter changes from aodix are queued and delivered as events in the next
// process call; changes made in the plugin editor arrive as output events and are passed to aodix (automation) through the
// gui thread. The editor is embedded with the X11 window api, plugin timers and file descriptors run in the aodix message loop.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AdxPlugin.h"
#include "../platform/adxplat_runloop.h"

#include <clap/clap.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <dlfcn.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// largest block passed to process() (aodix uses at most 2048 frames)
static const uint32_t MAX_PROCESS_FRAMES=8192;

static std::thread::id g_main_thread;
static thread_local bool tl_audio_thread=false;

static int32_t id_hash(const char* s)
{
	uint32_t h=2166136261u;
	for(;*s;s++)
	{
		h^=(uint8_t)*s;
		h*=16777619u;
	}
	return (int32_t)(h?h:1);
}

static void copy_str(char* dst,const char* src,int max_chars)
{
	snprintf(dst,max_chars+1,"%s",src?src:"");
}

// tiny spin lock (held only for a few instructions, also in the audio thread)
class SpinLock
{
public:
	void lock(void) { while(flag.test_and_set(std::memory_order_acquire)) {} }
	bool try_lock(void) { return !flag.test_and_set(std::memory_order_acquire); }
	void unlock(void) { flag.clear(std::memory_order_release); }
private:
	std::atomic_flag flag=ATOMIC_FLAG_INIT;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// module cache: libraries are initialized once and never unloaded
struct ClapModule
{
	void*						lib;
	const clap_plugin_entry_t*	entry;
	const clap_plugin_factory_t*	factory;
};

static std::mutex g_module_mutex;
static std::map<std::string,ClapModule> g_modules;

static const ClapModule* clap_open(const char* path,std::string& error)
{
	std::lock_guard<std::mutex> lock(g_module_mutex);
	auto it=g_modules.find(path);
	if(it!=g_modules.end())
		return &it->second;

	if(g_main_thread==std::thread::id())
		g_main_thread=std::this_thread::get_id();

	void* lib=dlopen(path,RTLD_NOW|RTLD_LOCAL);
	if(!lib)
	{
		const char* e=dlerror();
		error=e?e:"dlopen failed";
		return nullptr;
	}

	const clap_plugin_entry_t* entry=(const clap_plugin_entry_t*)dlsym(lib,"clap_entry");
	if(!entry)
	{
		error="not a CLAP plugin (no clap_entry)";
		dlclose(lib);
		return nullptr;
	}
	if(!clap_version_is_compatible(entry->clap_version))
	{
		error="incompatible CLAP version";
		dlclose(lib);
		return nullptr;
	}
	if(!entry->init(path))
	{
		error="clap_entry init failed";
		dlclose(lib);
		return nullptr;
	}

	const clap_plugin_factory_t* factory=(const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
	if(!factory)
	{
		error="no CLAP plugin factory";
		entry->deinit();
		dlclose(lib);
		return nullptr;
	}

	ClapModule& m=g_modules[path];
	m.lib=lib;
	m.entry=entry;
	m.factory=factory;
	return &m;
}

static bool has_feature(const clap_plugin_descriptor_t* d,const char* feature)
{
	if(!d->features)
		return false;
	for(const char* const* f=d->features;*f;f++)
		if(strcmp(*f,feature)==0)
			return true;
	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ClapPlugin : public AdxPlugin
{
public:
	ClapPlugin(const ClapModule* m,const clap_plugin_descriptor_t* d,AdxPluginHost* h);
	~ClapPlugin() override;

	bool		init(char* error,int error_size);

	const char*	format(void) override { return "CLAP"; }
	void		getEffectName(char* buf) override { copy_str(buf,desc->name,64); }

	void		setSampleRate(double rate) override;
	void		setBlockSize(int) override {}
	void		setActive(bool active) override;

	float		getParameter(int index) override;
	void		setParameter(int index,float value) override;
	void		getParamName(int index,char* buf) override;
	void		getParamDisplay(int index,char* buf) override;

	int			getProgram(void) override { return 0; }
	void		setProgram(int) override {}
	void		getProgramName(char* buf) override { copy_str(buf,program_name.c_str(),24); }
	void		setProgramName(const char* name) override { program_name=name?name:""; }
	bool		getProgramNameIndexed(int index,char* buf) override { if(index!=0) return false; getProgramName(buf); return true; }

	int			getChunk(void** data,bool preset) override;
	void		setChunk(const void* data,int size,bool preset) override;

	void		process(float** inputs,float** outputs,int frames,const ADX_MIDI_EVENT* events,int num_events) override;

	bool		editorGetSize(int* width,int* height) override;
	bool		editorOpen(unsigned long parent) override;
	void		editorClose(void) override;
	void		editorResized(int width,int height) override;
	bool		editorCanResize(void) override;

	void		idle(void) override;

	// host callbacks
	static ClapPlugin* from(const clap_host_t* h) { return (ClapPlugin*)h->host_data; }
	static const void* CLAP_ABI host_get_extension(const clap_host_t* h,const char* id);
	static void CLAP_ABI host_request_restart(const clap_host_t* h) { from(h)->restart_requested=true; }
	static void CLAP_ABI host_request_process(const clap_host_t*) {}
	static void CLAP_ABI host_request_callback(const clap_host_t* h) { from(h)->callback_requested=true; }

private:
	struct Param
	{
		clap_id			id;
		std::string		name;
		double			min;
		double			max;
		double			def;
		uint32_t		flags;
		void*			cookie;
	};

	struct ParamChange
	{
		clap_id			id;
		double			value;
	};

	union InEvent
	{
		clap_event_header_t			header;
		clap_event_note_t			note;
		clap_event_midi_t			midi;
		clap_event_param_value_t	param;
	};

	void		scan_ports(void);
	void		scan_params(void);
	void		refresh_param_values(void);
	double		to_value(const Param& p,float n) const;
	float		to_normalized(const Param& p,double v) const;
	int			param_index(clap_id id) const;
	void		stop_processing_now(void);

	// input / output event lists
	static uint32_t CLAP_ABI in_size(const clap_input_events_t* l) { return (uint32_t)((ClapPlugin*)l->ctx)->in_events.size(); }
	static const clap_event_header_t* CLAP_ABI in_get(const clap_input_events_t* l,uint32_t i) { return &((ClapPlugin*)l->ctx)->in_events[i].header; }
	static bool CLAP_ABI out_push(const clap_output_events_t* l,const clap_event_header_t* e);

	const ClapModule*				module;
	const clap_plugin_descriptor_t*	desc;
	AdxPluginHost*					host;
	clap_host_t						host_struct;
	const clap_plugin_t*			plugin;

	// plugin extensions
	const clap_plugin_params_t*			ext_params;
	const clap_plugin_state_t*			ext_state;
	const clap_plugin_gui_t*			ext_gui;
	const clap_plugin_audio_ports_t*	ext_audio_ports;
	const clap_plugin_note_ports_t*		ext_note_ports;
	const clap_plugin_timer_support_t*	ext_timer;
	const clap_plugin_posix_fd_support_t* ext_fd;

	// ports
	std::vector<uint32_t>			in_port_channels;
	std::vector<uint32_t>			out_port_channels;
	std::vector<clap_audio_buffer_t>	in_buffers;
	std::vector<clap_audio_buffer_t>	out_buffers;
	int								note_in_port;		// -1 = no note input
	uint32_t						note_in_dialects;
	bool							note_out;

	// parameters
	std::vector<Param>				params;
	std::unordered_map<clap_id,int>	param_by_id;
	std::vector<std::atomic<float>>	param_cache;		// normalized values
	SpinLock						proc_lock;			// aodix -> audio thread
	std::vector<ParamChange>		proc_pending;
	std::vector<ParamChange>		proc_work;
	SpinLock						gui_lock;			// audio thread (plugin edits) -> gui thread
	std::vector<ParamChange>		gui_pending;
	std::vector<ParamChange>		gui_work;

	// processing
	SpinLock						busy;				// audio thread vs. activation changes
	double							sample_rate;
	bool							active;
	std::atomic<bool>				processing;
	std::atomic<bool>				stop_request;
	int64_t							steady_time;
	std::vector<InEvent>			in_events;
	std::vector<ADX_MIDI_EVENT>		midi_out;
	clap_input_events_t				in_list;
	clap_output_events_t			out_list;

	// requests from the plugin (handled in idle)
	std::atomic<bool>				restart_requested;
	std::atomic<bool>				callback_requested;
	std::atomic<bool>				rescan_params;

	// editor
	bool							gui_created;
	std::atomic<uint64_t>			pending_resize;			// width<<32 | height requested from another thread
	uint32_t						host_w,host_h;			// size aodix gave the editor window itself (not a user resize)
	int								gui_resizable;			// -1 = unknown, cached after the editor was opened
	bool							gui_open;				// set_parent / show done
	std::atomic<bool>				gui_destroy_requested;	// the plugin lost its gui (closed with was_destroyed)

	// activation changes requested outside the main thread (e.g. jack sample rate callback), done in idle()
	std::atomic<double>				deferred_rate;
	std::atomic<int>				deferred_active;		// -1 = none
	struct TimerCtx { ClapPlugin* self; clap_id id; };
	std::map<clap_id,TimerCtx*>		timers;
	std::vector<int>				fds;

	std::string						program_name;
	std::vector<char>				chunk;

	friend struct ClapHostExt;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// host extensions
struct ClapHostExt
{
	static ClapPlugin* p(const clap_host_t* h) { return ClapPlugin::from(h); }

	// log
	static void CLAP_ABI log(const clap_host_t* h,clap_log_severity s,const char* msg)
	{
		if(s==CLAP_LOG_DEBUG)
			return;
		fprintf(stderr,"aodix: clap: %s: %s\n",p(h)->desc->name,msg?msg:"");
	}

	// thread check
	static bool CLAP_ABI is_main_thread(const clap_host_t*) { return std::this_thread::get_id()==g_main_thread; }
	static bool CLAP_ABI is_audio_thread(const clap_host_t*) { return tl_audio_thread; }

	// params
	static void CLAP_ABI params_rescan(const clap_host_t* h,clap_param_rescan_flags) { p(h)->rescan_params=true; }
	static void CLAP_ABI params_clear(const clap_host_t*,clap_id,clap_param_clear_flags) {}
	static void CLAP_ABI params_request_flush(const clap_host_t*) {}

	// state
	static void CLAP_ABI state_mark_dirty(const clap_host_t*) {}

	// gui
	static void CLAP_ABI gui_resize_hints_changed(const clap_host_t*) {}
	// [thread-safe]: the aodix window may only be touched in the gui thread, other threads are served in idle().
	// the host must not call set_size() here (not allowed for non-resizable editors, e.g. u-he plugins terminate)
	static bool CLAP_ABI gui_request_resize(const clap_host_t* h,uint32_t w,uint32_t hh)
	{
		ClapPlugin* cp=p(h);
		if(w==0 || hh==0)
			return false;
		if(std::this_thread::get_id()==g_main_thread)
		{
			cp->host_w=w;
			cp->host_h=hh;
			cp->host->plugin_resize_editor(cp,(int)w,(int)hh);
		}
		else
		{
			cp->pending_resize.store(((uint64_t)w<<32)|hh);
		}
		return true;
	}
	static bool CLAP_ABI gui_request_show(const clap_host_t*) { return false; }
	static bool CLAP_ABI gui_request_hide(const clap_host_t*) { return false; }
	static void CLAP_ABI gui_closed(const clap_host_t* h,bool was_destroyed) { if(was_destroyed) p(h)->gui_destroy_requested=true; }

	// timers
	static void timer_cb(void* user)
	{
		ClapPlugin::TimerCtx* t=(ClapPlugin::TimerCtx*)user;
		if(t->self->ext_timer)
			t->self->ext_timer->on_timer(t->self->plugin,t->id);
	}
	static bool CLAP_ABI register_timer(const clap_host_t* h,uint32_t period_ms,clap_id* timer_id)
	{
		ClapPlugin* cp=p(h);
		ClapPlugin::TimerCtx* t=new ClapPlugin::TimerCtx{cp,CLAP_INVALID_ID};
		unsigned long const id=adxplat::timer_start(period_ms>0?period_ms:1,timer_cb,t);
		t->id=(clap_id)id;
		cp->timers[t->id]=t;
		*timer_id=t->id;
		return true;
	}
	static bool CLAP_ABI unregister_timer(const clap_host_t* h,clap_id timer_id)
	{
		ClapPlugin* cp=p(h);
		auto it=cp->timers.find(timer_id);
		if(it==cp->timers.end())
			return false;
		adxplat::timer_stop(timer_id);
		delete it->second;
		cp->timers.erase(it);
		return true;
	}

	// file descriptors (the aodix loop watches for readability)
	static void fd_cb(int fd,void* user)
	{
		ClapPlugin* cp=(ClapPlugin*)user;
		if(cp->ext_fd)
			cp->ext_fd->on_fd(cp->plugin,fd,CLAP_POSIX_FD_READ);
	}
	static bool CLAP_ABI register_fd(const clap_host_t* h,int fd,clap_posix_fd_flags_t)
	{
		ClapPlugin* cp=p(h);
		adxplat::add_fd_watch(fd,fd_cb,cp);
		cp->fds.push_back(fd);
		return true;
	}
	static bool CLAP_ABI modify_fd(const clap_host_t*,int,clap_posix_fd_flags_t) { return true; }
	static bool CLAP_ABI unregister_fd(const clap_host_t* h,int fd)
	{
		ClapPlugin* cp=p(h);
		adxplat::remove_fd_watch(fd,cp);
		for(size_t i=0;i<cp->fds.size();i++)
			if(cp->fds[i]==fd)
			{
				cp->fds.erase(cp->fds.begin()+i);
				break;
			}
		return true;
	}

	// audio / note ports, latency, tail
	static bool CLAP_ABI audio_ports_is_rescan_flag_supported(const clap_host_t*,uint32_t) { return false; }
	static void CLAP_ABI audio_ports_rescan(const clap_host_t*,uint32_t) {}
	static uint32_t CLAP_ABI note_ports_supported_dialects(const clap_host_t*) { return CLAP_NOTE_DIALECT_CLAP|CLAP_NOTE_DIALECT_MIDI; }
	static void CLAP_ABI note_ports_rescan(const clap_host_t*,uint32_t) {}
	static void CLAP_ABI latency_changed(const clap_host_t*) {}
	static void CLAP_ABI tail_changed(const clap_host_t*) {}
};

static const clap_host_log_t			g_host_log={ClapHostExt::log};
static const clap_host_thread_check_t	g_host_thread_check={ClapHostExt::is_main_thread,ClapHostExt::is_audio_thread};
static const clap_host_params_t			g_host_params={ClapHostExt::params_rescan,ClapHostExt::params_clear,ClapHostExt::params_request_flush};
static const clap_host_state_t			g_host_state={ClapHostExt::state_mark_dirty};
static const clap_host_gui_t			g_host_gui={ClapHostExt::gui_resize_hints_changed,ClapHostExt::gui_request_resize,ClapHostExt::gui_request_show,ClapHostExt::gui_request_hide,ClapHostExt::gui_closed};
static const clap_host_timer_support_t	g_host_timer={ClapHostExt::register_timer,ClapHostExt::unregister_timer};
static const clap_host_posix_fd_support_t g_host_fd={ClapHostExt::register_fd,ClapHostExt::modify_fd,ClapHostExt::unregister_fd};
static const clap_host_audio_ports_t	g_host_audio_ports={ClapHostExt::audio_ports_is_rescan_flag_supported,ClapHostExt::audio_ports_rescan};
static const clap_host_note_ports_t		g_host_note_ports={ClapHostExt::note_ports_supported_dialects,ClapHostExt::note_ports_rescan};
static const clap_host_latency_t		g_host_latency={ClapHostExt::latency_changed};
static const clap_host_tail_t			g_host_tail={ClapHostExt::tail_changed};

const void* CLAP_ABI ClapPlugin::host_get_extension(const clap_host_t*,const char* id)
{
	if(!strcmp(id,CLAP_EXT_LOG)) return &g_host_log;
	if(!strcmp(id,CLAP_EXT_THREAD_CHECK)) return &g_host_thread_check;
	if(!strcmp(id,CLAP_EXT_PARAMS)) return &g_host_params;
	if(!strcmp(id,CLAP_EXT_STATE)) return &g_host_state;
	if(!strcmp(id,CLAP_EXT_GUI)) return &g_host_gui;
	if(!strcmp(id,CLAP_EXT_TIMER_SUPPORT)) return &g_host_timer;
	if(!strcmp(id,CLAP_EXT_POSIX_FD_SUPPORT)) return &g_host_fd;
	if(!strcmp(id,CLAP_EXT_AUDIO_PORTS)) return &g_host_audio_ports;
	if(!strcmp(id,CLAP_EXT_NOTE_PORTS)) return &g_host_note_ports;
	if(!strcmp(id,CLAP_EXT_LATENCY)) return &g_host_latency;
	if(!strcmp(id,CLAP_EXT_TAIL)) return &g_host_tail;
	return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ClapPlugin::ClapPlugin(const ClapModule* m,const clap_plugin_descriptor_t* d,AdxPluginHost* h)
{
	module=m;
	desc=d;
	host=h;
	plugin=nullptr;
	ext_params=nullptr;
	ext_state=nullptr;
	ext_gui=nullptr;
	ext_audio_ports=nullptr;
	ext_note_ports=nullptr;
	ext_timer=nullptr;
	ext_fd=nullptr;
	note_in_port=-1;
	note_in_dialects=0;
	note_out=false;
	sample_rate=48000.0;
	active=false;
	processing=false;
	stop_request=false;
	steady_time=0;
	restart_requested=false;
	callback_requested=false;
	rescan_params=false;
	gui_created=false;
	pending_resize=0;
	host_w=host_h=0;
	gui_resizable=-1;
	gui_open=false;
	gui_destroy_requested=false;
	deferred_rate=0.0;
	deferred_active=-1;
	program_name="Default";

	memset(&host_struct,0,sizeof(host_struct));
	host_struct.clap_version=CLAP_VERSION;
	host_struct.host_data=this;
	host_struct.name="Aodix";
	host_struct.vendor="Arguru Software / Aodix Linux";
	host_struct.url="https://github.com/vanjac/aodix-enhanced";
	host_struct.version="4.2.0.3";
	host_struct.get_extension=host_get_extension;
	host_struct.request_restart=host_request_restart;
	host_struct.request_process=host_request_process;
	host_struct.request_callback=host_request_callback;

	in_list.ctx=this;
	in_list.size=in_size;
	in_list.get=in_get;
	out_list.ctx=this;
	out_list.try_push=out_push;

	in_events.reserve(2048);
	midi_out.reserve(1024);
	proc_pending.reserve(256);
	proc_work.reserve(256);
	gui_pending.reserve(256);
	gui_work.reserve(256);
}

bool ClapPlugin::init(char* error,int error_size)
{
	plugin=module->factory->create_plugin(module->factory,&host_struct,desc->id);
	if(!plugin)
	{
		snprintf(error,error_size,"The CLAP plugin '%s' could not be created.",desc->name);
		return false;
	}
	if(!plugin->init(plugin))
	{
		plugin->destroy(plugin);
		plugin=nullptr;
		snprintf(error,error_size,"The CLAP plugin '%s' could not be initialized.",desc->name);
		return false;
	}

	ext_params=(const clap_plugin_params_t*)plugin->get_extension(plugin,CLAP_EXT_PARAMS);
	ext_state=(const clap_plugin_state_t*)plugin->get_extension(plugin,CLAP_EXT_STATE);
	ext_gui=(const clap_plugin_gui_t*)plugin->get_extension(plugin,CLAP_EXT_GUI);
	ext_audio_ports=(const clap_plugin_audio_ports_t*)plugin->get_extension(plugin,CLAP_EXT_AUDIO_PORTS);
	ext_note_ports=(const clap_plugin_note_ports_t*)plugin->get_extension(plugin,CLAP_EXT_NOTE_PORTS);
	ext_timer=(const clap_plugin_timer_support_t*)plugin->get_extension(plugin,CLAP_EXT_TIMER_SUPPORT);
	ext_fd=(const clap_plugin_posix_fd_support_t*)plugin->get_extension(plugin,CLAP_EXT_POSIX_FD_SUPPORT);

	scan_ports();
	scan_params();

	uniqueID=id_hash(desc->id);
	version=1;
	programChunks=true;
	numPrograms=1;
	isSynth=has_feature(desc,CLAP_PLUGIN_FEATURE_INSTRUMENT) || note_in_port>=0;
	hasEditor=ext_gui && ext_gui->is_api_supported && ext_gui->is_api_supported(plugin,CLAP_WINDOW_API_X11,false);
	return true;
}

ClapPlugin::~ClapPlugin()
{
	if(!plugin)
		return;

	editorClose();
	setActive(false);

	for(auto& t:timers)
	{
		adxplat::timer_stop(t.first);
		delete t.second;
	}
	timers.clear();
	adxplat::remove_fd_watches(this);
	fds.clear();

	plugin->destroy(plugin);
	plugin=nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ClapPlugin::scan_ports(void)
{
	in_port_channels.clear();
	out_port_channels.clear();
	if(ext_audio_ports)
	{
		for(int dir=0;dir<2;dir++)
		{
			uint32_t const n=ext_audio_ports->count(plugin,dir==0);
			for(uint32_t i=0;i<n;i++)
			{
				clap_audio_port_info_t info;
				memset(&info,0,sizeof(info));
				if(!ext_audio_ports->get(plugin,i,dir==0,&info))
					continue;
				(dir==0?in_port_channels:out_port_channels).push_back(info.channel_count);
			}
		}
	}

	numInputs=0;
	numOutputs=0;
	for(uint32_t c:in_port_channels) numInputs+=(int)c;
	for(uint32_t c:out_port_channels) numOutputs+=(int)c;

	in_buffers.assign(in_port_channels.size(),clap_audio_buffer_t());
	out_buffers.assign(out_port_channels.size(),clap_audio_buffer_t());

	note_in_port=-1;
	note_in_dialects=0;
	note_out=false;
	if(ext_note_ports)
	{
		if(ext_note_ports->count(plugin,true)>0)
		{
			clap_note_port_info_t info;
			memset(&info,0,sizeof(info));
			if(ext_note_ports->get(plugin,0,true,&info))
			{
				note_in_port=0;
				note_in_dialects=info.supported_dialects;
			}
		}
		note_out=ext_note_ports->count(plugin,false)>0;
	}
}

void ClapPlugin::scan_params(void)
{
	params.clear();
	param_by_id.clear();
	if(ext_params)
	{
		uint32_t const n=ext_params->count(plugin);
		for(uint32_t i=0;i<n;i++)
		{
			clap_param_info_t info;
			memset(&info,0,sizeof(info));
			if(!ext_params->get_info(plugin,i,&info))
				continue;
			if(info.flags & CLAP_PARAM_IS_HIDDEN)
				continue;
			Param p;
			p.id=info.id;
			p.name=info.name;
			p.min=info.min_value;
			p.max=info.max_value>info.min_value?info.max_value:info.min_value+1.0;
			p.def=info.default_value;
			p.flags=info.flags;
			p.cookie=info.cookie;
			param_by_id[p.id]=(int)params.size();
			params.push_back(p);
		}
	}
	numParams=(int)params.size();
	param_cache=std::vector<std::atomic<float>>(params.size());
	refresh_param_values();
}

void ClapPlugin::refresh_param_values(void)
{
	for(size_t i=0;i<params.size();i++)
	{
		double v=params[i].def;
		if(ext_params && ext_params->get_value)
			ext_params->get_value(plugin,params[i].id,&v);
		param_cache[i].store(to_normalized(params[i],v));
	}
}

double ClapPlugin::to_value(const Param& p,float n) const
{
	n=n<0.0f?0.0f:(n>1.0f?1.0f:n);
	double v=p.min+double(n)*(p.max-p.min);
	if(p.flags & CLAP_PARAM_IS_STEPPED)
		v=floor(v+0.5);
	return v;
}

float ClapPlugin::to_normalized(const Param& p,double v) const
{
	double n=(v-p.min)/(p.max-p.min);
	return (float)(n<0.0?0.0:(n>1.0?1.0:n));
}

int ClapPlugin::param_index(clap_id id) const
{
	auto it=param_by_id.find(id);
	return it==param_by_id.end()?-1:it->second;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// activation (main thread)
void ClapPlugin::stop_processing_now(void)
{
	// stop_processing belongs to the audio thread: ask the processing thread, if audio does not run do it here
	if(!processing)
		return;
	stop_request=true;
	for(int i=0;i<100 && processing;i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	if(processing)
	{
		busy.lock();
		if(processing)
		{
			tl_audio_thread=true;
			plugin->stop_processing(plugin);
			tl_audio_thread=false;
			processing=false;
		}
		busy.unlock();
	}
	stop_request=false;
}

void ClapPlugin::setSampleRate(double rate)
{
	if(rate<=0.0)
		return;
	if(std::this_thread::get_id()!=g_main_thread)
	{
		deferred_rate=rate;
		return;
	}
	bool const was_active=active;
	if(was_active && rate!=sample_rate)
		setActive(false);
	sample_rate=rate;
	if(was_active && !active)
		setActive(true);
}

void ClapPlugin::setActive(bool a)
{
	if(std::this_thread::get_id()!=g_main_thread)
	{
		// clap activation belongs to the main thread
		deferred_active=a?1:0;
		return;
	}
	if(!plugin || a==active)
		return;

	if(a)
	{
		if(!plugin->activate(plugin,sample_rate,1,MAX_PROCESS_FRAMES))
		{
			fprintf(stderr,"aodix: clap: '%s' could not be activated\n",desc->name);
			return;
		}
		busy.lock();
		active=true;
		busy.unlock();
	}
	else
	{
		stop_processing_now();
		busy.lock();
		active=false;
		busy.unlock();
		plugin->deactivate(plugin);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// parameters
float ClapPlugin::getParameter(int index)
{
	if(index<0 || index>=numParams)
		return 0.0f;
	return param_cache[index].load(std::memory_order_relaxed);
}

void ClapPlugin::setParameter(int index,float value)
{
	if(index<0 || index>=numParams)
		return;
	param_cache[index].store(value<0.0f?0.0f:(value>1.0f?1.0f:value),std::memory_order_relaxed);
	proc_lock.lock();
	proc_pending.push_back({params[index].id,to_value(params[index],value)});
	proc_lock.unlock();
}

void ClapPlugin::getParamName(int index,char* buf)
{
	if(index<0 || index>=numParams)
	{
		buf[0]=0;
		return;
	}
	copy_str(buf,params[index].name.c_str(),31);
}

void ClapPlugin::getParamDisplay(int index,char* buf)
{
	if(index<0 || index>=numParams)
	{
		buf[0]=0;
		return;
	}
	double const v=to_value(params[index],param_cache[index].load(std::memory_order_relaxed));
	char text[256];
	text[0]=0;
	if(ext_params && ext_params->value_to_text && std::this_thread::get_id()==g_main_thread && ext_params->value_to_text(plugin,params[index].id,v,text,sizeof(text)))
		copy_str(buf,text,31);
	else if(params[index].flags & CLAP_PARAM_IS_STEPPED)
		snprintf(buf,32,"%d",(int)lround(v));
	else
		snprintf(buf,32,"%.4g",v);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// state: "ADXCLAP1" + clap state stream
struct ClapStreamBuf
{
	std::vector<char>*	out;
	const char*			in;
	size_t				in_size;
	size_t				in_pos;
};

static int64_t CLAP_ABI ostream_write(const clap_ostream_t* s,const void* buffer,uint64_t size)
{
	ClapStreamBuf* b=(ClapStreamBuf*)s->ctx;
	b->out->insert(b->out->end(),(const char*)buffer,(const char*)buffer+size);
	return (int64_t)size;
}

static int64_t CLAP_ABI istream_read(const clap_istream_t* s,void* buffer,uint64_t size)
{
	ClapStreamBuf* b=(ClapStreamBuf*)s->ctx;
	size_t const n=(size_t)std::min<uint64_t>(size,b->in_size-b->in_pos);
	memcpy(buffer,b->in+b->in_pos,n);
	b->in_pos+=n;
	return (int64_t)n;
}

int ClapPlugin::getChunk(void** data,bool)
{
	chunk.assign("ADXCLAP1","ADXCLAP1"+8);
	if(ext_state)
	{
		ClapStreamBuf b{&chunk,nullptr,0,0};
		clap_ostream_t s{&b,ostream_write};
		if(!ext_state->save(plugin,&s))
			chunk.resize(8);
	}
	*data=chunk.data();
	return (int)chunk.size();
}

void ClapPlugin::setChunk(const void* data,int size,bool)
{
	const char* p=(const char*)data;
	if(!p || size<8 || memcmp(p,"ADXCLAP1",8)!=0 || !ext_state)
		return;
	ClapStreamBuf b{nullptr,p+8,(size_t)(size-8),0};
	clap_istream_t s{&b,istream_read};
	ext_state->load(plugin,&s);
	refresh_param_values();
	host->plugin_update_display(this);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// audio thread
bool CLAP_ABI ClapPlugin::out_push(const clap_output_events_t* l,const clap_event_header_t* e)
{
	ClapPlugin* cp=(ClapPlugin*)l->ctx;
	if(e->space_id!=CLAP_CORE_EVENT_SPACE_ID)
		return true;

	switch(e->type)
	{
	case CLAP_EVENT_PARAM_VALUE:
		{
			const clap_event_param_value_t* pv=(const clap_event_param_value_t*)e;
			int const index=cp->param_index(pv->param_id);
			if(index>=0)
			{
				cp->param_cache[index].store(cp->to_normalized(cp->params[index],pv->value));
				cp->gui_lock.lock();
				if(cp->gui_pending.size()<4096)
					cp->gui_pending.push_back({pv->param_id,pv->value});
				cp->gui_lock.unlock();
			}
			return true;
		}
	case CLAP_EVENT_NOTE_ON:
	case CLAP_EVENT_NOTE_OFF:
		{
			const clap_event_note_t* n=(const clap_event_note_t*)e;
			if(n->key<0 || n->channel<0)
				return true;
			ADX_MIDI_EVENT m;
			memset(&m,0,sizeof(m));
			m.deltaFrames=(int)e->time;
			int const vel=(int)lround(n->velocity*127.0);
			m.midiData[0]=(unsigned char)(((e->type==CLAP_EVENT_NOTE_ON)?0x90:0x80)|(n->channel&15));
			m.midiData[1]=(unsigned char)(n->key&127);
			m.midiData[2]=(unsigned char)(e->type==CLAP_EVENT_NOTE_ON?(vel<1?1:(vel>127?127:vel)):(vel<0?0:(vel>127?127:vel)));
			if(cp->midi_out.size()<1024)
				cp->midi_out.push_back(m);
			return true;
		}
	case CLAP_EVENT_MIDI:
		{
			const clap_event_midi_t* mi=(const clap_event_midi_t*)e;
			ADX_MIDI_EVENT m;
			memset(&m,0,sizeof(m));
			m.deltaFrames=(int)e->time;
			memcpy(m.midiData,mi->data,3);
			if(cp->midi_out.size()<1024)
				cp->midi_out.push_back(m);
			return true;
		}
	}
	return true;
}

void ClapPlugin::process(float** inputs,float** outputs,int frames,const ADX_MIDI_EVENT* events,int num_events)
{
	bool done=false;

	if(frames>0 && (uint32_t)frames<=MAX_PROCESS_FRAMES && busy.try_lock())
	{
		tl_audio_thread=true;

		if(active && stop_request && processing)
		{
			plugin->stop_processing(plugin);
			processing=false;
		}

		if(active && !stop_request && !processing)
			processing=plugin->start_processing(plugin);

		if(active && processing)
		{
			// input events: parameter changes first (time 0), then the midi events of this block (sorted by aodix)
			in_events.clear();
			proc_lock.lock();
			proc_work.swap(proc_pending);
			proc_lock.unlock();
			for(const ParamChange& c:proc_work)
			{
				int const index=param_index(c.id);
				InEvent ev;
				memset(&ev,0,sizeof(ev));
				ev.param.header.size=sizeof(clap_event_param_value_t);
				ev.param.header.type=CLAP_EVENT_PARAM_VALUE;
				ev.param.header.space_id=CLAP_CORE_EVENT_SPACE_ID;
				ev.param.header.time=0;
				ev.param.param_id=c.id;
				ev.param.cookie=index>=0?params[index].cookie:nullptr;
				ev.param.note_id=-1;
				ev.param.port_index=-1;
				ev.param.channel=-1;
				ev.param.key=-1;
				ev.param.value=c.value;
				in_events.push_back(ev);
			}
			proc_work.clear();

			if(note_in_port>=0)
			{
				bool const clap_notes=(note_in_dialects & CLAP_NOTE_DIALECT_CLAP)!=0;
				bool const midi=(note_in_dialects & CLAP_NOTE_DIALECT_MIDI)!=0;
				for(int e=0;e<num_events;e++)
				{
					const ADX_MIDI_EVENT& m=events[e];
					unsigned char const status=m.midiData[0]&0xF0;
					int const ch=m.midiData[0]&0x0F;
					int const d1=m.midiData[1]&0x7F;
					int const d2=m.midiData[2]&0x7F;
					uint32_t const t=(uint32_t)std::max(0,std::min(m.deltaFrames,frames-1));
					bool const is_note=(status==0x90 || status==0x80);

					InEvent ev;
					memset(&ev,0,sizeof(ev));
					ev.header.time=t;
					ev.header.space_id=CLAP_CORE_EVENT_SPACE_ID;

					if(is_note && clap_notes)
					{
						bool const on=(status==0x90 && d2>0);
						ev.note.header.size=sizeof(clap_event_note_t);
						ev.note.header.type=on?CLAP_EVENT_NOTE_ON:CLAP_EVENT_NOTE_OFF;
						ev.note.note_id=-1;
						ev.note.port_index=(int16_t)note_in_port;
						ev.note.channel=(int16_t)ch;
						ev.note.key=(int16_t)d1;
						ev.note.velocity=on?d2/127.0:(status==0x80?d2/127.0:0.5);
						in_events.push_back(ev);
					}
					else if(midi)
					{
						ev.midi.header.size=sizeof(clap_event_midi_t);
						ev.midi.header.type=CLAP_EVENT_MIDI;
						ev.midi.port_index=(uint16_t)note_in_port;
						memcpy(ev.midi.data,m.midiData,3);
						in_events.push_back(ev);
					}
					else if(clap_notes && status==0xB0 && (d1==120 || d1==123))
					{
						// all notes / sounds off (aodix midi panic) for plugins without midi: note off with key wildcard
						ev.note.header.size=sizeof(clap_event_note_t);
						ev.note.header.type=CLAP_EVENT_NOTE_OFF;
						ev.note.note_id=-1;
						ev.note.port_index=(int16_t)note_in_port;
						ev.note.channel=(int16_t)ch;
						ev.note.key=-1;
						ev.note.velocity=0.0;
						in_events.push_back(ev);
					}
				}
			}

			// transport
			clap_event_transport_t tr;
			memset(&tr,0,sizeof(tr));
			tr.header.size=sizeof(tr);
			tr.header.type=CLAP_EVENT_TRANSPORT;
			tr.header.space_id=CLAP_CORE_EVENT_SPACE_ID;
			const ADX_TIME_INFO* ti=host->plugin_time_info();
			if(ti)
			{
				tr.flags=CLAP_TRANSPORT_HAS_TEMPO|CLAP_TRANSPORT_HAS_BEATS_TIMELINE|CLAP_TRANSPORT_HAS_SECONDS_TIMELINE|CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
				if(ti->flags & ADX_TRANSPORT_PLAYING) tr.flags|=CLAP_TRANSPORT_IS_PLAYING;
				if(ti->flags & ADX_TRANSPORT_RECORDING) tr.flags|=CLAP_TRANSPORT_IS_RECORDING;
				if(ti->flags & ADX_TRANSPORT_CYCLE_ACTIVE) tr.flags|=CLAP_TRANSPORT_IS_LOOP_ACTIVE;
				tr.song_pos_beats=(clap_beattime)llround(ti->ppqPos*double(CLAP_BEATTIME_FACTOR));
				tr.song_pos_seconds=(clap_sectime)llround((ti->sampleRate>0.0?ti->samplePos/ti->sampleRate:0.0)*double(CLAP_SECTIME_FACTOR));
				tr.tempo=ti->tempo;
				tr.loop_start_beats=(clap_beattime)llround(ti->cycleStartPos*double(CLAP_BEATTIME_FACTOR));
				tr.loop_end_beats=(clap_beattime)llround(ti->cycleEndPos*double(CLAP_BEATTIME_FACTOR));
				tr.bar_start=(clap_beattime)llround(ti->barStartPos*double(CLAP_BEATTIME_FACTOR));
				int const num=ti->timeSigNumerator>0?ti->timeSigNumerator:4;
				tr.bar_number=(int32_t)(ti->barStartPos/double(num));
				tr.tsig_num=(uint16_t)num;
				tr.tsig_denom=(uint16_t)(ti->timeSigDenominator>0?ti->timeSigDenominator:4);
			}

			// audio buffers (aodix uses flat channel arrays)
			int ch=0;
			for(size_t b=0;b<in_buffers.size();b++)
			{
				in_buffers[b].data32=inputs+ch;
				in_buffers[b].data64=nullptr;
				in_buffers[b].channel_count=in_port_channels[b];
				in_buffers[b].latency=0;
				in_buffers[b].constant_mask=0;
				ch+=(int)in_port_channels[b];
			}
			ch=0;
			for(size_t b=0;b<out_buffers.size();b++)
			{
				out_buffers[b].data32=outputs+ch;
				out_buffers[b].data64=nullptr;
				out_buffers[b].channel_count=out_port_channels[b];
				out_buffers[b].latency=0;
				out_buffers[b].constant_mask=0;
				ch+=(int)out_port_channels[b];
			}

			midi_out.clear();

			clap_process_t pr;
			memset(&pr,0,sizeof(pr));
			pr.steady_time=steady_time;
			pr.frames_count=(uint32_t)frames;
			pr.transport=ti?&tr:nullptr;
			pr.audio_inputs=in_buffers.empty()?nullptr:in_buffers.data();
			pr.audio_outputs=out_buffers.empty()?nullptr:out_buffers.data();
			pr.audio_inputs_count=(uint32_t)in_buffers.size();
			pr.audio_outputs_count=(uint32_t)out_buffers.size();
			pr.in_events=&in_list;
			pr.out_events=&out_list;

			plugin->process(plugin,&pr);
			steady_time+=frames;
			done=true;

			if(!midi_out.empty())
				host->plugin_midi_out(this,midi_out.data(),(int)midi_out.size());
		}

		tl_audio_thread=false;
		busy.unlock();
	}

	if(!done)
		for(int o=0;o<numOutputs;o++)
			memset(outputs[o],0,sizeof(float)*(frames>0?frames:0));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// gui thread housekeeping
void ClapPlugin::idle(void)
{
	if(!plugin)
		return;

	// parameter edits made in the plugin editor -> aodix (automation recording, display)
	gui_lock.lock();
	gui_work.swap(gui_pending);
	gui_lock.unlock();
	for(const ParamChange& c:gui_work)
	{
		int const index=param_index(c.id);
		if(index>=0)
			host->plugin_automate(this,index,to_normalized(params[index],c.value));
	}
	gui_work.clear();

	if(callback_requested.exchange(false))
		plugin->on_main_thread(plugin);

	// activation changes requested from other threads
	double const dr=deferred_rate.exchange(0.0);
	if(dr>0.0)
		setSampleRate(dr);
	int const da=deferred_active.exchange(-1);
	if(da>=0)
		setActive(da!=0);

	// editor requests made from other threads
	uint64_t const rs=pending_resize.exchange(0);
	if(rs && gui_created)
	{
		host_w=(uint32_t)(rs>>32);
		host_h=(uint32_t)(rs&0xFFFFFFFFu);
		host->plugin_resize_editor(this,(int)host_w,(int)host_h);
	}
	if(gui_destroy_requested.exchange(false) && gui_created)
	{
		ext_gui->destroy(plugin);
		gui_created=false;
		gui_open=false;
	}

	if(rescan_params.exchange(false))
	{
		refresh_param_values();
		host->plugin_update_display(this);
	}

	if(restart_requested.exchange(false) && active)
	{
		setActive(false);
		setActive(true);
	}

	// while not processing (audio stopped) parameter changes reach the plugin through flush
	if(!processing && active && ext_params && ext_params->flush)
	{
		proc_lock.lock();
		bool const any=!proc_pending.empty();
		proc_lock.unlock();
		if(any && busy.try_lock())
		{
			proc_lock.lock();
			proc_work.swap(proc_pending);
			proc_lock.unlock();
			in_events.clear();
			for(const ParamChange& c:proc_work)
			{
				InEvent ev;
				memset(&ev,0,sizeof(ev));
				ev.param.header.size=sizeof(clap_event_param_value_t);
				ev.param.header.type=CLAP_EVENT_PARAM_VALUE;
				ev.param.header.space_id=CLAP_CORE_EVENT_SPACE_ID;
				ev.param.param_id=c.id;
				ev.param.note_id=-1;
				ev.param.port_index=-1;
				ev.param.channel=-1;
				ev.param.key=-1;
				ev.param.value=c.value;
				in_events.push_back(ev);
			}
			proc_work.clear();
			midi_out.clear();

			// while the plugin is active, flush belongs to the audio thread: with the busy lock held this thread is it
			tl_audio_thread=true;
			ext_params->flush(plugin,&in_list,&out_list);
			tl_audio_thread=false;
			busy.unlock();
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// editor (embedded x11 window)
bool ClapPlugin::editorGetSize(int* width,int* height)
{
	if(!hasEditor)
		return false;
	if(!gui_created)
	{
		if(!ext_gui->create(plugin,CLAP_WINDOW_API_X11,false))
			return false;
		gui_created=true;
		gui_resizable=-1;
	}
	uint32_t w=0,h=0;
	if(!ext_gui->get_size(plugin,&w,&h) || w==0 || h==0)
		return false;

	// before the editor is opened aodix sizes its window to this: the resulting size message is no user resize.
	// once it is open only the plugin's own requests and real user resizes count (some plugins report a
	// different, unscaled size here than the size they asked for)
	if(!gui_open)
	{
		host_w=w;
		host_h=h;
	}
	*width=(int)w;
	*height=(int)h;
	return true;
}

bool ClapPlugin::editorOpen(unsigned long parent)
{
	if(!hasEditor || !parent)
		return false;
	if(!gui_created)
	{
		int w,h;
		editorGetSize(&w,&h);
	}
	if(!gui_created)
		return false;

	clap_window_t win;
	memset(&win,0,sizeof(win));
	win.api=CLAP_WINDOW_API_X11;
	win.x11=parent;
	if(!ext_gui->set_parent(plugin,&win))
	{
		ext_gui->destroy(plugin);
		gui_created=false;
		return false;
	}
	ext_gui->show(plugin);
	gui_open=true;

	gui_resizable=-1;
	uint32_t w=0,h=0;
	ext_gui->get_size(plugin,&w,&h);
	fprintf(stderr,"aodix: clap: %s: editor %ux%u, %s\n",desc->name,w,h,editorCanResize()?"resizable":"fixed size");
	return true;
}

void ClapPlugin::editorClose(void)
{
	if(!gui_created)
		return;
	ext_gui->hide(plugin);
	ext_gui->destroy(plugin);
	gui_created=false;
	gui_open=false;
	gui_resizable=-1;
}

bool ClapPlugin::editorCanResize(void)
{
	if(!gui_created)
		return false;
	if(gui_resizable<0)
	{
		// resizable only if the plugin says so and its resize hints (if any) allow at least one direction
		bool r=ext_gui->can_resize && ext_gui->can_resize(plugin);
		if(r && ext_gui->get_resize_hints)
		{
			clap_gui_resize_hints_t hints;
			memset(&hints,0,sizeof(hints));
			if(ext_gui->get_resize_hints(plugin,&hints) && !hints.can_resize_horizontally && !hints.can_resize_vertically)
				r=false;
		}
		gui_resizable=r?1:0;
	}
	return gui_resizable==1;
}

void ClapPlugin::editorResized(int width,int height)
{
	if(width<=0 || height<=0)
		return;

	// size changes made by aodix itself (editor opened, plugin asked for a size) are no user resize
	if((uint32_t)width==host_w && (uint32_t)height==host_h)
		return;
	if(!editorCanResize())
		return;

	uint32_t cw=0,ch=0;
	if(ext_gui->get_size(plugin,&cw,&ch) && (int)cw==width && (int)ch==height)
		return;

	// the plugin has to accept the size (adjust_size); set_size is only allowed with an adjusted size
	uint32_t w=(uint32_t)width,h=(uint32_t)height;
	if(!ext_gui->adjust_size || !ext_gui->adjust_size(plugin,&w,&h))
		return;
	if(w==0 || h==0)
		return;

	host_w=w;
	host_h=h;
	ext_gui->set_size(plugin,w,h);
	if((int)w!=width || (int)h!=height)
		host->plugin_resize_editor(this,(int)w,(int)h);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// library functions
int clap_plugin_scan(const char* path,std::vector<AdxPluginDesc>& out)
{
	std::string error;
	const ClapModule* m=clap_open(path,error);
	if(!m)
	{
		fprintf(stderr,"aodix: clap: %s: %s\n",path,error.c_str());
		return 0;
	}

	int count=0;
	uint32_t const n=m->factory->get_plugin_count(m->factory);
	for(uint32_t i=0;i<n;i++)
	{
		const clap_plugin_descriptor_t* d=m->factory->get_plugin_descriptor(m->factory,i);
		if(!d || !d->id)
			continue;
		out.push_back({path,d->name?d->name:d->id,id_hash(d->id),has_feature(d,CLAP_PLUGIN_FEATURE_INSTRUMENT)});
		count++;
	}
	return count;
}

AdxPlugin* clap_plugin_load(const char* path,int32_t wanted_id,AdxPluginHost* host,char* error,int error_size)
{
	std::string err;
	const ClapModule* m=clap_open(path,err);
	if(!m)
	{
		snprintf(error,error_size,"%s",err.c_str());
		return nullptr;
	}

	// matching id, otherwise the first instrument, otherwise the first plugin
	const clap_plugin_descriptor_t* chosen=nullptr;
	const clap_plugin_descriptor_t* first=nullptr;
	const clap_plugin_descriptor_t* first_instrument=nullptr;
	uint32_t const n=m->factory->get_plugin_count(m->factory);
	for(uint32_t i=0;i<n;i++)
	{
		const clap_plugin_descriptor_t* d=m->factory->get_plugin_descriptor(m->factory,i);
		if(!d || !d->id)
			continue;
		if(!first)
			first=d;
		if(!first_instrument && has_feature(d,CLAP_PLUGIN_FEATURE_INSTRUMENT))
			first_instrument=d;
		if(wanted_id!=0 && id_hash(d->id)==wanted_id)
		{
			chosen=d;
			break;
		}
	}
	if(!chosen)
		chosen=first_instrument?first_instrument:first;
	if(!chosen)
	{
		snprintf(error,error_size,"The CLAP file contains no plugin.");
		return nullptr;
	}

	ClapPlugin* p=new ClapPlugin(m,chosen,host);
	if(!p->init(error,error_size))
	{
		delete p;
		return nullptr;
	}
	return p;
}
