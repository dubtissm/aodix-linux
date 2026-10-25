/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - plugin abstraction
//
// The original Aodix talked directly to VST 2 plugins (AEffect / dispatcher opcodes). The linux port hosts
// VST 3 plugins (and later possibly CLAP). This interface contains exactly the operations Aodix needs, so the
// sequencer, routing and DSP code stay format independent. A format implementation (plugin_vst3.cpp, ...)
// translates them to the real plugin API.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <string>
#include <vector>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// midi event as queued by the sequencer for one processing block
struct ADX_MIDI_EVENT
{
	int				deltaFrames;	// sample offset inside the block
	unsigned char	midiData[4];	// status, data1, data2, (unused)
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// transport / time information (same layout of information Aodix maintained for its VST 2 plugins)
enum
{
	ADX_TRANSPORT_CHANGED		= 1,
	ADX_TRANSPORT_PLAYING		= 1<<1,
	ADX_TRANSPORT_CYCLE_ACTIVE	= 1<<2,
	ADX_TRANSPORT_RECORDING		= 1<<3,
	ADX_AUTOMATION_WRITING		= 1<<6,
	ADX_AUTOMATION_READING		= 1<<7,
	ADX_NANOS_VALID				= 1<<8,
	ADX_PPQ_POS_VALID			= 1<<9,
	ADX_TEMPO_VALID				= 1<<10,
	ADX_BARS_VALID				= 1<<11,
	ADX_CYCLE_POS_VALID			= 1<<12,
	ADX_TIME_SIG_VALID			= 1<<13,
};

struct ADX_TIME_INFO
{
	double	samplePos;
	double	sampleRate;
	double	nanoSeconds;
	double	ppqPos;
	double	tempo;
	double	barStartPos;
	double	cycleStartPos;
	double	cycleEndPos;
	int		timeSigNumerator;
	int		timeSigDenominator;
	int		smpteOffset;
	int		smpteFrameRate;
	int		samplesToNextClock;
	int		flags;
};

class AdxPlugin;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// callbacks from plugins into the host (implemented by the Aodix core)
class AdxPluginHost
{
public:
	virtual ~AdxPluginHost(){}

	// a parameter was changed by the plugin itself (editor knob): used for automation recording
	virtual void					plugin_automate(AdxPlugin* plugin,int param,float value)=0;

	// midi events produced by the plugin during process() (audio thread)
	virtual void					plugin_midi_out(AdxPlugin* plugin,const ADX_MIDI_EVENT* events,int num_events)=0;

	// plugin changed its program, parameter names, etc.
	virtual void					plugin_update_display(AdxPlugin* plugin)=0;

	// plugin editor wants a new size
	virtual void					plugin_resize_editor(AdxPlugin* plugin,int width,int height)=0;

	// current transport state
	virtual const ADX_TIME_INFO*	plugin_time_info(void)=0;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class AdxPlugin
{
public:
	virtual ~AdxPlugin(){}

	// static properties (valid after loading)
	int		numInputs=0;		// audio input channels (all input busses flattened)
	int		numOutputs=0;		// audio output channels (all output busses flattened)
	int		numParams=0;
	int		numPrograms=0;
	int32_t	uniqueID=0;			// stable 32-bit id (stored in projects to identify the plugin)
	int		version=0;
	bool	hasEditor=false;
	bool	programChunks=true;	// state is saved as opaque chunk (always true for vst3)
	bool	isSynth=false;

	virtual const char*	format(void)=0;		// "VST3", ...

	// names (buffers: effect name 64+1, program names 24+1, parameter strings 31+1 chars)
	virtual void		getEffectName(char* buf)=0;

	// processing setup
	virtual void		setSampleRate(double rate)=0;
	virtual void		setBlockSize(int frames)=0;
	virtual void		setActive(bool active)=0;

	// parameters (normalized 0..1)
	virtual float		getParameter(int index)=0;
	virtual void		setParameter(int index,float value)=0;
	virtual void		getParamName(int index,char* buf)=0;
	virtual void		getParamDisplay(int index,char* buf)=0;

	// programs
	virtual int			getProgram(void)=0;
	virtual void		setProgram(int index)=0;
	virtual void		getProgramName(char* buf)=0;
	virtual void		setProgramName(const char* name)=0;
	virtual bool		getProgramNameIndexed(int index,char* buf)=0;

	// state (the returned pointer stays valid until the next getChunk call or destruction)
	virtual int			getChunk(void** data,bool preset)=0;
	virtual void		setChunk(const void* data,int size,bool preset)=0;

	// audio processing (audio thread)
	virtual void		process(float** inputs,float** outputs,int frames,const ADX_MIDI_EVENT* events,int num_events)=0;

	// editor (gui thread); parent is an X11 window id
	virtual bool		editorGetSize(int* width,int* height)=0;
	virtual bool		editorOpen(unsigned long parent)=0;
	virtual void		editorClose(void)=0;
	virtual void		editorResized(int width,int height){(void)width;(void)height;}	// host window was resized
	virtual bool		editorCanResize(void){return false;}

	// periodic call from the gui thread
	virtual void		idle(void)=0;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// plugin library

// description of one loadable plugin (a bundle may contain several)
struct AdxPluginDesc
{
	std::string	path;		// bundle / file path
	std::string	name;		// display name
	int32_t		uniqueID;
	bool		isSynth;
};

// true if a directory entry in a vst folder is a plugin we can load (a ".vst3" bundle)
bool		adx_plugin_is_candidate(const char* name,bool is_dir);

// true if a path is a CLAP plugin (".clap")
bool		adx_plugin_is_clap(const char* path);

// true if a path is a LADSPA library (".so"); LADSPA libraries are only searched in the LADSPA folders
bool		adx_plugin_is_ladspa(const char* path);

// the existing LADSPA folders ($LADSPA_PATH, ~/.ladspa, /usr/local/lib/ladspa, /usr/lib/ladspa, ...)
void		adx_ladspa_paths(std::vector<std::string>& dirs);

// true if a path is an LV2 bundle (".lv2"); LV2 plugins are found through lilv in the standard LV2 places
bool		adx_plugin_is_lv2(const char* path);

// all installed LV2 plugins (path = bundle directory); empty if lilv is not installed
void		adx_lv2_list(std::vector<AdxPluginDesc>& out);

// list the plugins contained in a bundle / file
int			adx_plugin_scan(const char* path,std::vector<AdxPluginDesc>& out);

// load a plugin. wanted_id selects the class inside a bundle (0 = first instrument / first class)
AdxPlugin*	adx_plugin_load(const char* path,int32_t wanted_id,AdxPluginHost* host,char* error,int error_size);

// format implementations (dispatched by adx_plugin_scan / adx_plugin_load)
int			vst3_plugin_scan(const char* path,std::vector<AdxPluginDesc>& out);
AdxPlugin*	vst3_plugin_load(const char* path,int32_t wanted_id,AdxPluginHost* host,char* error,int error_size);
int			ladspa_plugin_scan(const char* path,std::vector<AdxPluginDesc>& out);
int			clap_plugin_scan(const char* path,std::vector<AdxPluginDesc>& out);
AdxPlugin*	clap_plugin_load(const char* path,int32_t wanted_id,AdxPluginHost* host,char* error,int error_size);
int			lv2_plugin_scan(const char* path,std::vector<AdxPluginDesc>& out);
AdxPlugin*	lv2_plugin_load(const char* path,int32_t wanted_id,AdxPluginHost* host,char* error,int error_size);
AdxPlugin*	ladspa_plugin_load(const char* path,int32_t wanted_id,AdxPluginHost* host,char* error,int error_size);
