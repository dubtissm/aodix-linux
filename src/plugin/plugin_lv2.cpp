/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - LV2 plugin hosting
//
// LV2 plugins are bundles (directories *.lv2) described in RDF/Turtle. The descriptions are read with lilv, which is loaded at
// runtime (liblilv-0.so.0, ISC licence) - like libjack there is no build dependency, and without lilv there is simply no LV2.
// Plugins live in the standard places (LV2_PATH or ~/.lv2, /usr/local/lib/lv2, /usr/lib/lv2), there is no folder setting.
//
// Projects store the bundle path and a 32-bit hash of the plugin URI; the plugin is found again by its URI hash.
//
// Host features: urid map/unmap, options, bounded block length, worker, log, state (lilv), time position, X11 UIs
// (ui:parent, ui:resize, idle interface, instance/data access, port map).
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AdxPlugin.h"
#include "../platform/adxplat_runloop.h"

namespace adxplat { void* x11_display(void); }

#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/data-access/data-access.h>
#include <lv2/instance-access/instance-access.h>
#include <lv2/log/log.h>
#include <lv2/midi/midi.h>
#include <lv2/options/options.h>
#include <lv2/parameters/parameters.h>
#include <lv2/port-props/port-props.h>
#include <lv2/resize-port/resize-port.h>
#include <lv2/state/state.h>
#include <lv2/time/time.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>

#include <X11/Xlib.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <dlfcn.h>
#include <semaphore.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// lilv (declared here, loaded at runtime; the api is stable since 0.24)
typedef void LilvWorld;
typedef void LilvPlugin;
typedef void LilvPlugins;
typedef void LilvPluginClass;
typedef void LilvPort;
typedef void LilvNode;
typedef void LilvNodes;
typedef void LilvState;
typedef void LilvUI;
typedef void LilvUIs;
typedef void LilvIter;

struct LilvInstance
{
	const LV2_Descriptor*	lv2_descriptor;
	LV2_Handle				lv2_handle;
	void*					pimpl;
};

typedef const void* (*LilvGetPortValueFunc)(const char* port_symbol,void* user_data,uint32_t* size,uint32_t* type);
typedef void (*LilvSetPortValueFunc)(const char* port_symbol,void* user_data,const void* value,uint32_t size,uint32_t type);

static struct
{
	void*					lib;
	bool					tried;

	LilvWorld*				(*world_new)(void);
	void					(*world_load_all)(LilvWorld*);
	const LilvPlugins*		(*world_get_all_plugins)(const LilvWorld*);
	LilvIter*				(*plugins_begin)(const LilvPlugins*);
	const LilvPlugin*		(*plugins_get)(const LilvPlugins*,LilvIter*);
	LilvIter*				(*plugins_next)(const LilvPlugins*,LilvIter*);
	bool					(*plugins_is_end)(const LilvPlugins*,LilvIter*);
	const LilvNode*			(*plugin_get_uri)(const LilvPlugin*);
	const LilvNode*			(*plugin_get_bundle_uri)(const LilvPlugin*);
	LilvNode*				(*plugin_get_name)(const LilvPlugin*);
	const LilvPluginClass*	(*plugin_get_class)(const LilvPlugin*);
	const LilvNode*			(*plugin_class_get_uri)(const LilvPluginClass*);
	uint32_t				(*plugin_get_num_ports)(const LilvPlugin*);
	const LilvPort*			(*plugin_get_port_by_index)(const LilvPlugin*,uint32_t);
	LilvNodes*				(*plugin_get_required_features)(const LilvPlugin*);
	LilvNodes*				(*plugin_get_value)(const LilvPlugin*,const LilvNode*);
	LilvUIs*				(*plugin_get_uis)(const LilvPlugin*);
	LilvInstance*			(*plugin_instantiate)(const LilvPlugin*,double,const LV2_Feature* const*);
	void					(*instance_free)(LilvInstance*);
	bool					(*port_is_a)(const LilvPlugin*,const LilvPort*,const LilvNode*);
	LilvNode*				(*port_get_name)(const LilvPlugin*,const LilvPort*);
	const LilvNode*			(*port_get_symbol)(const LilvPlugin*,const LilvPort*);
	void					(*port_get_range)(const LilvPlugin*,const LilvPort*,LilvNode**,LilvNode**,LilvNode**);
	bool					(*port_has_property)(const LilvPlugin*,const LilvPort*,const LilvNode*);
	bool					(*port_supports_event)(const LilvPlugin*,const LilvPort*,const LilvNode*);
	LilvNodes*				(*port_get_value)(const LilvPlugin*,const LilvPort*,const LilvNode*);
	LilvNode*				(*new_uri)(LilvWorld*,const char*);
	void					(*node_free)(LilvNode*);
	const char*				(*node_as_uri)(const LilvNode*);
	const char*				(*node_as_string)(const LilvNode*);
	float					(*node_as_float)(const LilvNode*);
	int						(*node_as_int)(const LilvNode*);
	bool					(*node_is_float)(const LilvNode*);
	bool					(*node_is_int)(const LilvNode*);
	bool					(*node_is_bool)(const LilvNode*);
	bool					(*node_as_bool)(const LilvNode*);
	bool					(*node_equals)(const LilvNode*,const LilvNode*);
	LilvIter*				(*nodes_begin)(const LilvNodes*);
	const LilvNode*			(*nodes_get)(const LilvNodes*,LilvIter*);
	LilvIter*				(*nodes_next)(const LilvNodes*,LilvIter*);
	bool					(*nodes_is_end)(const LilvNodes*,LilvIter*);
	void					(*nodes_free)(LilvNodes*);
	LilvIter*				(*uis_begin)(const LilvUIs*);
	const LilvUI*			(*uis_get)(const LilvUIs*,LilvIter*);
	LilvIter*				(*uis_next)(const LilvUIs*,LilvIter*);
	bool					(*uis_is_end)(const LilvUIs*,LilvIter*);
	void					(*uis_free)(LilvUIs*);
	const LilvNode*			(*ui_get_uri)(const LilvUI*);
	bool					(*ui_is_a)(const LilvUI*,const LilvNode*);
	const LilvNode*			(*ui_get_bundle_uri)(const LilvUI*);
	const LilvNode*			(*ui_get_binary_uri)(const LilvUI*);
	char*					(*file_uri_parse)(const char*,char**);
	void					(*free_)(void*);
	LilvState*				(*state_new_from_instance)(const LilvPlugin*,LilvInstance*,LV2_URID_Map*,const char*,const char*,const char*,const char*,LilvGetPortValueFunc,void*,uint32_t,const LV2_Feature* const*);
	char*					(*state_to_string)(LilvWorld*,LV2_URID_Map*,LV2_URID_Unmap*,const LilvState*,const char*,const char*);
	LilvState*				(*state_new_from_string)(LilvWorld*,LV2_URID_Map*,const char*);
	void					(*state_restore)(const LilvState*,LilvInstance*,LilvSetPortValueFunc,void*,uint32_t,const LV2_Feature* const*);
	void					(*state_free)(LilvState*);
} lilv;

static bool lilv_load(void)
{
	if(lilv.tried)
		return lilv.lib!=nullptr;
	lilv.tried=true;

	const char* const names[]={"liblilv-0.so.0","liblilv-0.so"};
	for(const char* n:names)
	{
		lilv.lib=dlopen(n,RTLD_NOW|RTLD_LOCAL);
		if(lilv.lib)
			break;
	}
	if(!lilv.lib)
	{
		fprintf(stderr,"aodix: lv2: liblilv-0.so.0 not found, LV2 plugins are not available (install lilv / liblilv-0-0)\n");
		return false;
	}

	bool ok=true;
	auto sym=[&](void** fn,const char* name)
	{
		*fn=dlsym(lilv.lib,name);
		if(!*fn)
		{
			fprintf(stderr,"aodix: lv2: lilv is missing '%s'\n",name);
			ok=false;
		}
	};
#define LILV_SYM(field,name) sym((void**)&lilv.field,name)
	LILV_SYM(world_new,"lilv_world_new");
	LILV_SYM(world_load_all,"lilv_world_load_all");
	LILV_SYM(world_get_all_plugins,"lilv_world_get_all_plugins");
	LILV_SYM(plugins_begin,"lilv_plugins_begin");
	LILV_SYM(plugins_get,"lilv_plugins_get");
	LILV_SYM(plugins_next,"lilv_plugins_next");
	LILV_SYM(plugins_is_end,"lilv_plugins_is_end");
	LILV_SYM(plugin_get_uri,"lilv_plugin_get_uri");
	LILV_SYM(plugin_get_bundle_uri,"lilv_plugin_get_bundle_uri");
	LILV_SYM(plugin_get_name,"lilv_plugin_get_name");
	LILV_SYM(plugin_get_class,"lilv_plugin_get_class");
	LILV_SYM(plugin_class_get_uri,"lilv_plugin_class_get_uri");
	LILV_SYM(plugin_get_num_ports,"lilv_plugin_get_num_ports");
	LILV_SYM(plugin_get_port_by_index,"lilv_plugin_get_port_by_index");
	LILV_SYM(plugin_get_required_features,"lilv_plugin_get_required_features");
	LILV_SYM(plugin_get_value,"lilv_plugin_get_value");
	LILV_SYM(plugin_get_uis,"lilv_plugin_get_uis");
	LILV_SYM(plugin_instantiate,"lilv_plugin_instantiate");
	LILV_SYM(instance_free,"lilv_instance_free");
	LILV_SYM(port_is_a,"lilv_port_is_a");
	LILV_SYM(port_get_name,"lilv_port_get_name");
	LILV_SYM(port_get_symbol,"lilv_port_get_symbol");
	LILV_SYM(port_get_range,"lilv_port_get_range");
	LILV_SYM(port_has_property,"lilv_port_has_property");
	LILV_SYM(port_supports_event,"lilv_port_supports_event");
	LILV_SYM(port_get_value,"lilv_port_get_value");
	LILV_SYM(new_uri,"lilv_new_uri");
	LILV_SYM(node_free,"lilv_node_free");
	LILV_SYM(node_as_uri,"lilv_node_as_uri");
	LILV_SYM(node_as_string,"lilv_node_as_string");
	LILV_SYM(node_as_float,"lilv_node_as_float");
	LILV_SYM(node_as_int,"lilv_node_as_int");
	LILV_SYM(node_is_float,"lilv_node_is_float");
	LILV_SYM(node_is_int,"lilv_node_is_int");
	LILV_SYM(node_is_bool,"lilv_node_is_bool");
	LILV_SYM(node_as_bool,"lilv_node_as_bool");
	LILV_SYM(node_equals,"lilv_node_equals");
	LILV_SYM(nodes_begin,"lilv_nodes_begin");
	LILV_SYM(nodes_get,"lilv_nodes_get");
	LILV_SYM(nodes_next,"lilv_nodes_next");
	LILV_SYM(nodes_is_end,"lilv_nodes_is_end");
	LILV_SYM(nodes_free,"lilv_nodes_free");
	LILV_SYM(uis_begin,"lilv_uis_begin");
	LILV_SYM(uis_get,"lilv_uis_get");
	LILV_SYM(uis_next,"lilv_uis_next");
	LILV_SYM(uis_is_end,"lilv_uis_is_end");
	LILV_SYM(uis_free,"lilv_uis_free");
	LILV_SYM(ui_get_uri,"lilv_ui_get_uri");
	LILV_SYM(ui_is_a,"lilv_ui_is_a");
	LILV_SYM(ui_get_bundle_uri,"lilv_ui_get_bundle_uri");
	LILV_SYM(ui_get_binary_uri,"lilv_ui_get_binary_uri");
	LILV_SYM(file_uri_parse,"lilv_file_uri_parse");
	LILV_SYM(free_,"lilv_free");
	LILV_SYM(state_new_from_instance,"lilv_state_new_from_instance");
	LILV_SYM(state_to_string,"lilv_state_to_string");
	LILV_SYM(state_new_from_string,"lilv_state_new_from_string");
	LILV_SYM(state_restore,"lilv_state_restore");
	LILV_SYM(state_free,"lilv_state_free");
#undef LILV_SYM

	if(!ok)
	{
		dlclose(lilv.lib);
		lilv.lib=nullptr;
	}
	return ok;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// helpers
static int32_t uri_hash(const char* s)
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

static std::string uri_to_path(const char* uri)
{
	char* p=lilv.file_uri_parse(uri,nullptr);
	std::string s=p?p:"";
	if(p)
		lilv.free_(p);
	while(s.size()>1 && s.back()=='/')
		s.pop_back();
	return s;
}

class SpinLock
{
public:
	void lock(void) { while(flag.test_and_set(std::memory_order_acquire)) {} }
	bool try_lock(void) { return !flag.test_and_set(std::memory_order_acquire); }
	void unlock(void) { flag.clear(std::memory_order_release); }
private:
	std::atomic_flag flag=ATOMIC_FLAG_INIT;
};

// single producer / single consumer byte ring for variable size messages (uint32 size + data)
class MsgRing
{
public:
	explicit MsgRing(size_t capacity):buf(capacity),wpos(0),rpos(0) {}
	bool write(const void* data,uint32_t size)
	{
		size_t const need=4+size;
		size_t const w=wpos.load(std::memory_order_relaxed);
		size_t const r=rpos.load(std::memory_order_acquire);
		if(buf.size()-(w-r)<need)
			return false;
		put(w,&size,4);
		put(w+4,data,size);
		wpos.store(w+need,std::memory_order_release);
		return true;
	}
	// returns false if empty
	bool read(std::vector<uint8_t>& out)
	{
		size_t const r=rpos.load(std::memory_order_relaxed);
		size_t const w=wpos.load(std::memory_order_acquire);
		if(w==r)
			return false;
		uint32_t size=0;
		get(r,&size,4);
		out.resize(size);
		get(r+4,out.data(),size);
		rpos.store(r+4+size,std::memory_order_release);
		return true;
	}
private:
	void put(size_t pos,const void* d,size_t n) { for(size_t i=0;i<n;i++) buf[(pos+i)%buf.size()]=((const uint8_t*)d)[i]; }
	void get(size_t pos,void* d,size_t n) { for(size_t i=0;i<n;i++) ((uint8_t*)d)[i]=buf[(pos+i)%buf.size()]; }
	std::vector<uint8_t>	buf;
	std::atomic<size_t>		wpos;
	std::atomic<size_t>		rpos;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// urid map (shared by all plugins)
static std::mutex g_urid_mutex;
static std::unordered_map<std::string,LV2_URID> g_urids;
static std::vector<std::string> g_uri_strings(1);

static LV2_URID urid_map(LV2_URID_Map_Handle,const char* uri)
{
	std::lock_guard<std::mutex> lock(g_urid_mutex);
	auto it=g_urids.find(uri);
	if(it!=g_urids.end())
		return it->second;
	LV2_URID const id=(LV2_URID)g_uri_strings.size();
	g_uri_strings.push_back(uri);
	g_urids[uri]=id;
	return id;
}

static const char* urid_unmap(LV2_URID_Unmap_Handle,LV2_URID urid)
{
	std::lock_guard<std::mutex> lock(g_urid_mutex);
	return (urid>0 && urid<g_uri_strings.size())?g_uri_strings[urid].c_str():nullptr;
}

static LV2_URID_Map		g_map={nullptr,urid_map};
static LV2_URID_Unmap	g_unmap={nullptr,urid_unmap};

static struct
{
	LV2_URID atom_Sequence,atom_Chunk,atom_Float,atom_Double,atom_Int,atom_Long,atom_Bool,atom_Object,atom_eventTransfer;
	LV2_URID midi_Event;
	LV2_URID time_Position,time_frame,time_speed,time_bpm,time_bar,time_barBeat,time_beatUnit,time_beatsPerBar;
	LV2_URID bufsz_minBlockLength,bufsz_maxBlockLength,bufsz_nominalBlockLength,bufsz_sequenceSize;
	LV2_URID param_sampleRate,ui_updateRate,ui_scaleFactor;
	LV2_URID log_Error,log_Warning,log_Note,log_Trace;
} U;

static void init_urids(void)
{
	static bool done=false;
	if(done)
		return;
	done=true;
	U.atom_Sequence=urid_map(nullptr,LV2_ATOM__Sequence);
	U.atom_Chunk=urid_map(nullptr,LV2_ATOM__Chunk);
	U.atom_Float=urid_map(nullptr,LV2_ATOM__Float);
	U.atom_Double=urid_map(nullptr,LV2_ATOM__Double);
	U.atom_Int=urid_map(nullptr,LV2_ATOM__Int);
	U.atom_Long=urid_map(nullptr,LV2_ATOM__Long);
	U.atom_Bool=urid_map(nullptr,LV2_ATOM__Bool);
	U.atom_Object=urid_map(nullptr,LV2_ATOM__Object);
	U.atom_eventTransfer=urid_map(nullptr,LV2_ATOM__eventTransfer);
	U.midi_Event=urid_map(nullptr,LV2_MIDI__MidiEvent);
	U.time_Position=urid_map(nullptr,LV2_TIME__Position);
	U.time_frame=urid_map(nullptr,LV2_TIME__frame);
	U.time_speed=urid_map(nullptr,LV2_TIME__speed);
	U.time_bpm=urid_map(nullptr,LV2_TIME__beatsPerMinute);
	U.time_bar=urid_map(nullptr,LV2_TIME__bar);
	U.time_barBeat=urid_map(nullptr,LV2_TIME__barBeat);
	U.time_beatUnit=urid_map(nullptr,LV2_TIME__beatUnit);
	U.time_beatsPerBar=urid_map(nullptr,LV2_TIME__beatsPerBar);
	U.bufsz_minBlockLength=urid_map(nullptr,LV2_BUF_SIZE__minBlockLength);
	U.bufsz_maxBlockLength=urid_map(nullptr,LV2_BUF_SIZE__maxBlockLength);
	U.bufsz_nominalBlockLength=urid_map(nullptr,LV2_BUF_SIZE__nominalBlockLength);
	U.bufsz_sequenceSize=urid_map(nullptr,LV2_BUF_SIZE__sequenceSize);
	U.param_sampleRate=urid_map(nullptr,LV2_PARAMETERS__sampleRate);
	U.ui_updateRate=urid_map(nullptr,LV2_UI__updateRate);
	U.ui_scaleFactor=urid_map(nullptr,LV2_UI__scaleFactor);
	U.log_Error=urid_map(nullptr,LV2_LOG__Error);
	U.log_Warning=urid_map(nullptr,LV2_LOG__Warning);
	U.log_Note=urid_map(nullptr,LV2_LOG__Note);
	U.log_Trace=urid_map(nullptr,LV2_LOG__Trace);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// lilv world (gui thread only, created once and kept: plugin descriptions stay valid while instances exist)
static LilvWorld* g_world=nullptr;
static struct
{
	LilvNode *lv2_AudioPort,*lv2_ControlPort,*lv2_CVPort,*lv2_InputPort,*lv2_OutputPort,*atom_AtomPort;
	LilvNode *lv2_toggled,*lv2_integer,*lv2_sampleRate,*lv2_enumeration,*pprops_logarithmic,*lv2_reportsLatency;
	LilvNode *lv2_designation,*lv2_freeWheeling,*lv2_InstrumentPlugin,*midi_MidiEvent,*time_Position,*rsz_minimumSize;
	LilvNode *ui_X11UI,*lv2_connectionOptional;
} N;

static LilvWorld* lv2_world(void)
{
	if(g_world)
		return g_world;
	if(!lilv_load())
		return nullptr;
	init_urids();

	g_world=lilv.world_new();
	lilv.world_load_all(g_world);

	N.lv2_AudioPort=lilv.new_uri(g_world,LV2_CORE__AudioPort);
	N.lv2_ControlPort=lilv.new_uri(g_world,LV2_CORE__ControlPort);
	N.lv2_CVPort=lilv.new_uri(g_world,LV2_CORE__CVPort);
	N.lv2_InputPort=lilv.new_uri(g_world,LV2_CORE__InputPort);
	N.lv2_OutputPort=lilv.new_uri(g_world,LV2_CORE__OutputPort);
	N.atom_AtomPort=lilv.new_uri(g_world,LV2_ATOM__AtomPort);
	N.lv2_toggled=lilv.new_uri(g_world,LV2_CORE__toggled);
	N.lv2_integer=lilv.new_uri(g_world,LV2_CORE__integer);
	N.lv2_sampleRate=lilv.new_uri(g_world,LV2_CORE__sampleRate);
	N.lv2_enumeration=lilv.new_uri(g_world,LV2_CORE__enumeration);
	N.pprops_logarithmic=lilv.new_uri(g_world,LV2_PORT_PROPS__logarithmic);
	N.lv2_reportsLatency=lilv.new_uri(g_world,LV2_CORE__reportsLatency);
	N.lv2_designation=lilv.new_uri(g_world,LV2_CORE__designation);
	N.lv2_freeWheeling=lilv.new_uri(g_world,LV2_CORE__freeWheeling);
	N.lv2_InstrumentPlugin=lilv.new_uri(g_world,LV2_CORE__InstrumentPlugin);
	N.midi_MidiEvent=lilv.new_uri(g_world,LV2_MIDI__MidiEvent);
	N.time_Position=lilv.new_uri(g_world,LV2_TIME__Position);
	N.rsz_minimumSize=lilv.new_uri(g_world,LV2_RESIZE_PORT__minimumSize);
	N.ui_X11UI=lilv.new_uri(g_world,LV2_UI__X11UI);
	N.lv2_connectionOptional=lilv.new_uri(g_world,LV2_CORE__connectionOptional);
	return g_world;
}

template<typename F> static void for_each_plugin(F f)
{
	LilvWorld* w=lv2_world();
	if(!w)
		return;
	const LilvPlugins* plugins=lilv.world_get_all_plugins(w);
	for(LilvIter* i=lilv.plugins_begin(plugins);!lilv.plugins_is_end(plugins,i);i=lilv.plugins_next(plugins,i))
		if(!f(lilv.plugins_get(plugins,i)))
			break;
}

static bool plugin_is_instrument(const LilvPlugin* p)
{
	const LilvNode* cls=lilv.plugin_class_get_uri(lilv.plugin_get_class(p));
	return cls && lilv.node_equals(cls,N.lv2_InstrumentPlugin);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// features supported by the host
static const char* const g_supported_features[]=
{
	LV2_URID__map,LV2_URID__unmap,LV2_OPTIONS__options,LV2_BUF_SIZE__boundedBlockLength,LV2_WORKER__schedule,
	LV2_LOG__log,LV2_CORE__isLive,LV2_CORE__inPlaceBroken,LV2_CORE__hardRTCapable,LV2_STATE__loadDefaultState,
	LV2_STATE__threadSafeRestore,"http://lv2plug.in/ns/ext/buf-size#coarseBlockLength",nullptr
};

static bool feature_supported(const char* uri)
{
	for(const char* const* f=g_supported_features;*f;f++)
		if(!strcmp(*f,uri))
			return true;
	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// worker thread (shared): runs the plugins' scheduled work outside the audio thread
class Lv2Plugin;
static std::mutex				g_worker_mutex;		// protects the plugin list and serializes work()
static std::vector<Lv2Plugin*>	g_worker_plugins;
static sem_t					g_worker_sem;
static std::thread				g_worker_thread;
static bool						g_worker_started=false;
static void worker_main(void);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static const int		MAX_PROCESS_FRAMES=8192;
static const uint32_t	ATOM_BUFFER_SIZE=32768;

class Lv2Plugin : public AdxPlugin
{
public:
	Lv2Plugin(const LilvPlugin* p,AdxPluginHost* h);
	~Lv2Plugin() override;

	bool		init(char* error,int error_size);

	const char*	format(void) override { return "LV2"; }
	void		getEffectName(char* buf) override { copy_str(buf,name.c_str(),64); }

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
	void		setProgramName(const char* n) override { program_name=n?n:""; }
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

	// worker (called by the shared worker thread with g_worker_mutex held)
	void		do_work(void);

private:
	enum PortType { PORT_OTHER, PORT_AUDIO_IN, PORT_AUDIO_OUT, PORT_CONTROL_IN, PORT_CONTROL_OUT, PORT_ATOM_IN, PORT_ATOM_OUT, PORT_CV_IN, PORT_CV_OUT };

	struct Port
	{
		PortType				type;
		std::string				symbol;
		std::string				name;
		float					value;				// control port memory (read / written by the plugin, audio thread)
		float					state_value;		// value handed to lilv when the state is saved (gui thread)
		float					def,min,max;
		bool					toggled,integer,logarithmic,sample_rate,hidden;
		bool					midi,time;			// atom input supports midi / time position
		std::vector<uint8_t>	atom;				// atom port buffer (8-byte aligned through uint64 storage below)
		std::vector<uint64_t>	atom_storage;
		int						param;				// aodix parameter index (control inputs)
	};

	struct ParamChange { uint32_t port; float value; };

	float		to_value(const Port& p,float n) const;
	float		to_normalized(const Port& p,float v) const;
	bool		instantiate(void);
	void		release(void);
	LV2_Atom_Sequence* seq(Port& p) { return (LV2_Atom_Sequence*)p.atom_storage.data(); }

	// host features
	static LV2_Worker_Status worker_schedule(LV2_Worker_Schedule_Handle h,uint32_t size,const void* data);
	static LV2_Worker_Status worker_respond(LV2_Worker_Respond_Handle h,uint32_t size,const void* data);
	static int log_printf(LV2_Log_Handle h,LV2_URID type,const char* fmt,...);
	static int log_vprintf(LV2_Log_Handle h,LV2_URID type,const char* fmt,va_list ap);
	static const void* get_port_value(const char* symbol,void* user,uint32_t* size,uint32_t* type);
	static void set_port_value(const char* symbol,void* user,const void* value,uint32_t size,uint32_t type);

	// ui
	static void ui_write(LV2UI_Controller c,uint32_t port,uint32_t size,uint32_t protocol,const void* buffer);
	static int ui_resize(LV2UI_Feature_Handle h,int w,int hh);
	static uint32_t ui_port_index(LV2UI_Feature_Handle h,const char* symbol);
	static void ui_idle_cb(void* user);
	bool		find_ui(void);

	const LilvPlugin*				plugin;
	AdxPluginHost*					host;
	std::string						uri;
	std::string						name;
	std::string						program_name;
	LilvInstance*					instance;
	double							sample_rate;
	bool							active;
	SpinLock						busy;

	std::vector<Port>				ports;
	std::vector<int>				audio_in,audio_out,control_in;
	int								midi_in_port;
	int								time_port;
	std::vector<float>				silence,scratch;

	std::vector<std::atomic<float>>	target;			// control input values set by aodix / the ui (per parameter)
	std::vector<std::atomic<float>>	outputs_seen;	// control output values after the last run (per port, read by the ui)
	SpinLock						atom_lock;		// ui -> plugin atom messages
	std::vector<uint8_t>			atom_pending;	// [uint32 port][uint32 size][LV2_Atom ...]
	std::vector<uint8_t>			atom_work;
	std::vector<ADX_MIDI_EVENT>		midi_out;

	// time position (audio thread)
	bool							last_playing;
	double							last_bpm;
	double							expected_frame;

	// features
	LV2_Options_Option				options[8];
	int32_t							opt_min_block,opt_max_block,opt_nominal_block,opt_seq_size;
	float							opt_sample_rate,opt_update_rate,opt_scale;
	LV2_Worker_Schedule				worker_sched;
	LV2_Log_Log						log;
	LV2_Feature						f_map,f_unmap,f_options,f_bounded,f_worker,f_log;
	const LV2_Feature*				features[8];

	// worker
	const LV2_Worker_Interface*		worker_iface;
	MsgRing							work_requests;
	MsgRing							work_responses;
	std::vector<uint8_t>			work_buf;

	// ui (gui thread)
	std::string						ui_uri,ui_bundle,ui_binary;
	const LV2UI_Descriptor*			ui_desc;
	LV2UI_Handle					ui_handle;
	LV2UI_Widget					ui_widget;
	const LV2UI_Idle_Interface*		ui_idle_iface;
	const LV2UI_Resize*				ui_resize_iface;
	unsigned long					ui_timer;
	int								ui_w,ui_h;
	std::vector<std::pair<int,int>>	host_sizes;		// sizes aodix gives the editor frame itself (reported / requested), no user resize
	unsigned long					ui_parent;
	std::vector<float>				ui_sent;		// last control values sent to the ui, per port
	MsgRing							ui_events;		// plugin atom outputs -> ui
	SpinLock						ui_auto_lock;
	std::vector<ParamChange>		ui_automate;	// ui parameter edits -> aodix (automation)
	bool							ui_open;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Lv2Plugin::Lv2Plugin(const LilvPlugin* p,AdxPluginHost* h)
	:work_requests(1<<16),work_responses(1<<16),ui_events(1<<18)
{
	plugin=p;
	host=h;
	instance=nullptr;
	sample_rate=48000.0;
	active=false;
	program_name="Default";
	midi_in_port=-1;
	time_port=-1;
	last_playing=false;
	last_bpm=0.0;
	expected_frame=-1.0;
	worker_iface=nullptr;
	ui_desc=nullptr;
	ui_handle=nullptr;
	ui_widget=nullptr;
	ui_idle_iface=nullptr;
	ui_resize_iface=nullptr;
	ui_timer=0;
	ui_w=ui_h=0;
	ui_parent=0;
	ui_open=false;

	uri=lilv.node_as_uri(lilv.plugin_get_uri(p));
	LilvNode* n=lilv.plugin_get_name(p);
	name=n?lilv.node_as_string(n):uri;
	if(n)
		lilv.node_free(n);

	silence.assign(MAX_PROCESS_FRAMES,0.0f);
	scratch.assign(MAX_PROCESS_FRAMES,0.0f);
	atom_pending.reserve(65536);
	atom_work.reserve(65536);
	midi_out.reserve(1024);
	ui_automate.reserve(256);
	work_buf.reserve(1<<16);
}

bool Lv2Plugin::init(char* error,int error_size)
{
	// required features
	LilvNodes* req=lilv.plugin_get_required_features(plugin);
	if(req)
	{
		std::string missing;
		for(LilvIter* i=lilv.nodes_begin(req);!lilv.nodes_is_end(req,i);i=lilv.nodes_next(req,i))
		{
			const char* f=lilv.node_as_uri(lilv.nodes_get(req,i));
			if(f && !feature_supported(f))
				missing+=std::string("\n")+f;
		}
		lilv.nodes_free(req);
		if(!missing.empty())
		{
			snprintf(error,error_size,"The LV2 plugin '%s' needs host features Aodix does not provide:%s",name.c_str(),missing.c_str());
			return false;
		}
	}

	// ports
	uint32_t const np=lilv.plugin_get_num_ports(plugin);
	ports.resize(np);
	int num_params=0;
	for(uint32_t i=0;i<np;i++)
	{
		const LilvPort* lp=lilv.plugin_get_port_by_index(plugin,i);
		Port& p=ports[i];
		p.type=PORT_OTHER;
		p.value=0.0f;
		p.def=0.0f;
		p.state_value=0.0f;
		p.min=0.0f;
		p.max=1.0f;
		p.toggled=p.integer=p.logarithmic=p.sample_rate=p.hidden=false;
		p.midi=p.time=false;
		p.param=-1;
		p.symbol=lilv.node_as_string(lilv.port_get_symbol(plugin,lp));
		LilvNode* pn=lilv.port_get_name(plugin,lp);
		p.name=pn?lilv.node_as_string(pn):p.symbol;
		if(pn)
			lilv.node_free(pn);

		bool const in=lilv.port_is_a(plugin,lp,N.lv2_InputPort);
		bool const out=lilv.port_is_a(plugin,lp,N.lv2_OutputPort);

		if(lilv.port_is_a(plugin,lp,N.lv2_AudioPort))
		{
			p.type=in?PORT_AUDIO_IN:PORT_AUDIO_OUT;
			(in?audio_in:audio_out).push_back((int)i);
		}
		else if(lilv.port_is_a(plugin,lp,N.lv2_ControlPort))
		{
			p.type=in?PORT_CONTROL_IN:PORT_CONTROL_OUT;
			LilvNode *d=nullptr,*mn=nullptr,*mx=nullptr;
			lilv.port_get_range(plugin,lp,&d,&mn,&mx);
			auto num=[](const LilvNode* nd,float fallback)->float
			{
				if(!nd) return fallback;
				if(lilv.node_is_float(nd) || lilv.node_is_int(nd)) return lilv.node_as_float(nd);
				if(lilv.node_is_bool(nd)) return lilv.node_as_bool(nd)?1.0f:0.0f;
				return fallback;
			};
			p.min=num(mn,0.0f);
			p.max=num(mx,1.0f);
			p.def=num(d,p.min);
			if(d) lilv.node_free(d);
			if(mn) lilv.node_free(mn);
			if(mx) lilv.node_free(mx);
			p.toggled=lilv.port_has_property(plugin,lp,N.lv2_toggled);
			p.integer=lilv.port_has_property(plugin,lp,N.lv2_integer) || lilv.port_has_property(plugin,lp,N.lv2_enumeration);
			p.logarithmic=lilv.port_has_property(plugin,lp,N.pprops_logarithmic);
			p.sample_rate=lilv.port_has_property(plugin,lp,N.lv2_sampleRate);
			if(p.toggled) { p.min=0.0f; p.max=1.0f; }
			if(p.max<=p.min) p.max=p.min+1.0f;
			p.value=p.def;

			if(in)
			{
				// the freewheeling designation and latency reporting are host business, not parameters
				LilvNodes* des=lilv.port_get_value(plugin,lp,N.lv2_designation);
				if(des)
				{
					for(LilvIter* it=lilv.nodes_begin(des);!lilv.nodes_is_end(des,it);it=lilv.nodes_next(des,it))
						if(lilv.node_equals(lilv.nodes_get(des,it),N.lv2_freeWheeling))
							p.hidden=true;
					lilv.nodes_free(des);
				}
				if(!p.hidden)
				{
					p.param=num_params++;
					control_in.push_back((int)i);
				}
			}
		}
		else if(lilv.port_is_a(plugin,lp,N.atom_AtomPort))
		{
			p.type=in?PORT_ATOM_IN:PORT_ATOM_OUT;
			uint32_t size=ATOM_BUFFER_SIZE;
			LilvNodes* ms=lilv.port_get_value(plugin,lp,N.rsz_minimumSize);
			if(ms)
			{
				for(LilvIter* it=lilv.nodes_begin(ms);!lilv.nodes_is_end(ms,it);it=lilv.nodes_next(ms,it))
				{
					int const v=lilv.node_as_int(lilv.nodes_get(ms,it));
					if(v>0 && (uint32_t)v>size)
						size=(uint32_t)v;
				}
				lilv.nodes_free(ms);
			}
			p.atom_storage.assign((size+7)/8,0);
			if(in)
			{
				p.midi=lilv.port_supports_event(plugin,lp,N.midi_MidiEvent);
				p.time=lilv.port_supports_event(plugin,lp,N.time_Position);
				if(p.midi && midi_in_port<0)
					midi_in_port=(int)i;
				if(p.time && time_port<0)
					time_port=(int)i;
			}
		}
		else if(lilv.port_is_a(plugin,lp,N.lv2_CVPort))
		{
			p.type=in?PORT_CV_IN:PORT_CV_OUT;
		}
		(void)out;
	}

	// sample-rate relative ranges
	for(Port& p:ports)
		if(p.sample_rate)
		{
			p.min*=(float)sample_rate;
			p.max*=(float)sample_rate;
			p.def*=(float)sample_rate;
			p.value=p.def;
		}

	numInputs=(int)audio_in.size();
	numOutputs=(int)audio_out.size();
	numParams=num_params;
	numPrograms=1;
	uniqueID=uri_hash(uri.c_str());
	version=1;
	programChunks=true;
	isSynth=plugin_is_instrument(plugin) || midi_in_port>=0;

	target=std::vector<std::atomic<float>>(control_in.size());
	for(size_t k=0;k<control_in.size();k++)
		target[k].store(ports[control_in[k]].def);
	ui_sent.assign(ports.size(),NAN);
	outputs_seen=std::vector<std::atomic<float>>(ports.size());
	for(size_t i=0;i<ports.size();i++)
		outputs_seen[i].store(ports[i].value);

	// features
	opt_min_block=1;
	opt_max_block=MAX_PROCESS_FRAMES;
	opt_nominal_block=256;
	opt_seq_size=(int32_t)ATOM_BUFFER_SIZE;
	opt_sample_rate=(float)sample_rate;
	opt_update_rate=30.0f;
	opt_scale=1.0f;
	int o=0;
	options[o++]={LV2_OPTIONS_INSTANCE,0,U.bufsz_minBlockLength,sizeof(int32_t),U.atom_Int,&opt_min_block};
	options[o++]={LV2_OPTIONS_INSTANCE,0,U.bufsz_maxBlockLength,sizeof(int32_t),U.atom_Int,&opt_max_block};
	options[o++]={LV2_OPTIONS_INSTANCE,0,U.bufsz_nominalBlockLength,sizeof(int32_t),U.atom_Int,&opt_nominal_block};
	options[o++]={LV2_OPTIONS_INSTANCE,0,U.bufsz_sequenceSize,sizeof(int32_t),U.atom_Int,&opt_seq_size};
	options[o++]={LV2_OPTIONS_INSTANCE,0,U.param_sampleRate,sizeof(float),U.atom_Float,&opt_sample_rate};
	options[o++]={LV2_OPTIONS_INSTANCE,0,U.ui_updateRate,sizeof(float),U.atom_Float,&opt_update_rate};
	options[o++]={LV2_OPTIONS_INSTANCE,0,0,0,0,nullptr};

	worker_sched.handle=this;
	worker_sched.schedule_work=worker_schedule;
	log.handle=this;
	log.printf=log_printf;
	log.vprintf=log_vprintf;

	f_map={LV2_URID__map,&g_map};
	f_unmap={LV2_URID__unmap,&g_unmap};
	f_options={LV2_OPTIONS__options,options};
	f_bounded={LV2_BUF_SIZE__boundedBlockLength,nullptr};
	f_worker={LV2_WORKER__schedule,&worker_sched};
	f_log={LV2_LOG__log,&log};
	int f=0;
	features[f++]=&f_map;
	features[f++]=&f_unmap;
	features[f++]=&f_options;
	features[f++]=&f_bounded;
	features[f++]=&f_worker;
	features[f++]=&f_log;
	features[f++]=nullptr;

	if(!instantiate())
	{
		snprintf(error,error_size,"The LV2 plugin '%s' could not be instantiated.",name.c_str());
		return false;
	}

	hasEditor=find_ui();
	return true;
}

Lv2Plugin::~Lv2Plugin()
{
	editorClose();
	setActive(false);
	release();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// instance life cycle (gui thread)
bool Lv2Plugin::instantiate(void)
{
	opt_sample_rate=(float)sample_rate;
	instance=lilv.plugin_instantiate(plugin,sample_rate,features);
	if(!instance)
		return false;

	const LV2_Descriptor* d=instance->lv2_descriptor;
	LV2_Handle const hnd=instance->lv2_handle;

	worker_iface=d->extension_data?(const LV2_Worker_Interface*)d->extension_data(LV2_WORKER__interface):nullptr;
	if(worker_iface)
	{
		std::lock_guard<std::mutex> lock(g_worker_mutex);
		if(!g_worker_started)
		{
			sem_init(&g_worker_sem,0,0);
			g_worker_thread=std::thread(worker_main);
			g_worker_thread.detach();
			g_worker_started=true;
		}
		g_worker_plugins.push_back(this);
	}

	for(size_t i=0;i<ports.size();i++)
	{
		Port& p=ports[i];
		switch(p.type)
		{
		case PORT_CONTROL_IN:
		case PORT_CONTROL_OUT:
			d->connect_port(hnd,(uint32_t)i,&p.value);
			break;
		case PORT_ATOM_IN:
		case PORT_ATOM_OUT:
			d->connect_port(hnd,(uint32_t)i,p.atom_storage.data());
			break;
		case PORT_AUDIO_IN:
		case PORT_CV_IN:
			d->connect_port(hnd,(uint32_t)i,silence.data());
			break;
		case PORT_AUDIO_OUT:
		case PORT_CV_OUT:
			d->connect_port(hnd,(uint32_t)i,scratch.data());
			break;
		default:
			d->connect_port(hnd,(uint32_t)i,nullptr);
			break;
		}
	}
	return true;
}

void Lv2Plugin::release(void)
{
	if(!instance)
		return;
	if(worker_iface)
	{
		std::lock_guard<std::mutex> lock(g_worker_mutex);
		g_worker_plugins.erase(std::remove(g_worker_plugins.begin(),g_worker_plugins.end(),this),g_worker_plugins.end());
	}
	busy.lock();
	LilvInstance* inst=instance;
	instance=nullptr;
	busy.unlock();
	lilv.instance_free(inst);
	worker_iface=nullptr;
}

void Lv2Plugin::setSampleRate(double rate)
{
	if(rate<=0.0 || rate==sample_rate)
		return;

	// lv2 knows the sample rate only from instantiation: keep the state, create a new instance
	void* data=nullptr;
	int const size=instance?getChunk(&data,false):0;
	std::vector<uint8_t> state((uint8_t*)data,(uint8_t*)data+size);

	bool const was_active=active;
	setActive(false);
	editorClose();
	release();

	for(Port& p:ports)
		if(p.sample_rate && sample_rate>0.0)
		{
			float const f=(float)(rate/sample_rate);
			p.min*=f;
			p.max*=f;
			p.def*=f;
		}
	sample_rate=rate;

	if(instantiate() && !state.empty())
		setChunk(state.data(),(int)state.size(),false);
	if(was_active)
		setActive(true);
}

void Lv2Plugin::setActive(bool a)
{
	if(!instance || a==active)
		return;
	busy.lock();
	const LV2_Descriptor* d=instance->lv2_descriptor;
	if(a)
	{
		if(d->activate)
			d->activate(instance->lv2_handle);
		last_playing=false;
		last_bpm=0.0;
		expected_frame=-1.0;
	}
	else if(d->deactivate)
		d->deactivate(instance->lv2_handle);
	active=a;
	busy.unlock();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// parameters
float Lv2Plugin::to_value(const Port& p,float n) const
{
	n=n<0.0f?0.0f:(n>1.0f?1.0f:n);
	if(p.toggled)
		return n>=0.5f?1.0f:0.0f;
	float v;
	if(p.logarithmic && p.min>0.0f && p.max>0.0f)
		v=p.min*powf(p.max/p.min,n);
	else
		v=p.min+n*(p.max-p.min);
	if(p.integer)
		v=roundf(v);
	return v;
}

float Lv2Plugin::to_normalized(const Port& p,float v) const
{
	if(p.toggled)
		return v>0.0f?1.0f:0.0f;
	float n;
	if(p.logarithmic && p.min>0.0f && p.max>0.0f)
		n=v>0.0f?logf(v/p.min)/logf(p.max/p.min):0.0f;
	else
		n=(v-p.min)/(p.max-p.min);
	return n<0.0f?0.0f:(n>1.0f?1.0f:n);
}

float Lv2Plugin::getParameter(int index)
{
	if(index<0 || index>=numParams)
		return 0.0f;
	return to_normalized(ports[control_in[index]],target[index].load(std::memory_order_relaxed));
}

void Lv2Plugin::setParameter(int index,float value)
{
	if(index<0 || index>=numParams)
		return;
	target[index].store(to_value(ports[control_in[index]],value),std::memory_order_relaxed);
}

void Lv2Plugin::getParamName(int index,char* buf)
{
	if(index<0 || index>=numParams)
	{
		buf[0]=0;
		return;
	}
	copy_str(buf,ports[control_in[index]].name.c_str(),31);
}

void Lv2Plugin::getParamDisplay(int index,char* buf)
{
	if(index<0 || index>=numParams)
	{
		buf[0]=0;
		return;
	}
	const Port& p=ports[control_in[index]];
	float const v=target[index].load(std::memory_order_relaxed);
	if(p.toggled)
		snprintf(buf,32,"%s",v>0.0f?"on":"off");
	else if(p.integer)
		snprintf(buf,32,"%d",(int)lroundf(v));
	else if(fabsf(v)>=10000.0f)
		snprintf(buf,32,"%.0f",v);
	else
		snprintf(buf,32,"%.4g",v);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// state: "ADXLV2S1" + lilv state as turtle (port values and the plugin's own state)
const void* Lv2Plugin::get_port_value(const char* symbol,void* user,uint32_t* size,uint32_t* type)
{
	Lv2Plugin* self=(Lv2Plugin*)user;
	for(size_t k=0;k<self->control_in.size();k++)
	{
		Port& p=self->ports[self->control_in[k]];
		if(p.symbol==symbol)
		{
			// the value written to the turtle is read right away, a member keeps it alive
			p.state_value=self->target[k].load();
			*size=sizeof(float);
			*type=U.atom_Float;
			return &p.state_value;
		}
	}
	*size=0;
	*type=0;
	return nullptr;
}

void Lv2Plugin::set_port_value(const char* symbol,void* user,const void* value,uint32_t size,uint32_t type)
{
	Lv2Plugin* self=(Lv2Plugin*)user;
	float v;
	if(type==U.atom_Float && size==sizeof(float)) v=*(const float*)value;
	else if(type==U.atom_Double && size==sizeof(double)) v=(float)*(const double*)value;
	else if(type==U.atom_Int && size==sizeof(int32_t)) v=(float)*(const int32_t*)value;
	else if(type==U.atom_Long && size==sizeof(int64_t)) v=(float)*(const int64_t*)value;
	else if(type==U.atom_Bool && size==sizeof(int32_t)) v=*(const int32_t*)value?1.0f:0.0f;
	else return;
	for(size_t k=0;k<self->control_in.size();k++)
		if(self->ports[self->control_in[k]].symbol==symbol)
			self->target[k].store(v);
}

static std::vector<char> g_chunk_buf;

int Lv2Plugin::getChunk(void** data,bool)
{
	static std::vector<char> chunk;
	chunk.assign("ADXLV2S1","ADXLV2S1"+8);
	if(instance && lv2_world())
	{
		busy.lock();
		LilvState* st=lilv.state_new_from_instance(plugin,instance,&g_map,nullptr,nullptr,nullptr,nullptr,get_port_value,this,
			LV2_STATE_IS_POD|LV2_STATE_IS_PORTABLE,features);
		busy.unlock();
		if(st)
		{
			char* ttl=lilv.state_to_string(g_world,&g_map,&g_unmap,st,"urn:aodix:state",nullptr);
			if(ttl)
			{
				chunk.insert(chunk.end(),ttl,ttl+strlen(ttl)+1);
				lilv.free_(ttl);
			}
			lilv.state_free(st);
		}
	}
	g_chunk_buf=chunk;
	*data=g_chunk_buf.data();
	return (int)g_chunk_buf.size();
}

void Lv2Plugin::setChunk(const void* data,int size,bool)
{
	const char* p=(const char*)data;
	if(!p || size<9 || memcmp(p,"ADXLV2S1",8)!=0 || !instance || !lv2_world())
		return;
	std::string ttl(p+8,(size_t)(size-8));
	LilvState* st=lilv.state_new_from_string(g_world,&g_map,ttl.c_str());
	if(!st)
		return;
	busy.lock();
	lilv.state_restore(st,instance,set_port_value,this,0,features);
	busy.unlock();
	lilv.state_free(st);
	host->plugin_update_display(this);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// worker
LV2_Worker_Status Lv2Plugin::worker_schedule(LV2_Worker_Schedule_Handle h,uint32_t size,const void* data)
{
	Lv2Plugin* self=(Lv2Plugin*)h;
	if(!self->work_requests.write(data,size))
		return LV2_WORKER_ERR_NO_SPACE;
	sem_post(&g_worker_sem);
	return LV2_WORKER_SUCCESS;
}

LV2_Worker_Status Lv2Plugin::worker_respond(LV2_Worker_Respond_Handle h,uint32_t size,const void* data)
{
	Lv2Plugin* self=(Lv2Plugin*)h;
	return self->work_responses.write(data,size)?LV2_WORKER_SUCCESS:LV2_WORKER_ERR_NO_SPACE;
}

void Lv2Plugin::do_work(void)
{
	while(instance && worker_iface && work_requests.read(work_buf))
		worker_iface->work(instance->lv2_handle,worker_respond,this,(uint32_t)work_buf.size(),work_buf.data());
}

static void worker_main(void)
{
	for(;;)
	{
		while(sem_wait(&g_worker_sem)!=0) {}
		std::lock_guard<std::mutex> lock(g_worker_mutex);
		for(Lv2Plugin* p:g_worker_plugins)
			p->do_work();
	}
}

int Lv2Plugin::log_vprintf(LV2_Log_Handle h,LV2_URID type,const char* fmt,va_list ap)
{
	if(type==U.log_Trace || type==U.log_Note)
		return 0;
	Lv2Plugin* self=(Lv2Plugin*)h;
	char buf[512];
	vsnprintf(buf,sizeof(buf),fmt,ap);
	size_t n=strlen(buf);
	while(n>0 && buf[n-1]=='\n')
		buf[--n]=0;
	fprintf(stderr,"aodix: lv2: %s: %s\n",self->name.c_str(),buf);
	return (int)n;
}

int Lv2Plugin::log_printf(LV2_Log_Handle h,LV2_URID type,const char* fmt,...)
{
	va_list ap;
	va_start(ap,fmt);
	int const r=log_vprintf(h,type,fmt,ap);
	va_end(ap);
	return r;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// audio thread
void Lv2Plugin::process(float** inputs,float** outputs,int frames,const ADX_MIDI_EVENT* events,int num_events)
{
	bool done=false;
	if(frames>0 && frames<=MAX_PROCESS_FRAMES && busy.try_lock())
	{
		if(instance && active)
		{
			const LV2_Descriptor* d=instance->lv2_descriptor;
			LV2_Handle const hnd=instance->lv2_handle;

			// control values
			for(size_t k=0;k<control_in.size();k++)
				ports[control_in[k]].value=target[k].load(std::memory_order_relaxed);

			// atom ports: inputs get an empty sequence, outputs the full capacity
			for(Port& p:ports)
			{
				if(p.type==PORT_ATOM_IN)
				{
					LV2_Atom_Sequence* s=seq(p);
					s->atom.type=U.atom_Sequence;
					s->atom.size=sizeof(LV2_Atom_Sequence_Body);
					s->body.unit=0;
					s->body.pad=0;
				}
				else if(p.type==PORT_ATOM_OUT)
				{
					LV2_Atom_Sequence* s=seq(p);
					s->atom.type=U.atom_Chunk;
					s->atom.size=(uint32_t)(p.atom_storage.size()*8-sizeof(LV2_Atom));
				}
			}

			// time position when the transport changes (start / stop, tempo change, jump)
			const ADX_TIME_INFO* ti=host->plugin_time_info();
			if(time_port>=0 && ti)
			{
				bool const playing=(ti->flags & ADX_TRANSPORT_PLAYING)!=0;
				bool const jumped=playing && expected_frame>=0.0 && fabs(ti->samplePos-expected_frame)>0.5;
				if(playing!=last_playing || ti->tempo!=last_bpm || jumped || (playing && expected_frame<0.0))
				{
					Port& tp=ports[time_port];
					LV2_Atom_Sequence* s=seq(tp);
					uint32_t const cap=(uint32_t)(tp.atom_storage.size()*8);
					uint8_t buf[256];
					LV2_Atom_Forge forge;
					lv2_atom_forge_init(&forge,&g_map);
					lv2_atom_forge_set_buffer(&forge,buf,sizeof(buf));
					LV2_Atom_Forge_Frame frame;
					lv2_atom_forge_object(&forge,&frame,0,U.time_Position);
					int const num=ti->timeSigNumerator>0?ti->timeSigNumerator:4;
					double const beats=ti->ppqPos;
					lv2_atom_forge_key(&forge,U.time_frame); lv2_atom_forge_long(&forge,(int64_t)ti->samplePos);
					lv2_atom_forge_key(&forge,U.time_speed); lv2_atom_forge_float(&forge,playing?1.0f:0.0f);
					lv2_atom_forge_key(&forge,U.time_bpm); lv2_atom_forge_float(&forge,(float)ti->tempo);
					lv2_atom_forge_key(&forge,U.time_bar); lv2_atom_forge_long(&forge,(int64_t)(beats/num));
					lv2_atom_forge_key(&forge,U.time_barBeat); lv2_atom_forge_float(&forge,(float)fmod(beats,(double)num));
					lv2_atom_forge_key(&forge,U.time_beatUnit); lv2_atom_forge_int(&forge,ti->timeSigDenominator>0?ti->timeSigDenominator:4);
					lv2_atom_forge_key(&forge,U.time_beatsPerBar); lv2_atom_forge_float(&forge,(float)num);
					lv2_atom_forge_pop(&forge,&frame);
					const LV2_Atom* obj=(const LV2_Atom*)buf;
					LV2_Atom_Event* ev=lv2_atom_sequence_end(&s->body,s->atom.size);
					uint32_t const evsize=(uint32_t)(sizeof(LV2_Atom_Event)+lv2_atom_pad_size(obj->size));
					if(sizeof(LV2_Atom)+s->atom.size+evsize<=cap)
					{
						ev->time.frames=0;
						ev->body.type=obj->type;
						ev->body.size=obj->size;
						memcpy(LV2_ATOM_BODY(&ev->body),LV2_ATOM_BODY_CONST(obj),obj->size);
						s->atom.size+=evsize;
					}
					last_playing=playing;
					last_bpm=ti->tempo;
				}
				expected_frame=playing?ti->samplePos+frames:-1.0;
			}

			// messages from the ui (time 0)
			atom_lock.lock();
			atom_work.swap(atom_pending);
			atom_lock.unlock();
			for(size_t pos=0;pos+8<=atom_work.size();)
			{
				uint32_t port,size;
				memcpy(&port,&atom_work[pos],4);
				memcpy(&size,&atom_work[pos+4],4);
				const LV2_Atom* a=(const LV2_Atom*)&atom_work[pos+8];
				if(port<ports.size() && ports[port].type==PORT_ATOM_IN)
				{
					Port& ap=ports[port];
					LV2_Atom_Sequence* s=seq(ap);
					uint32_t const cap=(uint32_t)(ap.atom_storage.size()*8);
					uint32_t const evsize=(uint32_t)(sizeof(LV2_Atom_Event)+lv2_atom_pad_size(a->size));
					if(sizeof(LV2_Atom)+s->atom.size+evsize<=cap)
					{
						LV2_Atom_Event* ev=lv2_atom_sequence_end(&s->body,s->atom.size);
						ev->time.frames=0;
						ev->body=*a;
						memcpy(LV2_ATOM_BODY(&ev->body),LV2_ATOM_BODY_CONST(a),a->size);
						s->atom.size+=evsize;
					}
				}
				pos+=8+((size+7)&~7u);
			}
			atom_work.clear();

			// midi events (aodix delivers them sorted)
			if(midi_in_port>=0)
			{
				Port& mp=ports[midi_in_port];
				LV2_Atom_Sequence* s=seq(mp);
				uint32_t const cap=(uint32_t)(mp.atom_storage.size()*8);
				for(int e=0;e<num_events;e++)
				{
					const ADX_MIDI_EVENT& m=events[e];
					uint8_t const status=m.midiData[0]&0xF0;
					uint32_t len=3;
					if(status==0xC0 || status==0xD0) len=2;
					uint32_t const evsize=(uint32_t)(sizeof(LV2_Atom_Event)+lv2_atom_pad_size(len));
					if(sizeof(LV2_Atom)+s->atom.size+evsize>cap)
						break;
					LV2_Atom_Event* ev=lv2_atom_sequence_end(&s->body,s->atom.size);
					ev->time.frames=std::max(0,std::min(m.deltaFrames,frames-1));
					ev->body.type=U.midi_Event;
					ev->body.size=len;
					memcpy(LV2_ATOM_BODY(&ev->body),m.midiData,len);
					s->atom.size+=evsize;
				}
			}

			// audio buffers
			for(size_t i=0;i<audio_in.size();i++)
				d->connect_port(hnd,(uint32_t)audio_in[i],inputs[i]);
			for(size_t i=0;i<audio_out.size();i++)
				d->connect_port(hnd,(uint32_t)audio_out[i],outputs[i]);

			// worker responses, run, end of run
			if(worker_iface)
			{
				while(work_responses.read(work_buf))
					worker_iface->work_response(hnd,(uint32_t)work_buf.size(),work_buf.data());
			}

			d->run(hnd,(uint32_t)frames);

			if(worker_iface && worker_iface->end_run)
				worker_iface->end_run(hnd);

			// outputs: control values for the ui, midi to aodix, all atom events to the ui
			for(size_t i=0;i<ports.size();i++)
				if(ports[i].type==PORT_CONTROL_OUT)
					outputs_seen[i].store(ports[i].value,std::memory_order_relaxed);
			midi_out.clear();
			for(size_t i=0;i<ports.size();i++)
			{
				Port& p=ports[i];
				if(p.type!=PORT_ATOM_OUT)
					continue;
				LV2_Atom_Sequence* s=seq(p);
				if(s->atom.type!=U.atom_Sequence)
					continue;
				LV2_ATOM_SEQUENCE_FOREACH(s,ev)
				{
					if(ev->body.type==U.midi_Event && ev->body.size>=1 && ev->body.size<=3 && midi_out.size()<1024)
					{
						ADX_MIDI_EVENT m;
						memset(&m,0,sizeof(m));
						m.deltaFrames=(int)ev->time.frames;
						memcpy(m.midiData,LV2_ATOM_BODY(&ev->body),ev->body.size);
						midi_out.push_back(m);
					}
					if(ui_open)
					{
						// [uint32 port] + atom
						uint8_t tmp[4096];
						uint32_t const asize=(uint32_t)sizeof(LV2_Atom)+ev->body.size;
						if(asize+4<=sizeof(tmp))
						{
							uint32_t const port=(uint32_t)i;
							memcpy(tmp,&port,4);
							memcpy(tmp+4,&ev->body,asize);
							ui_events.write(tmp,asize+4);
						}
					}
				}
			}
			if(!midi_out.empty())
				host->plugin_midi_out(this,midi_out.data(),(int)midi_out.size());
			done=true;
		}
		busy.unlock();
	}

	if(!done)
		for(int o=0;o<numOutputs;o++)
			memset(outputs[o],0,sizeof(float)*(frames>0?frames:0));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ui (X11UI, embedded; gui thread)
bool Lv2Plugin::find_ui(void)
{
	LilvUIs* uis=lilv.plugin_get_uis(plugin);
	if(!uis)
		return false;
	bool found=false;
	for(LilvIter* i=lilv.uis_begin(uis);!lilv.uis_is_end(uis,i);i=lilv.uis_next(uis,i))
	{
		const LilvUI* ui=lilv.uis_get(uis,i);
		if(!lilv.ui_is_a(ui,N.ui_X11UI))
			continue;
		ui_uri=lilv.node_as_uri(lilv.ui_get_uri(ui));
		ui_bundle=uri_to_path(lilv.node_as_uri(lilv.ui_get_bundle_uri(ui)))+"/";
		ui_binary=uri_to_path(lilv.node_as_uri(lilv.ui_get_binary_uri(ui)));
		found=!ui_binary.empty();
		break;
	}
	lilv.uis_free(uis);
	return found;
}

void Lv2Plugin::ui_write(LV2UI_Controller c,uint32_t port,uint32_t size,uint32_t protocol,const void* buffer)
{
	Lv2Plugin* self=(Lv2Plugin*)c;
	if(port>=self->ports.size())
		return;
	Port& p=self->ports[port];
	if(protocol==0 && size==sizeof(float) && p.type==PORT_CONTROL_IN && p.param>=0)
	{
		float const v=*(const float*)buffer;
		self->target[p.param].store(v);
		self->ui_sent[port]=v;
		// ui edits are recorded like knob movements in aodix (we are in the gui thread)
		self->host->plugin_automate(self,p.param,self->to_normalized(p,v));
	}
	else if(protocol==U.atom_eventTransfer && p.type==PORT_ATOM_IN && size>=sizeof(LV2_Atom))
	{
		self->atom_lock.lock();
		size_t const pos=self->atom_pending.size();
		if(pos+8+size+8<=65536)
		{
			self->atom_pending.resize(pos+8+((size+7)&~7u));
			memcpy(&self->atom_pending[pos],&port,4);
			memcpy(&self->atom_pending[pos+4],&size,4);
			memcpy(&self->atom_pending[pos+8],buffer,size);
		}
		self->atom_lock.unlock();
	}
}

int Lv2Plugin::ui_resize(LV2UI_Feature_Handle h,int w,int hh)
{
	Lv2Plugin* self=(Lv2Plugin*)h;
	if(w<=0 || hh<=0)
		return 1;
	self->ui_w=w;
	self->ui_h=hh;
	if(self->ui_open)
	{
		self->host_sizes.push_back({w,hh});
		self->host->plugin_resize_editor(self,w,hh);
	}
	return 0;
}

uint32_t Lv2Plugin::ui_port_index(LV2UI_Feature_Handle h,const char* symbol)
{
	Lv2Plugin* self=(Lv2Plugin*)h;
	for(size_t i=0;i<self->ports.size();i++)
		if(self->ports[i].symbol==symbol)
			return (uint32_t)i;
	return LV2UI_INVALID_PORT_INDEX;
}

void Lv2Plugin::ui_idle_cb(void* user)
{
	Lv2Plugin* self=(Lv2Plugin*)user;
	if(self->ui_handle && self->ui_idle_iface)
		self->ui_idle_iface->idle(self->ui_handle);
}

bool Lv2Plugin::editorGetSize(int* width,int* height)
{
	if(!hasEditor)
		return false;

	// the ui tells its size through ui:resize; if it does not, ask X for the size of its widget window
	if(ui_widget && (ui_w<=0 || ui_h<=0))
	{
		Display* dpy=(Display*)adxplat::x11_display();
		Window root;
		int x,y;
		unsigned int w,h,b,depth;
		if(dpy && XGetGeometry(dpy,(Window)(uintptr_t)ui_widget,&root,&x,&y,&w,&h,&b,&depth) && w>1 && h>1)
		{
			ui_w=(int)w;
			ui_h=(int)h;
		}
	}
	*width=ui_w>0?ui_w:400;
	*height=ui_h>0?ui_h:300;
	host_sizes.push_back({*width,*height});
	if(host_sizes.size()>8)
		host_sizes.erase(host_sizes.begin());
	return true;
}

bool Lv2Plugin::editorOpen(unsigned long parent)
{
	if(!hasEditor || !parent || !instance)
		return false;
	editorClose();

	static std::map<std::string,void*> ui_libs;
	void* lib=ui_libs.count(ui_binary)?ui_libs[ui_binary]:nullptr;
	if(!lib)
	{
		lib=dlopen(ui_binary.c_str(),RTLD_NOW|RTLD_LOCAL);
		if(!lib)
		{
			fprintf(stderr,"aodix: lv2: %s: %s\n",name.c_str(),dlerror());
			return false;
		}
		ui_libs[ui_binary]=lib;
	}
	LV2UI_DescriptorFunction df=(LV2UI_DescriptorFunction)dlsym(lib,"lv2ui_descriptor");
	if(!df)
		return false;
	ui_desc=nullptr;
	for(uint32_t i=0;;i++)
	{
		const LV2UI_Descriptor* d=df(i);
		if(!d)
			break;
		if(ui_uri==d->URI)
		{
			ui_desc=d;
			break;
		}
	}
	if(!ui_desc)
		return false;

	static LV2UI_Resize resize_feature;
	static LV2UI_Port_Map port_map;
	static LV2_Extension_Data_Feature data_access;
	resize_feature.handle=this;
	resize_feature.ui_resize=ui_resize;
	port_map.handle=this;
	port_map.port_index=ui_port_index;
	data_access.data_access=instance->lv2_descriptor->extension_data;

	LV2_Feature f_parent={LV2_UI__parent,(void*)(uintptr_t)parent};
	LV2_Feature f_resize={LV2_UI__resize,&resize_feature};
	LV2_Feature f_portmap={LV2_UI__portMap,&port_map};
	LV2_Feature f_instance={LV2_INSTANCE_ACCESS_URI,instance->lv2_handle};
	LV2_Feature f_data={LV2_DATA_ACCESS_URI,&data_access};
	LV2_Feature f_idle={LV2_UI__idleInterface,nullptr};
	const LV2_Feature* uif[]={&f_map,&f_unmap,&f_options,&f_log,&f_parent,&f_resize,&f_portmap,&f_instance,&f_data,&f_idle,nullptr};

	ui_parent=parent;
	ui_widget=nullptr;
	ui_w=ui_h=0;
	ui_handle=ui_desc->instantiate(ui_desc,uri.c_str(),ui_bundle.c_str(),ui_write,this,&ui_widget,uif);
	if(!ui_handle)
	{
		fprintf(stderr,"aodix: lv2: %s: the ui could not be created\n",name.c_str());
		return false;
	}

	ui_idle_iface=ui_desc->extension_data?(const LV2UI_Idle_Interface*)ui_desc->extension_data(LV2_UI__idleInterface):nullptr;
	ui_resize_iface=ui_desc->extension_data?(const LV2UI_Resize*)ui_desc->extension_data(LV2_UI__resize):nullptr;

	// current values to the ui
	if(ui_desc->port_event)
	{
		for(size_t k=0;k<control_in.size();k++)
		{
			float const v=target[k].load();
			ui_desc->port_event(ui_handle,(uint32_t)control_in[k],sizeof(float),0,&v);
			ui_sent[control_in[k]]=v;
		}
	}

	ui_timer=adxplat::timer_start(33,ui_idle_cb,this);
	ui_open=true;

	int w,h;
	editorGetSize(&w,&h);
	fprintf(stderr,"aodix: lv2: %s: editor %dx%d\n",name.c_str(),w,h);
	return true;
}

void Lv2Plugin::editorClose(void)
{
	if(!ui_handle)
		return;
	if(ui_timer)
		adxplat::timer_stop(ui_timer);
	ui_timer=0;
	ui_open=false;
	ui_desc->cleanup(ui_handle);
	ui_handle=nullptr;
	ui_widget=nullptr;
	host_sizes.clear();
	std::vector<uint8_t> tmp;
	while(ui_events.read(tmp)) {}
}

bool Lv2Plugin::editorCanResize(void)
{
	return ui_handle && ui_resize_iface;
}

void Lv2Plugin::editorResized(int width,int height)
{
	if(!ui_handle || width<=0 || height<=0)
		return;

	// the frame got a size aodix gave it itself (the size messages arrive later): no user resize
	for(size_t i=0;i<host_sizes.size();i++)
		if(host_sizes[i].first==width && host_sizes[i].second==height)
		{
			host_sizes.erase(host_sizes.begin(),host_sizes.begin()+i+1);
			return;
		}
	if(width==ui_w && height==ui_h)
		return;
	if(!ui_resize_iface)
		return;
	ui_w=width;
	ui_h=height;
	// like suil: the ui's resize interface is called with the ui instance handle
	ui_resize_iface->ui_resize((LV2UI_Feature_Handle)ui_handle,width,height);
	Display* dpy=(Display*)adxplat::x11_display();
	if(dpy && ui_widget)
	{
		XResizeWindow(dpy,(Window)(uintptr_t)ui_widget,(unsigned)width,(unsigned)height);
		XFlush(dpy);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Lv2Plugin::idle(void)
{
	if(!ui_handle || !ui_desc->port_event)
		return;

	// control values (inputs changed by aodix / automation, outputs written by the plugin)
	for(size_t i=0;i<ports.size();i++)
	{
		Port& p=ports[i];
		float v;
		if(p.type==PORT_CONTROL_IN && p.param>=0)
			v=target[p.param].load(std::memory_order_relaxed);
		else if(p.type==PORT_CONTROL_OUT)
			v=outputs_seen[i].load(std::memory_order_relaxed);
		else
			continue;
		if(!(v==ui_sent[i]))
		{
			ui_sent[i]=v;
			ui_desc->port_event(ui_handle,(uint32_t)i,sizeof(float),0,&v);
		}
	}

	// atom events from the plugin
	std::vector<uint8_t> msg;
	while(ui_events.read(msg))
	{
		if(msg.size()<4+sizeof(LV2_Atom))
			continue;
		uint32_t port;
		memcpy(&port,msg.data(),4);
		const LV2_Atom* a=(const LV2_Atom*)(msg.data()+4);
		ui_desc->port_event(ui_handle,port,(uint32_t)(sizeof(LV2_Atom)+a->size),U.atom_eventTransfer,a);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// library functions
bool adx_plugin_is_lv2(const char* path)
{
	size_t n=strlen(path);
	while(n>0 && path[n-1]=='/')
		n--;
	return n>=4 && strncmp(path+n-4,".lv2",4)==0;
}

void adx_lv2_list(std::vector<AdxPluginDesc>& out)
{
	for_each_plugin([&](const LilvPlugin* p)
	{
		const char* uri=lilv.node_as_uri(lilv.plugin_get_uri(p));
		LilvNode* n=lilv.plugin_get_name(p);
		std::string const name=n?lilv.node_as_string(n):uri;
		if(n)
			lilv.node_free(n);
		std::string const bundle=uri_to_path(lilv.node_as_uri(lilv.plugin_get_bundle_uri(p)));
		out.push_back({bundle,name,uri_hash(uri),plugin_is_instrument(p)});
		return true;
	});
}

int lv2_plugin_scan(const char* path,std::vector<AdxPluginDesc>& out)
{
	std::vector<AdxPluginDesc> all;
	adx_lv2_list(all);
	int count=0;
	std::string want=path;
	while(want.size()>1 && want.back()=='/')
		want.pop_back();
	for(const AdxPluginDesc& d:all)
		if(d.path==want)
		{
			out.push_back(d);
			count++;
		}
	return count;
}

AdxPlugin* lv2_plugin_load(const char* path,int32_t wanted_id,AdxPluginHost* host,char* error,int error_size)
{
	if(!lv2_world())
	{
		snprintf(error,error_size,"LV2 plugins need the lilv library (liblilv-0.so.0), which was not found.");
		return nullptr;
	}

	// the plugin with this uri hash (wherever its bundle is now), otherwise the first plugin of the bundle
	std::string want=path;
	while(want.size()>1 && want.back()=='/')
		want.pop_back();
	const LilvPlugin* chosen=nullptr;
	const LilvPlugin* first_in_bundle=nullptr;
	for_each_plugin([&](const LilvPlugin* p)
	{
		const char* uri=lilv.node_as_uri(lilv.plugin_get_uri(p));
		if(wanted_id!=0 && uri_hash(uri)==wanted_id)
		{
			chosen=p;
			return false;
		}
		if(!first_in_bundle && uri_to_path(lilv.node_as_uri(lilv.plugin_get_bundle_uri(p)))==want)
			first_in_bundle=p;
		return true;
	});
	if(!chosen)
		chosen=first_in_bundle;
	if(!chosen)
	{
		snprintf(error,error_size,"The LV2 plugin was not found (bundle '%s').",path);
		return nullptr;
	}

	Lv2Plugin* p=new Lv2Plugin(chosen,host);
	if(!p->init(error,error_size))
	{
		delete p;
		return nullptr;
	}
	return p;
}
