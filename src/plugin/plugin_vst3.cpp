/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - VST3 host implementation of the AdxPlugin interface
//
// Threading model (same as aodix used it with vst 2):
// - process() runs in the audio thread (jack callback, inside the aodix critical section)
// - everything else runs in the gui thread, except setParameter() which aodix also calls from the audio
//   thread (sequencer automation events).
// Parameter changes therefore go through small queues: changes for the processor are delivered with the
// next process() call, changes for the edit controller are delivered in idle() (gui thread).
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AdxPlugin.h"
#include "../platform/adxplat_runloop.h"

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/vsttypes.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/connectionproxy.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/moduleinfo/moduleinfoparser.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// host application object handed to plugins
class AodixHostApplication : public HostApplication
{
public:
	tresult PLUGIN_API getName(String128 name) override
	{
		Steinberg::Vst::StringConvert::convert("Aodix",name,127);
		return kResultTrue;
	}
};

IPtr<AodixHostApplication>& host_application(void)
{
	static IPtr<AodixHostApplication> app=owned(new AodixHostApplication());
	return app;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// loaded modules stay loaded for the whole session (many plugins don't like being unloaded and reloaded)
std::map<std::string,VST3::Hosting::Module::Ptr>& module_cache(void)
{
	static std::map<std::string,VST3::Hosting::Module::Ptr> cache;
	return cache;
}

VST3::Hosting::Module::Ptr get_module(const std::string& path,std::string& error)
{
	auto& cache=module_cache();
	auto it=cache.find(path);
	if(it!=cache.end())
		return it->second;

	VST3::Hosting::Module::Ptr m=VST3::Hosting::Module::create(path,error);
	if(m)
	{
		m->getFactory().setHostContext(host_application());
		cache[path]=m;
	}
	return m;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 32-bit id stored in aodix projects (hash of the 128-bit class id)
int32_t uid_hash(const VST3::UID& uid)
{
	const TUID& d=uid.data();
	uint32_t h=2166136261u;
	for(int i=0;i<16;i++)
	{
		h^=(uint8_t)d[i];
		h*=16777619u;
	}
	if(h==0)
		h=1;
	return (int32_t)h;
}

bool is_instrument(const std::vector<std::string>& sub)
{
	for(const std::string& s:sub)
		if(s=="Instrument" || s.compare(0,10,"Instrument")==0)
			return true;
	return false;
}

std::string utf16_to_utf8(const TChar* s)
{
	return Steinberg::Vst::StringConvert::convert(s);
}

// copy into a fixed aodix string buffer (ascii-safe truncation)
void copy_str(char* dst,const std::string& src,size_t max_chars)
{
	size_t n=std::min(src.size(),max_chars);
	// don't cut utf-8 sequences in half
	while(n>0 && n<src.size() && ((unsigned char)src[n]&0xC0)==0x80)
		n--;
	memcpy(dst,src.data(),n);
	dst[n]=0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// tiny spin lock for the parameter queues (held only for a few instructions, also in the audio thread)
class SpinLock
{
public:
	void lock(void){while(flag.test_and_set(std::memory_order_acquire)){}}
	void unlock(void){flag.clear(std::memory_order_release);}
private:
	std::atomic_flag flag=ATOMIC_FLAG_INIT;
};

struct ParamChange
{
	ParamID			id;
	ParamValue		value;
	int32			offset;
};

class Vst3Plugin;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ComponentHandler : public U::Implements<U::Directly<IComponentHandler>>
{
public:
	explicit ComponentHandler(Vst3Plugin* p):plugin(p){}
	tresult PLUGIN_API beginEdit(ParamID id) override;
	tresult PLUGIN_API performEdit(ParamID id,ParamValue value) override;
	tresult PLUGIN_API endEdit(ParamID id) override;
	tresult PLUGIN_API restartComponent(int32 flags) override;
	Vst3Plugin* plugin;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// plug frame for editors, also provides the linux run loop (fd watches and timers of the aodix gui thread)
class PlugFrame : public U::Implements<U::Directly<IPlugFrame,Linux::IRunLoop>>
{
public:
	explicit PlugFrame(Vst3Plugin* p):plugin(p){}
	~PlugFrame() override
	{
		for(auto& t:timers)
			adxplat::timer_stop(t.first);
		for(auto& e:handlers)
			adxplat::remove_fd_watches(e);
	}

	tresult PLUGIN_API resizeView(IPlugView* view,ViewRect* new_size) override;

	tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* handler,Linux::FileDescriptor fd) override
	{
		if(!handler)
			return kInvalidArgument;
		adxplat::add_fd_watch(fd,&PlugFrame::on_fd,handler);
		handlers.push_back(handler);
		return kResultTrue;
	}

	tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler* handler) override
	{
		adxplat::remove_fd_watches(handler);
		handlers.erase(std::remove(handlers.begin(),handlers.end(),handler),handlers.end());
		return kResultTrue;
	}

	tresult PLUGIN_API registerTimer(Linux::ITimerHandler* handler,Linux::TimerInterval ms) override
	{
		if(!handler)
			return kInvalidArgument;
		unsigned long const id=adxplat::timer_start((unsigned int)std::max<Linux::TimerInterval>(ms,1),&PlugFrame::on_timer,handler);
		timers.push_back({id,handler});
		return kResultTrue;
	}

	tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* handler) override
	{
		for(auto it=timers.begin();it!=timers.end();)
		{
			if(it->second==handler)
			{
				adxplat::timer_stop(it->first);
				it=timers.erase(it);
			}
			else
				++it;
		}
		return kResultTrue;
	}

	static void on_fd(int fd,void* user){((Linux::IEventHandler*)user)->onFDIsSet(fd);}
	static void on_timer(void* user){((Linux::ITimerHandler*)user)->onTimer();}

	Vst3Plugin* plugin;
	std::vector<std::pair<unsigned long,Linux::ITimerHandler*>> timers;
	std::vector<Linux::IEventHandler*> handlers;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Vst3Plugin : public AdxPlugin
{
public:
	Vst3Plugin(AdxPluginHost* h):host(h){}
	~Vst3Plugin() override;

	bool init(VST3::Hosting::Module::Ptr mod,const VST3::Hosting::ClassInfo& info,std::string& error);

	const char*	format(void) override {return "VST3";}
	void		getEffectName(char* buf) override {copy_str(buf,name,64);}

	void		setSampleRate(double rate) override;
	void		setBlockSize(int frames) override {(void)frames;}	// max block size is fixed to the aodix maximum
	void		setActive(bool active) override;
	void		negotiate_stereo(void);

	float		getParameter(int index) override;
	void		setParameter(int index,float value) override;
	void		getParamName(int index,char* buf) override;
	void		getParamDisplay(int index,char* buf) override;

	int			getProgram(void) override;
	void		setProgram(int index) override;
	void		getProgramName(char* buf) override;
	void		setProgramName(const char* name) override;
	bool		getProgramNameIndexed(int index,char* buf) override;

	int			getChunk(void** data,bool preset) override;
	void		setChunk(const void* data,int size,bool preset) override;

	void		process(float** inputs,float** outputs,int frames,const ADX_MIDI_EVENT* events,int num_events) override;

	bool		editorGetSize(int* width,int* height) override;
	bool		editorOpen(unsigned long parent) override;
	void		editorClose(void) override;
	void		editorResized(int width,int height) override;
	bool		editorCanResize(void) override;

	void		idle(void) override;

	// called by the component handler / plug frame (gui thread)
	void		on_perform_edit(ParamID id,ParamValue value);
	void		on_restart(int32 flags);
	void		on_resize_view(IPlugView* view,ViewRect* r);

private:
	void		scan_parameters(void);
	void		scan_programs(void);
	void		scan_midi_mapping(void);
	void		setup_processing(void);
	void		push_processor_change(ParamID id,ParamValue v,int32 offset);
	void		push_controller_change(ParamID id,ParamValue v);
	int			param_index(ParamID id);

	AdxPluginHost*					host;
	VST3::Hosting::Module::Ptr		module;
	std::string						name;

	IPtr<IComponent>				component;
	IPtr<IAudioProcessor>			processor;
	IPtr<IEditController>			controller;
	IPtr<ConnectionProxy>			comp_to_ctrl;
	IPtr<ConnectionProxy>			ctrl_to_comp;
	IPtr<ComponentHandler>			handler;
	IPtr<IPlugView>					view;
	IPtr<PlugFrame>					frame;
	bool							separate_controller=false;

	double							sample_rate=44100.0;
	bool							active=false;
	bool							processing=false;

	// busses
	std::vector<int32>				in_bus_channels;
	std::vector<int32>				out_bus_channels;
	bool							has_event_in=false;
	bool							has_event_out=false;

	// parameters
	std::vector<ParamID>			param_ids;
	std::vector<std::string>		param_names;
	std::unique_ptr<std::atomic<float>[]> param_values;
	std::map<ParamID,int>			param_index_map;

	// programs
	ProgramListID					program_list=kNoProgramListId;
	int32							program_count=0;
	ParamID							program_param=kNoParamId;
	std::string						custom_program_name;

	// midi mapping [channel][controller] -> parameter
	std::vector<ParamID>			midi_map;
	uint16_t						active_notes[16][8];	// bit sets of sounding notes per channel

	// queues
	SpinLock						proc_lock;
	std::vector<ParamChange>		proc_pending;
	std::vector<ParamChange>		proc_work;
	SpinLock						ctrl_lock;
	std::vector<ParamChange>		ctrl_pending;
	std::vector<ParamChange>		ctrl_work;

	// processing helpers
	ParameterChanges				in_changes;
	ParameterChanges				out_changes;
	EventList						in_events;
	EventList						out_events;
	std::vector<AudioBusBuffers>	in_buffers;
	std::vector<AudioBusBuffers>	out_buffers;
	std::vector<ADX_MIDI_EVENT>		midi_out;

	// state chunk
	std::vector<char>				chunk;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
tresult PLUGIN_API ComponentHandler::beginEdit(ParamID){return kResultTrue;}
tresult PLUGIN_API ComponentHandler::endEdit(ParamID){return kResultTrue;}
tresult PLUGIN_API ComponentHandler::performEdit(ParamID id,ParamValue value)
{
	plugin->on_perform_edit(id,value);
	return kResultTrue;
}
tresult PLUGIN_API ComponentHandler::restartComponent(int32 flags)
{
	plugin->on_restart(flags);
	return kResultTrue;
}

tresult PLUGIN_API PlugFrame::resizeView(IPlugView* view,ViewRect* new_size)
{
	if(!view || !new_size)
		return kInvalidArgument;
	plugin->on_resize_view(view,new_size);
	return kResultTrue;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ask for stereo main busses where the plugin starts with mono ones (only kept if the plugin accepts)
void Vst3Plugin::negotiate_stereo(void)
{
	if(!processor || !component)
		return;
	std::vector<SpeakerArrangement> arr[2];
	bool change=false;
	for(int dir=0;dir<2;dir++)
	{
		BusDirection const bd=dir?kOutput:kInput;
		int32 const n=component->getBusCount(kAudio,bd);
		for(int32 b=0;b<n;b++)
		{
			SpeakerArrangement sa=0;
			if(processor->getBusArrangement(bd,b,sa)!=kResultTrue)
				return;
			BusInfo bi;
			if(component->getBusInfo(kAudio,bd,b,bi)==kResultTrue && bi.busType==kMain && SpeakerArr::getChannelCount(sa)==1)
			{
				sa=SpeakerArr::kStereo;
				change=true;
			}
			arr[dir].push_back(sa);
		}
	}
	if(!change)
		return;
	tresult const r=processor->setBusArrangements(arr[0].empty()?nullptr:arr[0].data(),(int32)arr[0].size(),arr[1].empty()?nullptr:arr[1].data(),(int32)arr[1].size());
	fprintf(stderr,"aodix: vst3: stereo main busses %s\n",r==kResultTrue?"accepted":"rejected, keeping the plugin's layout");
}

bool Vst3Plugin::init(VST3::Hosting::Module::Ptr mod,const VST3::Hosting::ClassInfo& info,std::string& error)
{
	module=mod;
	name=info.name();
	uniqueID=uid_hash(info.ID());
	isSynth=is_instrument(info.subCategories());
	programChunks=true;

	const VST3::Hosting::PluginFactory& factory=module->getFactory();

	component=factory.createInstance<IComponent>(info.ID());
	if(!component)
	{
		error="The plugin component could not be created.";
		return false;
	}

	if(component->initialize(host_application())!=kResultOk)
	{
		error="The plugin component could not be initialized.";
		component=nullptr;
		return false;
	}

	processor=U::cast<IAudioProcessor>(component);
	if(!processor)
	{
		error="The plugin has no audio processor.";
		return false;
	}

	// edit controller: same object or separate class
	controller=U::cast<IEditController>(component);
	if(!controller)
	{
		TUID cid;
		if(component->getControllerClassId(cid)==kResultTrue)
		{
			controller=factory.createInstance<IEditController>(VST3::UID(cid));
			if(controller)
			{
				if(controller->initialize(host_application())!=kResultOk)
					controller=nullptr;
				else
					separate_controller=true;
			}
		}
	}

	if(controller)
	{
		handler=owned(new ComponentHandler(this));
		controller->setComponentHandler(handler);

		// connect component and controller
		if(separate_controller)
		{
			auto comp_cp=U::cast<IConnectionPoint>(component);
			auto ctrl_cp=U::cast<IConnectionPoint>(controller);
			if(comp_cp && ctrl_cp)
			{
				comp_to_ctrl=owned(new ConnectionProxy(comp_cp));
				ctrl_to_comp=owned(new ConnectionProxy(ctrl_cp));
				comp_to_ctrl->connect(ctrl_cp);
				ctrl_to_comp->connect(comp_cp);
			}
		}

		// synchronize the controller with the component state
		MemoryStream stream;
		if(component->getState(&stream)==kResultTrue)
		{
			stream.seek(0,IBStream::kIBSeekSet,nullptr);
			controller->setComponentState(&stream);
		}
	}

	// busses: plugins with a flexible channel layout (e.g. JUCE plugins like Signalizer) may start with mono main
	// busses until the host asks for something else - aodix asks for stereo, like other hosts do. a plugin that only
	// supports mono rejects the arrangement and keeps its busses
	negotiate_stereo();

	// busses: activate all audio busses and the first event busses
	numInputs=0;
	numOutputs=0;
	for(int dir=0;dir<2;dir++)
	{
		BusDirection const bd=dir?kOutput:kInput;
		int32 const n=component->getBusCount(kAudio,bd);
		for(int32 b=0;b<n;b++)
		{
			BusInfo bi;
			if(component->getBusInfo(kAudio,bd,b,bi)!=kResultTrue)
				continue;
			component->activateBus(kAudio,bd,b,true);
			if(dir)
			{
				out_bus_channels.push_back(bi.channelCount);
				numOutputs+=bi.channelCount;
			}
			else
			{
				in_bus_channels.push_back(bi.channelCount);
				numInputs+=bi.channelCount;
			}
		}
	}
	if(component->getBusCount(kEvent,kInput)>0)
	{
		component->activateBus(kEvent,kInput,0,true);
		has_event_in=true;
	}
	if(component->getBusCount(kEvent,kOutput)>0)
	{
		component->activateBus(kEvent,kOutput,0,true);
		has_event_out=true;
	}

	in_buffers.resize(in_bus_channels.size());
	out_buffers.resize(out_bus_channels.size());

	scan_parameters();
	scan_programs();
	scan_midi_mapping();

	// editor available?
	hasEditor=false;
	if(controller)
	{
		IPtr<IPlugView> v=owned(controller->createView(ViewType::kEditor));
		if(v && v->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID)==kResultTrue)
			hasEditor=true;
	}

	memset(active_notes,0,sizeof(active_notes));
	proc_pending.reserve(1024);
	proc_work.reserve(1024);
	ctrl_pending.reserve(1024);
	ctrl_work.reserve(1024);
	midi_out.reserve(512);
	in_events.setMaxSize(2048);
	out_events.setMaxSize(2048);
	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Vst3Plugin::~Vst3Plugin()
{
	editorClose();

	if(processing && processor)
		processor->setProcessing(false);
	if(active && component)
		component->setActive(false);

	if(comp_to_ctrl)
		comp_to_ctrl->disconnect();
	if(ctrl_to_comp)
		ctrl_to_comp->disconnect();
	comp_to_ctrl=nullptr;
	ctrl_to_comp=nullptr;

	if(controller)
	{
		controller->setComponentHandler(nullptr);
		if(separate_controller)
			controller->terminate();
	}
	controller=nullptr;
	processor=nullptr;

	if(component)
		component->terminate();
	component=nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Vst3Plugin::scan_parameters(void)
{
	param_ids.clear();
	param_names.clear();
	param_index_map.clear();

	if(!controller)
	{
		numParams=0;
		param_values.reset(new std::atomic<float>[1]);
		return;
	}

	int32 const n=controller->getParameterCount();
	std::vector<float> values;

	for(int32 i=0;i<n;i++)
	{
		ParameterInfo pi;
		if(controller->getParameterInfo(i,pi)!=kResultTrue)
			continue;

		param_index_map[pi.id]=(int)param_ids.size();
		param_ids.push_back(pi.id);
		param_names.push_back(utf16_to_utf8(pi.shortTitle[0]?pi.shortTitle:pi.title));
		values.push_back((float)controller->getParamNormalized(pi.id));

		if((pi.flags & ParameterInfo::kIsProgramChange) && pi.unitId==kRootUnitId && program_param==kNoParamId)
			program_param=pi.id;
	}

	numParams=(int)param_ids.size();
	param_values.reset(new std::atomic<float>[std::max(numParams,1)]);
	for(int i=0;i<numParams;i++)
		param_values[i].store(values[i]);

	in_changes.setMaxParameters(std::max(numParams,1));
	out_changes.setMaxParameters(std::max(numParams,1));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Vst3Plugin::scan_programs(void)
{
	numPrograms=0;
	program_count=0;
	program_list=kNoProgramListId;

	auto units=U::cast<IUnitInfo>(controller);
	if(!units || program_param==kNoParamId)
		return;

	// program list of the root unit
	int32 const nu=units->getUnitCount();
	for(int32 u=0;u<nu;u++)
	{
		UnitInfo ui;
		if(units->getUnitInfo(u,ui)==kResultTrue && ui.id==kRootUnitId)
			program_list=ui.programListId;
	}

	if(program_list==kNoProgramListId && units->getProgramListCount()>0)
	{
		ProgramListInfo pli;
		if(units->getProgramListInfo(0,pli)==kResultTrue)
			program_list=pli.id;
	}

	int32 const nl=units->getProgramListCount();
	for(int32 l=0;l<nl;l++)
	{
		ProgramListInfo pli;
		if(units->getProgramListInfo(l,pli)==kResultTrue && pli.id==program_list)
			program_count=pli.programCount;
	}

	numPrograms=(program_count>0)?program_count:0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Vst3Plugin::scan_midi_mapping(void)
{
	midi_map.assign(16*131,kNoParamId);

	auto mapping=U::cast<IMidiMapping>(controller);
	if(!mapping)
		return;

	for(int ch=0;ch<16;ch++)
	{
		for(int cc=0;cc<131;cc++)
		{
			ParamID id=kNoParamId;
			if(mapping->getMidiControllerAssignment(0,(int16)ch,(CtrlNumber)cc,id)==kResultTrue)
				midi_map[ch*131+cc]=id;
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int Vst3Plugin::param_index(ParamID id)
{
	auto it=param_index_map.find(id);
	return it==param_index_map.end()?-1:it->second;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Vst3Plugin::setup_processing(void)
{
	ProcessSetup setup;
	setup.processMode=kRealtime;
	setup.symbolicSampleSize=kSample32;
	setup.maxSamplesPerBlock=2048;
	setup.sampleRate=sample_rate;
	processor->setupProcessing(setup);
}

void Vst3Plugin::setSampleRate(double rate)
{
	if(rate<=0.0)
		return;
	if(rate==sample_rate && active)
		return;

	sample_rate=rate;

	if(active)
	{
		setActive(false);
		setActive(true);
	}
}

void Vst3Plugin::setActive(bool on)
{
	if(on==active)
		return;

	if(on)
	{
		setup_processing();
		component->setActive(true);
		processing=(processor->setProcessing(true)==kResultTrue) || true;
		active=true;
	}
	else
	{
		if(processing)
			processor->setProcessing(false);
		processing=false;
		component->setActive(false);
		active=false;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// parameters
void Vst3Plugin::push_processor_change(ParamID id,ParamValue v,int32 offset)
{
	proc_lock.lock();
	if(proc_pending.size()<4096)
		proc_pending.push_back({id,v,offset});
	proc_lock.unlock();
}

void Vst3Plugin::push_controller_change(ParamID id,ParamValue v)
{
	ctrl_lock.lock();
	if(ctrl_pending.size()<4096)
		ctrl_pending.push_back({id,v,0});
	ctrl_lock.unlock();
}

float Vst3Plugin::getParameter(int index)
{
	if(index<0 || index>=numParams)
		return 0.0f;
	return param_values[index].load(std::memory_order_relaxed);
}

void Vst3Plugin::setParameter(int index,float value)
{
	if(index<0 || index>=numParams)
		return;
	value=std::min(1.0f,std::max(0.0f,value));
	param_values[index].store(value,std::memory_order_relaxed);
	push_processor_change(param_ids[index],value,0);
	push_controller_change(param_ids[index],value);
}

void Vst3Plugin::getParamName(int index,char* buf)
{
	buf[0]=0;
	if(index>=0 && index<numParams)
		copy_str(buf,param_names[index],31);
}

void Vst3Plugin::getParamDisplay(int index,char* buf)
{
	buf[0]=0;
	if(index<0 || index>=numParams || !controller)
		return;
	String128 s;
	s[0]=0;
	if(controller->getParamStringByValue(param_ids[index],param_values[index].load(),s)==kResultTrue)
		copy_str(buf,utf16_to_utf8(s),31);
}

void Vst3Plugin::on_perform_edit(ParamID id,ParamValue value)
{
	int const index=param_index(id);
	if(index>=0)
		param_values[index].store((float)value);
	push_processor_change(id,value,0);
	if(index>=0)
		host->plugin_automate(this,index,(float)value);
}

void Vst3Plugin::on_restart(int32 flags)
{
	if(flags & (kParamValuesChanged|kParamTitlesChanged))
	{
		for(int i=0;i<numParams;i++)
			param_values[i].store((float)controller->getParamNormalized(param_ids[i]));
		if(flags & kParamTitlesChanged)
			for(int i=0;i<numParams;i++)
			{
				ParameterInfo pi;
				if(controller->getParameterInfo(i,pi)==kResultTrue)
					param_names[i]=utf16_to_utf8(pi.shortTitle[0]?pi.shortTitle:pi.title);
			}
	}

	if(flags & kReloadComponent)
	{
		if(active)
		{
			setActive(false);
			setActive(true);
		}
	}

	host->plugin_update_display(this);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// programs
int Vst3Plugin::getProgram(void)
{
	if(program_count<=0 || program_param==kNoParamId)
		return 0;
	int const index=param_index(program_param);
	if(index<0)
		return 0;
	if(program_count==1)
		return 0;
	int const p=(int)(param_values[index].load()*(program_count-1)+0.5f);
	return std::max(0,std::min(program_count-1,p));
}

void Vst3Plugin::setProgram(int p)
{
	if(program_count<=0 || program_param==kNoParamId)
		return;
	int const index=param_index(program_param);
	if(index<0)
		return;
	p=std::max(0,std::min(program_count-1,p));
	float const v=(program_count>1)?float(p)/float(program_count-1):0.0f;
	param_values[index].store(v);
	custom_program_name.clear();

	// the controller updates its parameters when the program changes (gui thread)
	if(controller)
	{
		controller->setParamNormalized(program_param,v);
		for(int i=0;i<numParams;i++)
			param_values[i].store((float)controller->getParamNormalized(param_ids[i]));
	}
	push_processor_change(program_param,v,0);
}

void Vst3Plugin::getProgramName(char* buf)
{
	if(!custom_program_name.empty())
	{
		copy_str(buf,custom_program_name,24);
		return;
	}
	if(!getProgramNameIndexed(getProgram(),buf))
		strcpy(buf,"Default");
}

void Vst3Plugin::setProgramName(const char* n)
{
	// vst3 programs can't be renamed by the host; the name is kept for display
	custom_program_name=n?n:"";
}

bool Vst3Plugin::getProgramNameIndexed(int index,char* buf)
{
	buf[0]=0;
	auto units=U::cast<IUnitInfo>(controller);
	if(!units || program_list==kNoProgramListId || index<0 || index>=program_count)
		return false;
	String128 s;
	s[0]=0;
	if(units->getProgramName(program_list,index,s)!=kResultTrue)
		return false;
	copy_str(buf,utf16_to_utf8(s),24);
	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// state: "ADX3VST3" + component state + controller state
static void put_u32(std::vector<char>& v,uint32_t x)
{
	for(int i=0;i<4;i++)
		v.push_back((char)((x>>(i*8))&0xFF));
}

static uint32_t get_u32(const unsigned char* p)
{
	return p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24);
}

int Vst3Plugin::getChunk(void** data,bool preset)
{
	(void)preset;
	chunk.clear();
	chunk.insert(chunk.end(),"ADX3VST3",(const char*)"ADX3VST3"+8);

	MemoryStream comp;
	component->getState(&comp);
	put_u32(chunk,(uint32_t)comp.getSize());
	chunk.insert(chunk.end(),comp.getData(),comp.getData()+comp.getSize());

	MemoryStream ctrl;
	if(controller)
		controller->getState(&ctrl);
	put_u32(chunk,(uint32_t)ctrl.getSize());
	chunk.insert(chunk.end(),ctrl.getData(),ctrl.getData()+ctrl.getSize());

	*data=chunk.data();
	return (int)chunk.size();
}

void Vst3Plugin::setChunk(const void* data,int size,bool preset)
{
	(void)preset;
	const unsigned char* p=(const unsigned char*)data;
	if(!p || size<=0)
		return;

	const unsigned char* comp_data=p;
	uint32_t comp_size=(uint32_t)size;
	const unsigned char* ctrl_data=nullptr;
	uint32_t ctrl_size=0;

	if(size>=16 && memcmp(p,"ADX3VST3",8)==0)
	{
		comp_size=get_u32(p+8);
		comp_data=p+12;
		if(12+(size_t)comp_size+4<=(size_t)size)
		{
			ctrl_size=get_u32(p+12+comp_size);
			ctrl_data=p+16+comp_size;
			if(16+(size_t)comp_size+ctrl_size>(size_t)size)
				ctrl_size=0;
		}
		else
			comp_size=std::min<uint32_t>(comp_size,(uint32_t)size-12);
	}

	{
		MemoryStream s((void*)comp_data,comp_size);
		component->setState(&s);
	}

	if(controller)
	{
		MemoryStream s((void*)comp_data,comp_size);
		controller->setComponentState(&s);
		if(ctrl_data && ctrl_size>0)
		{
			MemoryStream c((void*)ctrl_data,ctrl_size);
			controller->setState(&c);
		}
		for(int i=0;i<numParams;i++)
			param_values[i].store((float)controller->getParamNormalized(param_ids[i]));
	}

	custom_program_name.clear();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// audio thread
void Vst3Plugin::process(float** inputs,float** outputs,int frames,const ADX_MIDI_EVENT* events,int num_events)
{
	if(!active || frames<=0)
	{
		for(int o=0;o<numOutputs;o++)
			memset(outputs[o],0,sizeof(float)*std::max(frames,0));
		return;
	}

	// pending parameter changes
	in_changes.clearQueue();
	proc_lock.lock();
	proc_work.swap(proc_pending);
	proc_lock.unlock();
	for(const ParamChange& c:proc_work)
	{
		int32 qi=0;
		IParamValueQueue* q=in_changes.addParameterData(c.id,qi);
		if(q)
		{
			int32 pi=0;
			q->addPoint(std::min(c.offset,frames-1),c.value,pi);
		}
	}
	proc_work.clear();

	// midi events
	in_events.clear();
	if(has_event_in)
	{
		for(int e=0;e<num_events;e++)
		{
			const ADX_MIDI_EVENT& m=events[e];
			unsigned char const status=m.midiData[0]&0xF0;
			int const ch=m.midiData[0]&0x0F;
			int const d1=m.midiData[1]&0x7F;
			int const d2=m.midiData[2]&0x7F;
			int32 const offset=std::max(0,std::min(m.deltaFrames,frames-1));

			Event ev;
			memset(&ev,0,sizeof(ev));
			ev.busIndex=0;
			ev.sampleOffset=offset;

			if(status==0x90 && d2>0)
			{
				ev.type=Event::kNoteOnEvent;
				ev.noteOn.channel=(int16)ch;
				ev.noteOn.pitch=(int16)d1;
				ev.noteOn.velocity=d2/127.0f;
				ev.noteOn.noteId=-1;
				in_events.addEvent(ev);
				active_notes[ch][d1>>4]|=(uint16_t)(1<<(d1&15));
			}
			else if(status==0x80 || status==0x90)
			{
				ev.type=Event::kNoteOffEvent;
				ev.noteOff.channel=(int16)ch;
				ev.noteOff.pitch=(int16)d1;
				ev.noteOff.velocity=(status==0x80)?d2/127.0f:0.5f;
				ev.noteOff.noteId=-1;
				in_events.addEvent(ev);
				active_notes[ch][d1>>4]&=(uint16_t)~(1<<(d1&15));
			}
			else if(status==0xA0)
			{
				ev.type=Event::kPolyPressureEvent;
				ev.polyPressure.channel=(int16)ch;
				ev.polyPressure.pitch=(int16)d1;
				ev.polyPressure.pressure=d2/127.0f;
				ev.polyPressure.noteId=-1;
				in_events.addEvent(ev);
			}
			else
			{
				// controllers, channel pressure, pitch bend and program change through the midi mapping
				int cc=-1;
				ParamValue value=0.0;
				if(status==0xB0) { cc=d1; value=d2/127.0; }
				if(status==0xD0) { cc=kAfterTouch; value=d1/127.0; }
				if(status==0xE0) { cc=kPitchBend; value=((d2<<7)|d1)/16383.0; }
				if(status==0xC0) { cc=kCtrlProgramChange; value=d1/127.0; }

				if(cc>=0)
				{
					ParamID const id=midi_map[ch*131+cc];
					if(id!=kNoParamId)
					{
						int32 qi=0;
						IParamValueQueue* q=in_changes.addParameterData(id,qi);
						if(q)
						{
							int32 pi=0;
							q->addPoint(offset,value,pi);
						}
						int const index=param_index(id);
						if(index>=0)
							param_values[index].store((float)value);
					}
					else if(status==0xC0 && program_param!=kNoParamId && program_count>0)
					{
						// program change to the program list
						int const prg=std::min(d1,program_count-1);
						ParamValue const v=(program_count>1)?double(prg)/double(program_count-1):0.0;
						int32 qi=0;
						IParamValueQueue* q=in_changes.addParameterData(program_param,qi);
						if(q)
						{
							int32 pi=0;
							q->addPoint(offset,v,pi);
						}
					}
					else if(status==0xB0 && (d1==120 || d1==123))
					{
						// all sounds off / all notes off (aodix midi panic): release the sounding notes
						for(int n=0;n<128;n++)
						{
							if(active_notes[ch][n>>4] & (1<<(n&15)))
							{
								Event off;
								memset(&off,0,sizeof(off));
								off.type=Event::kNoteOffEvent;
								off.sampleOffset=offset;
								off.noteOff.channel=(int16)ch;
								off.noteOff.pitch=(int16)n;
								off.noteOff.velocity=0.0f;
								off.noteOff.noteId=-1;
								in_events.addEvent(off);
							}
						}
						memset(active_notes[ch],0,sizeof(active_notes[ch]));
					}
				}
			}
		}
	}

	// transport
	ProcessContext ctx;
	memset(&ctx,0,sizeof(ctx));
	const ADX_TIME_INFO* ti=host->plugin_time_info();
	ctx.sampleRate=sample_rate;
	if(ti)
	{
		ctx.state=ProcessContext::kTempoValid | ProcessContext::kTimeSigValid | ProcessContext::kProjectTimeMusicValid | ProcessContext::kBarPositionValid | ProcessContext::kCycleValid;
		if(ti->flags & ADX_TRANSPORT_PLAYING) ctx.state|=ProcessContext::kPlaying;
		if(ti->flags & ADX_TRANSPORT_CYCLE_ACTIVE) ctx.state|=ProcessContext::kCycleActive;
		if(ti->flags & ADX_TRANSPORT_RECORDING) ctx.state|=ProcessContext::kRecording;
		ctx.projectTimeSamples=(TSamples)ti->samplePos;
		ctx.projectTimeMusic=ti->ppqPos;
		ctx.barPositionMusic=ti->barStartPos;
		ctx.cycleStartMusic=ti->cycleStartPos;
		ctx.cycleEndMusic=ti->cycleEndPos;
		ctx.tempo=ti->tempo;
		ctx.timeSigNumerator=ti->timeSigNumerator>0?ti->timeSigNumerator:4;
		ctx.timeSigDenominator=ti->timeSigDenominator>0?ti->timeSigDenominator:4;
	}

	// audio busses (aodix uses flat channel arrays)
	int ch_index=0;
	for(size_t b=0;b<in_buffers.size();b++)
	{
		in_buffers[b].numChannels=in_bus_channels[b];
		in_buffers[b].silenceFlags=0;
		in_buffers[b].channelBuffers32=inputs+ch_index;
		ch_index+=in_bus_channels[b];
	}
	ch_index=0;
	for(size_t b=0;b<out_buffers.size();b++)
	{
		out_buffers[b].numChannels=out_bus_channels[b];
		out_buffers[b].silenceFlags=0;
		out_buffers[b].channelBuffers32=outputs+ch_index;
		ch_index+=out_bus_channels[b];
	}

	out_changes.clearQueue();
	out_events.clear();

	ProcessData data;
	data.processMode=kRealtime;
	data.symbolicSampleSize=kSample32;
	data.numSamples=frames;
	data.numInputs=(int32)in_buffers.size();
	data.numOutputs=(int32)out_buffers.size();
	data.inputs=in_buffers.empty()?nullptr:in_buffers.data();
	data.outputs=out_buffers.empty()?nullptr:out_buffers.data();
	data.inputParameterChanges=&in_changes;
	data.outputParameterChanges=&out_changes;
	data.inputEvents=has_event_in?&in_events:nullptr;
	data.outputEvents=has_event_out?&out_events:nullptr;
	data.processContext=&ctx;

	processor->process(data);

	// parameter changes made by the plugin itself
	int32 const nchg=out_changes.getParameterCount();
	for(int32 i=0;i<nchg;i++)
	{
		IParamValueQueue* q=out_changes.getParameterData(i);
		if(!q || q->getPointCount()<=0)
			continue;
		int32 off=0;
		ParamValue v=0;
		if(q->getPoint(q->getPointCount()-1,off,v)!=kResultTrue)
			continue;
		ParamID const id=q->getParameterId();
		int const index=param_index(id);
		if(index>=0)
			param_values[index].store((float)v);
		push_controller_change(id,v);
	}

	// midi output (to aodix midi-out wires)
	if(has_event_out)
	{
		midi_out.clear();
		int32 const nev=out_events.getEventCount();
		for(int32 i=0;i<nev;i++)
		{
			Event ev;
			if(out_events.getEvent(i,ev)!=kResultTrue)
				continue;
			ADX_MIDI_EVENT m;
			memset(&m,0,sizeof(m));
			m.deltaFrames=ev.sampleOffset;
			if(ev.type==Event::kNoteOnEvent)
			{
				m.midiData[0]=(unsigned char)(0x90|(ev.noteOn.channel&15));
				m.midiData[1]=(unsigned char)(ev.noteOn.pitch&127);
				m.midiData[2]=(unsigned char)std::max(1,std::min(127,(int)(ev.noteOn.velocity*127.0f+0.5f)));
			}
			else if(ev.type==Event::kNoteOffEvent)
			{
				m.midiData[0]=(unsigned char)(0x80|(ev.noteOff.channel&15));
				m.midiData[1]=(unsigned char)(ev.noteOff.pitch&127);
				m.midiData[2]=(unsigned char)std::min(127,(int)(ev.noteOff.velocity*127.0f+0.5f));
			}
			else if(ev.type==Event::kPolyPressureEvent)
			{
				m.midiData[0]=(unsigned char)(0xA0|(ev.polyPressure.channel&15));
				m.midiData[1]=(unsigned char)(ev.polyPressure.pitch&127);
				m.midiData[2]=(unsigned char)std::min(127,(int)(ev.polyPressure.pressure*127.0f+0.5f));
			}
			else if(ev.type==Event::kLegacyMIDICCOutEvent)
			{
				int const c=ev.midiCCOut.channel&15;
				int const n=ev.midiCCOut.controlNumber;
				if(n<128) { m.midiData[0]=(unsigned char)(0xB0|c); m.midiData[1]=(unsigned char)n; m.midiData[2]=(unsigned char)(ev.midiCCOut.value&127); }
				else if(n==kAfterTouch) { m.midiData[0]=(unsigned char)(0xD0|c); m.midiData[1]=(unsigned char)(ev.midiCCOut.value&127); }
				else if(n==kPitchBend) { m.midiData[0]=(unsigned char)(0xE0|c); m.midiData[1]=(unsigned char)(ev.midiCCOut.value&127); m.midiData[2]=(unsigned char)(ev.midiCCOut.value2&127); }
				else if(n==kCtrlProgramChange) { m.midiData[0]=(unsigned char)(0xC0|c); m.midiData[1]=(unsigned char)(ev.midiCCOut.value&127); }
				else continue;
			}
			else
				continue;
			midi_out.push_back(m);
		}
		if(!midi_out.empty())
			host->plugin_midi_out(this,midi_out.data(),(int)midi_out.size());
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// gui thread housekeeping: deliver parameter changes to the edit controller
void Vst3Plugin::idle(void)
{
	ctrl_lock.lock();
	ctrl_work.swap(ctrl_pending);
	ctrl_lock.unlock();

	if(controller)
		for(const ParamChange& c:ctrl_work)
			controller->setParamNormalized(c.id,c.value);

	ctrl_work.clear();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// editor
bool Vst3Plugin::editorGetSize(int* width,int* height)
{
	if(!controller || !hasEditor)
		return false;
	if(!view)
	{
		view=owned(controller->createView(ViewType::kEditor));
		if(!view)
			return false;
	}
	ViewRect r;
	if(view->getSize(&r)!=kResultTrue)
		return false;
	*width=r.getWidth();
	*height=r.getHeight();
	return true;
}

bool Vst3Plugin::editorOpen(unsigned long parent)
{
	if(!controller || !hasEditor || !parent)
		return false;
	if(!view)
	{
		view=owned(controller->createView(ViewType::kEditor));
		if(!view)
			return false;
	}
	if(view->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID)!=kResultTrue)
	{
		view=nullptr;
		return false;
	}

	frame=owned(new PlugFrame(this));
	view->setFrame(frame);

	if(view->attached((void*)(uintptr_t)parent,kPlatformTypeX11EmbedWindowID)!=kResultTrue)
	{
		view->setFrame(nullptr);
		view=nullptr;
		frame=nullptr;
		return false;
	}
	return true;
}

void Vst3Plugin::editorClose(void)
{
	if(view)
	{
		view->removed();
		view->setFrame(nullptr);
		view=nullptr;
	}
	frame=nullptr;
}

bool Vst3Plugin::editorCanResize(void)
{
	return view && view->canResize()==kResultTrue;
}

void Vst3Plugin::editorResized(int width,int height)
{
	if(!view)
		return;
	ViewRect r(0,0,width,height);
	ViewRect cur;
	if(view->getSize(&cur)==kResultTrue && cur.getWidth()==width && cur.getHeight()==height)
		return;
	if(view->canResize()==kResultTrue)
	{
		view->checkSizeConstraint(&r);
		view->onSize(&r);

		// the view may have limited the size: make the frame follow
		if(r.getWidth()!=width || r.getHeight()!=height)
			host->plugin_resize_editor(this,r.getWidth(),r.getHeight());
	}
}

void Vst3Plugin::on_resize_view(IPlugView* v,ViewRect* r)
{
	// the plugin wants another editor size: resize the aodix frame window, then confirm to the view
	host->plugin_resize_editor(this,r->getWidth(),r->getHeight());
	v->onSize(r);
}

} // namespace

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// plugin library functions
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ends_with_nocase(const char* s,const char* suffix)
{
	size_t const n=strlen(s);
	size_t const m=strlen(suffix);
	return n>=m && strcasecmp(s+n-m,suffix)==0;
}

bool adx_plugin_is_candidate(const char* name,bool is_dir)
{
	// vst3 plugins on linux are bundles: Name.vst3/Contents/x86_64-linux/Name.so (older ones are single files)
	(void)is_dir;
	return ends_with_nocase(name,".vst3");
}

// class list from moduleinfo.json (no need to load the plugin binary) or from the factory
static bool scan_classes(const char* path,std::vector<AdxPluginDesc>& out,std::vector<VST3::UID>* uids)
{
	const std::string audio_class=kVstAudioEffectClass;

	if(!uids)
	{
		auto info_path=VST3::Hosting::Module::getModuleInfoPath(path);
		if(info_path)
		{
			std::ifstream f(*info_path);
			std::stringstream ss;
			ss<<f.rdbuf();
			std::string const json=ss.str();
			auto info=ModuleInfoLib::parseJson(json,nullptr);
			if(info)
			{
				size_t const before=out.size();
				for(const auto& c:info->classes)
				{
					if(c.category!=audio_class)
						continue;
					auto uid=VST3::UID::fromString(c.cid);
					if(!uid)
						continue;
					out.push_back({path,c.name,uid_hash(*uid),is_instrument(c.subCategories)});
				}
				if(out.size()>before)
					return true;
			}
		}
	}

	std::string error;
	auto module=get_module(path,error);
	if(!module)
	{
		fprintf(stderr,"aodix: vst3: %s: %s\n",path,error.c_str());
		return false;
	}

	for(const auto& c:module->getFactory().classInfos())
	{
		if(c.category()!=audio_class)
			continue;
		out.push_back({path,c.name(),uid_hash(c.ID()),is_instrument(c.subCategories())});
		if(uids)
			uids->push_back(c.ID());
	}
	return true;
}

int vst3_plugin_scan(const char* path,std::vector<AdxPluginDesc>& out)
{
	size_t const before=out.size();
	scan_classes(path,out,nullptr);
	return (int)(out.size()-before);
}

AdxPlugin* vst3_plugin_load(const char* path,int32_t wanted_id,AdxPluginHost* host,char* error,int error_size)
{
	std::string err;
	auto module=get_module(path,err);
	if(!module)
	{
		snprintf(error,error_size,"%s",err.empty()?"The VST3 module could not be loaded.":err.c_str());
		return nullptr;
	}

	// choose the class: matching id, otherwise the first instrument, otherwise the first audio class
	const std::string audio_class=kVstAudioEffectClass;
	const VST3::Hosting::ClassInfo* chosen=nullptr;
	const VST3::Hosting::ClassInfo* first=nullptr;
	const VST3::Hosting::ClassInfo* first_instrument=nullptr;
	auto infos=module->getFactory().classInfos();

	for(const auto& c:infos)
	{
		if(c.category()!=audio_class)
			continue;
		if(!first)
			first=&c;
		if(!first_instrument && is_instrument(c.subCategories()))
			first_instrument=&c;
		if(wanted_id!=0 && uid_hash(c.ID())==wanted_id)
			chosen=&c;
	}

	if(!chosen)
		chosen=first_instrument?first_instrument:first;

	if(!chosen)
	{
		snprintf(error,error_size,"The VST3 module contains no audio plugin.");
		return nullptr;
	}

	Vst3Plugin* p=new Vst3Plugin(host);
	if(!p->init(module,*chosen,err))
	{
		snprintf(error,error_size,"%s",err.c_str());
		delete p;
		return nullptr;
	}
	return p;
}
