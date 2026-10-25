/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - LADSPA plugin hosting
//
// LADSPA (Linux Audio Developer's Simple Plugin API) plugins are shared libraries exporting ladspa_descriptor(index); one
// library may contain several plugins. A plugin has audio ports and control ports (floats with range hints), no midi and no
// editor. Control input ports become Aodix parameters (normalized 0..1 <-> plugin range), the state stored in projects is
// the list of control values. Every LADSPA plugin has a unique 32-bit id, which Aodix stores in its projects directly.
//
// The interface definitions below are written from the public LADSPA 1.1 specification (ABI compatible with ladspa.h).
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AdxPlugin.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <climits>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LADSPA 1.1 interface
namespace ladspa
{
	typedef float Data;
	typedef int Properties;
	typedef int PortDescriptor;
	typedef int HintDescriptor;
	typedef void* Handle;

	enum
	{
		PORT_INPUT=0x1, PORT_OUTPUT=0x2, PORT_CONTROL=0x4, PORT_AUDIO=0x8,

		HINT_BOUNDED_BELOW=0x1, HINT_BOUNDED_ABOVE=0x2, HINT_TOGGLED=0x4, HINT_SAMPLE_RATE=0x8,
		HINT_LOGARITHMIC=0x10, HINT_INTEGER=0x20,
		HINT_DEFAULT_MASK=0x3C0, HINT_DEFAULT_NONE=0x0, HINT_DEFAULT_MINIMUM=0x40, HINT_DEFAULT_LOW=0x80,
		HINT_DEFAULT_MIDDLE=0xC0, HINT_DEFAULT_HIGH=0x100, HINT_DEFAULT_MAXIMUM=0x140, HINT_DEFAULT_0=0x200,
		HINT_DEFAULT_1=0x240, HINT_DEFAULT_100=0x280, HINT_DEFAULT_440=0x2C0,
	};

	struct PortRangeHint
	{
		HintDescriptor	HintDescriptor_;
		Data			LowerBound;
		Data			UpperBound;
	};

	struct Descriptor
	{
		unsigned long			UniqueID;
		const char*				Label;
		Properties				Properties_;
		const char*				Name;
		const char*				Maker;
		const char*				Copyright;
		unsigned long			PortCount;
		const PortDescriptor*	PortDescriptors;
		const char* const*		PortNames;
		const PortRangeHint*	PortRangeHints;
		void*					ImplementationData;
		Handle					(*instantiate)(const Descriptor*,unsigned long sample_rate);
		void					(*connect_port)(Handle,unsigned long port,Data* location);
		void					(*activate)(Handle);
		void					(*run)(Handle,unsigned long sample_count);
		void					(*run_adding)(Handle,unsigned long sample_count);
		void					(*set_run_adding_gain)(Handle,Data gain);
		void					(*deactivate)(Handle);
		void					(*cleanup)(Handle);
	};

	typedef const Descriptor* (*DescriptorFunction)(unsigned long index);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// library cache: libraries are opened once and never closed (some plugins do not survive being unloaded)
static std::mutex g_lib_mutex;
static std::map<std::string,ladspa::DescriptorFunction> g_libs;

static ladspa::DescriptorFunction ladspa_open(const char* path,std::string& error)
{
	std::lock_guard<std::mutex> lock(g_lib_mutex);
	auto it=g_libs.find(path);
	if(it!=g_libs.end())
		return it->second;

	void* lib=dlopen(path,RTLD_NOW|RTLD_LOCAL);
	if(!lib)
	{
		const char* e=dlerror();
		error=e?e:"dlopen failed";
		return nullptr;
	}

	ladspa::DescriptorFunction fn=(ladspa::DescriptorFunction)dlsym(lib,"ladspa_descriptor");
	if(!fn)
	{
		error="not a LADSPA plugin library (no ladspa_descriptor)";
		dlclose(lib);
		return nullptr;
	}

	g_libs[path]=fn;
	return fn;
}

static void copy_str(char* dst,const char* src,int max_chars)
{
	snprintf(dst,max_chars+1,"%s",src?src:"");
}

// largest block passed to process() (aodix uses at most 2048 frames)
static const int MAX_PROCESS_FRAMES=8192;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class LadspaPlugin : public AdxPlugin
{
public:
	LadspaPlugin(const ladspa::Descriptor* d,AdxPluginHost* h);
	~LadspaPlugin() override;

	const char*	format(void) override { return "LADSPA"; }
	void		getEffectName(char* buf) override { copy_str(buf,desc->Name,64); }

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

	bool		editorGetSize(int*,int*) override { return false; }
	bool		editorOpen(unsigned long) override { return false; }
	void		editorClose(void) override {}
	void		idle(void) override {}

private:
	struct Control
	{
		unsigned long	port;
		float			lower;		// range at the current sample rate
		float			upper;
		int				hints;
		std::string		name;
	};

	void	update_ranges(void);
	float	default_value(const Control& c) const;
	float	to_value(const Control& c,float normalized) const;
	float	to_normalized(const Control& c,float value) const;
	bool	instantiate(void);
	void	release(void);

	const ladspa::Descriptor*	desc;
	AdxPluginHost*				host;
	ladspa::Handle				handle;
	unsigned long				sample_rate;
	bool						active;
	std::string					program_name;

	std::vector<unsigned long>	audio_in;
	std::vector<unsigned long>	audio_out;
	std::vector<Control>		controls;
	std::vector<unsigned long>	control_out;

	std::vector<std::atomic<float>>	target;		// control values set by aodix (gui / automation)
	std::vector<float>				port_value;	// control port memory read by the plugin (written by the audio thread)
	std::vector<float>				out_value;	// control output ports (meters), not used
	std::vector<float>				silence;	// unconnected inputs
	std::vector<float>				scratch;	// unconnected outputs

	std::atomic_flag			busy;		// audio thread vs. (re)instantiation
	std::vector<char>			chunk;
};

LadspaPlugin::LadspaPlugin(const ladspa::Descriptor* d,AdxPluginHost* h)
{
	desc=d;
	host=h;
	(void)host;
	handle=nullptr;
	sample_rate=0;
	active=false;
	program_name="Default";
	busy.clear();

	for(unsigned long p=0;p<desc->PortCount;p++)
	{
		int const pd=desc->PortDescriptors[p];
		if(pd & ladspa::PORT_AUDIO)
		{
			if(pd & ladspa::PORT_INPUT) audio_in.push_back(p);
			else audio_out.push_back(p);
		}
		else if(pd & ladspa::PORT_CONTROL)
		{
			if(pd & ladspa::PORT_INPUT)
			{
				Control c;
				c.port=p;
				c.hints=desc->PortRangeHints?desc->PortRangeHints[p].HintDescriptor_:0;
				c.name=(desc->PortNames && desc->PortNames[p])?desc->PortNames[p]:"";
				c.lower=0.0f;
				c.upper=1.0f;
				controls.push_back(c);
			}
			else
				control_out.push_back(p);
		}
	}

	numInputs=(int)audio_in.size();
	numOutputs=(int)audio_out.size();
	numParams=(int)controls.size();
	numPrograms=1;
	uniqueID=(int32_t)desc->UniqueID;
	version=1;
	hasEditor=false;
	programChunks=true;
	isSynth=false;

	target=std::vector<std::atomic<float>>(controls.size());
	port_value.assign(controls.size(),0.0f);
	out_value.assign(control_out.size(),0.0f);
	silence.assign(MAX_PROCESS_FRAMES,0.0f);
	scratch.assign(MAX_PROCESS_FRAMES,0.0f);

	// default values at 48 kHz until the host sets the real sample rate
	sample_rate=48000;
	update_ranges();
	for(size_t i=0;i<controls.size();i++)
	{
		target[i].store(default_value(controls[i]));
		port_value[i]=target[i].load();
	}
	sample_rate=0;
}

LadspaPlugin::~LadspaPlugin()
{
	release();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ranges and value mapping
void LadspaPlugin::update_ranges(void)
{
	for(Control& c:controls)
	{
		const ladspa::PortRangeHint* h=desc->PortRangeHints?&desc->PortRangeHints[c.port]:nullptr;
		float const sr=(c.hints & ladspa::HINT_SAMPLE_RATE)?float(sample_rate):1.0f;
		bool const below=(c.hints & ladspa::HINT_BOUNDED_BELOW)!=0;
		bool const above=(c.hints & ladspa::HINT_BOUNDED_ABOVE)!=0;

		if(c.hints & ladspa::HINT_TOGGLED)
		{
			c.lower=0.0f;
			c.upper=1.0f;
			continue;
		}

		c.lower=(below && h)?h->LowerBound*sr:0.0f;
		c.upper=(above && h)?h->UpperBound*sr:(c.lower+1.0f);

		// unbounded below: make sure the default fits
		if(!below && above && c.upper<=0.0f)
			c.lower=c.upper-1.0f;
		if(c.upper<=c.lower)
			c.upper=c.lower+1.0f;
	}
}

float LadspaPlugin::default_value(const Control& c) const
{
	float const lo=c.lower;
	float const hi=c.upper;
	bool const lg=(c.hints & ladspa::HINT_LOGARITHMIC) && lo>0.0f && hi>0.0f;

	auto between=[&](float f)->float
	{
		if(lg)
			return expf(logf(lo)*(1.0f-f)+logf(hi)*f);
		return lo*(1.0f-f)+hi*f;
	};

	float v=lo;
	switch(c.hints & ladspa::HINT_DEFAULT_MASK)
	{
	case ladspa::HINT_DEFAULT_MINIMUM:	v=lo; break;
	case ladspa::HINT_DEFAULT_LOW:		v=between(0.25f); break;
	case ladspa::HINT_DEFAULT_MIDDLE:	v=between(0.5f); break;
	case ladspa::HINT_DEFAULT_HIGH:		v=between(0.75f); break;
	case ladspa::HINT_DEFAULT_MAXIMUM:	v=hi; break;
	case ladspa::HINT_DEFAULT_0:		v=0.0f; break;
	case ladspa::HINT_DEFAULT_1:		v=1.0f; break;
	case ladspa::HINT_DEFAULT_100:		v=100.0f; break;
	case ladspa::HINT_DEFAULT_440:		v=440.0f; break;
	default:							v=(lo<=0.0f && hi>=0.0f)?0.0f:lo; break;
	}

	if(c.hints & ladspa::HINT_INTEGER)
		v=roundf(v);
	return v;
}

float LadspaPlugin::to_value(const Control& c,float n) const
{
	n=n<0.0f?0.0f:(n>1.0f?1.0f:n);

	if(c.hints & ladspa::HINT_TOGGLED)
		return n>=0.5f?1.0f:0.0f;

	float v;
	if((c.hints & ladspa::HINT_LOGARITHMIC) && c.lower>0.0f && c.upper>0.0f)
		v=c.lower*powf(c.upper/c.lower,n);
	else
		v=c.lower+n*(c.upper-c.lower);

	if(c.hints & ladspa::HINT_INTEGER)
		v=roundf(v);
	return v;
}

float LadspaPlugin::to_normalized(const Control& c,float v) const
{
	if(c.hints & ladspa::HINT_TOGGLED)
		return v>0.0f?1.0f:0.0f;

	float n;
	if((c.hints & ladspa::HINT_LOGARITHMIC) && c.lower>0.0f && c.upper>0.0f)
		n=(v>0.0f)?logf(v/c.lower)/logf(c.upper/c.lower):0.0f;
	else
		n=(v-c.lower)/(c.upper-c.lower);
	return n<0.0f?0.0f:(n>1.0f?1.0f:n);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// instance life cycle (gui thread, aodix holds the audio lock while changing the sample rate)
bool LadspaPlugin::instantiate(void)
{
	handle=desc->instantiate(desc,sample_rate);
	if(!handle)
	{
		fprintf(stderr,"aodix: ladspa: '%s' could not be instantiated\n",desc->Name);
		return false;
	}

	for(size_t i=0;i<controls.size();i++)
	{
		port_value[i]=target[i].load(std::memory_order_relaxed);
		desc->connect_port(handle,controls[i].port,&port_value[i]);
	}
	for(size_t i=0;i<control_out.size();i++)
		desc->connect_port(handle,control_out[i],&out_value[i]);
	for(unsigned long p:audio_in)
		desc->connect_port(handle,p,silence.data());
	for(unsigned long p:audio_out)
		desc->connect_port(handle,p,scratch.data());
	return true;
}

void LadspaPlugin::release(void)
{
	while(busy.test_and_set(std::memory_order_acquire)) {}
	if(handle)
	{
		if(active && desc->deactivate)
			desc->deactivate(handle);
		if(desc->cleanup)
			desc->cleanup(handle);
		handle=nullptr;
	}
	busy.clear(std::memory_order_release);
}

void LadspaPlugin::setSampleRate(double rate)
{
	unsigned long const sr=(unsigned long)(rate>0.0?rate+0.5:48000.0);
	if(sr==sample_rate && handle)
		return;

	// keep the values (in plugin units) across the new instance, ranges with sample rate hints are rescaled
	std::vector<float> norm(controls.size());
	for(size_t i=0;i<controls.size();i++)
		norm[i]=to_normalized(controls[i],target[i].load());

	bool const was_active=active;
	release();
	active=false;

	unsigned long const old_rate=sample_rate;
	sample_rate=sr;
	update_ranges();
	for(size_t i=0;i<controls.size();i++)
	{
		if(controls[i].hints & ladspa::HINT_SAMPLE_RATE)
			target[i].store(old_rate?to_value(controls[i],norm[i]):default_value(controls[i]));
	}

	while(busy.test_and_set(std::memory_order_acquire)) {}
	instantiate();
	if(handle && was_active)
	{
		if(desc->activate)
			desc->activate(handle);
		active=true;
	}
	busy.clear(std::memory_order_release);
}

void LadspaPlugin::setActive(bool a)
{
	if(!handle)
		setSampleRate(48000.0);
	if(!handle || a==active)
		return;

	while(busy.test_and_set(std::memory_order_acquire)) {}
	if(a)
	{
		if(desc->activate)
			desc->activate(handle);
	}
	else
	{
		if(desc->deactivate)
			desc->deactivate(handle);
	}
	active=a;
	busy.clear(std::memory_order_release);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// parameters
float LadspaPlugin::getParameter(int index)
{
	if(index<0 || index>=numParams)
		return 0.0f;
	return to_normalized(controls[index],target[index].load(std::memory_order_relaxed));
}

void LadspaPlugin::setParameter(int index,float value)
{
	if(index<0 || index>=numParams)
		return;
	target[index].store(to_value(controls[index],value),std::memory_order_relaxed);
}

void LadspaPlugin::getParamName(int index,char* buf)
{
	if(index<0 || index>=numParams)
	{
		buf[0]=0;
		return;
	}
	copy_str(buf,controls[index].name.c_str(),31);
}

void LadspaPlugin::getParamDisplay(int index,char* buf)
{
	if(index<0 || index>=numParams)
	{
		buf[0]=0;
		return;
	}
	Control const& c=controls[index];
	float const v=target[index].load(std::memory_order_relaxed);
	if(c.hints & ladspa::HINT_TOGGLED)
		snprintf(buf,32,"%s",v>0.0f?"on":"off");
	else if(c.hints & ladspa::HINT_INTEGER)
		snprintf(buf,32,"%d",(int)lroundf(v));
	else if(fabsf(v)>=10000.0f)
		snprintf(buf,32,"%.0f",v);
	else
		snprintf(buf,32,"%.4g",v);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// state: "ADXLADS1", count, control values (plugin units)
int LadspaPlugin::getChunk(void** data,bool)
{
	uint32_t const n=(uint32_t)controls.size();
	chunk.resize(8+4+4*n);
	memcpy(chunk.data(),"ADXLADS1",8);
	memcpy(chunk.data()+8,&n,4);
	for(uint32_t i=0;i<n;i++)
	{
		float const v=target[i].load();
		memcpy(chunk.data()+12+4*i,&v,4);
	}
	*data=chunk.data();
	return (int)chunk.size();
}

void LadspaPlugin::setChunk(const void* data,int size,bool)
{
	const char* p=(const char*)data;
	if(!p || size<12 || memcmp(p,"ADXLADS1",8)!=0)
		return;
	uint32_t n=0;
	memcpy(&n,p+8,4);
	if(size<(int)(12+4*n))
		return;
	for(uint32_t i=0;i<n && i<controls.size();i++)
	{
		float v;
		memcpy(&v,p+12+4*i,4);
		target[i].store(v);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// audio thread
void LadspaPlugin::process(float** inputs,float** outputs,int frames,const ADX_MIDI_EVENT*,int)
{
	bool done=false;
	if(frames>0 && !busy.test_and_set(std::memory_order_acquire))
	{
		if(handle && active && frames<=MAX_PROCESS_FRAMES)
		{
			// control values for this block
			for(size_t i=0;i<controls.size();i++)
				port_value[i]=target[i].load(std::memory_order_relaxed);

			for(size_t i=0;i<audio_in.size();i++)
				desc->connect_port(handle,audio_in[i],inputs[i]);
			for(size_t i=0;i<audio_out.size();i++)
				desc->connect_port(handle,audio_out[i],outputs[i]);

			desc->run(handle,(unsigned long)frames);
			done=true;
		}
		busy.clear(std::memory_order_release);
	}

	if(!done)
		for(int o=0;o<numOutputs;o++)
			memset(outputs[o],0,sizeof(float)*(frames>0?frames:0));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// library functions
static bool ends_with(const char* s,const char* suffix)
{
	size_t const n=strlen(s),m=strlen(suffix);
	return n>=m && strcmp(s+n-m,suffix)==0;
}

bool adx_plugin_is_ladspa(const char* path)
{
	return ends_with(path,".so");
}

void adx_ladspa_paths(std::vector<std::string>& dirs)
{
	std::vector<std::string> candidates;

	// LADSPA_PATH (colon separated) first, then the usual places
	if(const char* env=getenv("LADSPA_PATH"))
	{
		std::string s=env;
		size_t pos=0;
		while(pos<=s.size())
		{
			size_t const c=s.find(':',pos);
			std::string const d=s.substr(pos,c==std::string::npos?std::string::npos:c-pos);
			if(!d.empty())
				candidates.push_back(d);
			if(c==std::string::npos)
				break;
			pos=c+1;
		}
	}
	if(const char* home=getenv("HOME"))
		candidates.push_back(std::string(home)+"/.ladspa");
	candidates.push_back("/usr/local/lib/ladspa");
	candidates.push_back("/usr/lib/ladspa");
	candidates.push_back("/usr/lib64/ladspa");
	candidates.push_back("/usr/lib/x86_64-linux-gnu/ladspa");

	// existing directories, each only once (some of them are symlinks to each other)
	std::vector<std::string> seen;
	for(const std::string& d:candidates)
	{
		char real[PATH_MAX];
		struct stat st;
		if(stat(d.c_str(),&st)!=0 || !S_ISDIR(st.st_mode) || !realpath(d.c_str(),real))
			continue;
		bool dup=false;
		for(const std::string& s:seen)
			if(s==real)
				dup=true;
		if(dup)
			continue;
		seen.push_back(real);
		dirs.push_back(d);
	}
}

int ladspa_plugin_scan(const char* path,std::vector<AdxPluginDesc>& out)
{
	std::string error;
	ladspa::DescriptorFunction fn=ladspa_open(path,error);
	if(!fn)
	{
		fprintf(stderr,"aodix: ladspa: %s: %s\n",path,error.c_str());
		return 0;
	}

	int count=0;
	for(unsigned long i=0;;i++)
	{
		const ladspa::Descriptor* d=fn(i);
		if(!d)
			break;

		// plugins without audio outputs are of no use in aodix
		bool has_out=false;
		for(unsigned long p=0;p<d->PortCount;p++)
			if((d->PortDescriptors[p] & ladspa::PORT_AUDIO) && (d->PortDescriptors[p] & ladspa::PORT_OUTPUT))
				has_out=true;
		if(!has_out)
			continue;

		out.push_back({path,d->Name?d->Name:(d->Label?d->Label:"?"),(int32_t)d->UniqueID,false});
		count++;
	}
	return count;
}

AdxPlugin* ladspa_plugin_load(const char* path,int32_t wanted_id,AdxPluginHost* host,char* error,int error_size)
{
	std::string err;
	ladspa::DescriptorFunction fn=ladspa_open(path,err);
	if(!fn)
	{
		snprintf(error,error_size,"%s",err.c_str());
		return nullptr;
	}

	const ladspa::Descriptor* chosen=nullptr;
	const ladspa::Descriptor* first=nullptr;
	for(unsigned long i=0;;i++)
	{
		const ladspa::Descriptor* d=fn(i);
		if(!d)
			break;
		if(!first)
			first=d;
		if(wanted_id!=0 && (int32_t)d->UniqueID==wanted_id)
		{
			chosen=d;
			break;
		}
	}
	if(!chosen)
		chosen=first;

	if(!chosen || !chosen->instantiate || !chosen->connect_port || !chosen->run)
	{
		snprintf(error,error_size,"The LADSPA library contains no usable plugin.");
		return nullptr;
	}

	return new LadspaPlugin(chosen,host);
}
