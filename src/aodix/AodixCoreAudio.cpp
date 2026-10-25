/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Core Audio Implementation (linux port, JACK)
//
// Replaces the original ASIO module (AodixCoreAsio.cpp). The audio callback structure is the same as in
// the original: the driver callback fills dsp_input_buffer, calls dsp_work() inside the critical section
// and copies dsp_output_buffer to the hardware channels selected by the master input/output pins.
//
// JACK specifics:
// - libjack is loaded at runtime (dlopen), so aodix builds and starts without jack development packages
//   and works with jack2 as well as with pipewire-jack.
// - the jack client stays open while aodix runs. audio_close()/audio_init() (used by the configuration
//   and bounce dialogs) suspend and resume processing instead of closing the client, so the user's port
//   connections survive.
// - midi input arrives on a jack midi port and is handed to midi_in_process() by a worker thread, just
//   like the windows multimedia midi callback thread did in the original.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AodixCore.h"

#include <dlfcn.h>
#include <semaphore.h>
#include <atomic>
#include <string>
#include <vector>
#include <thread>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define AUDIO_MAX_I_CHANNELS	32
#define AUDIO_MAX_O_CHANNELS	32

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// minimal jack api declarations (stable public C abi of libjack)
typedef uint32_t	jack_nframes_t;
typedef struct _jack_client	jack_client_t;
typedef struct _jack_port	jack_port_t;
typedef unsigned char		jack_midi_data_t;

struct jack_midi_event_t
{
	jack_nframes_t		time;
	size_t				size;
	jack_midi_data_t*	buffer;
};

enum
{
	JackNullOption		= 0x00,
	JackNoStartServer	= 0x01,
};

enum
{
	JackPortIsInput		= 0x01,
	JackPortIsOutput	= 0x02,
	JackPortIsPhysical	= 0x04,
};

#define JACK_DEFAULT_AUDIO_TYPE	"32 bit float mono audio"
#define JACK_DEFAULT_MIDI_TYPE	"8 bit raw midi"

typedef int		(*JackProcessCallback)(jack_nframes_t nframes,void* arg);
typedef int		(*JackBufferSizeCallback)(jack_nframes_t nframes,void* arg);
typedef int		(*JackSampleRateCallback)(jack_nframes_t nframes,void* arg);
typedef void	(*JackShutdownCallback)(void* arg);

struct JACK_API
{
	void*			lib;
	jack_client_t*	(*client_open)(const char* name,int options,int* status,...);
	int				(*client_close)(jack_client_t*);
	char*			(*get_client_name)(jack_client_t*);
	int				(*activate)(jack_client_t*);
	int				(*deactivate)(jack_client_t*);
	jack_nframes_t	(*get_sample_rate)(jack_client_t*);
	jack_nframes_t	(*get_buffer_size)(jack_client_t*);
	int				(*set_process_callback)(jack_client_t*,JackProcessCallback,void*);
	int				(*set_buffer_size_callback)(jack_client_t*,JackBufferSizeCallback,void*);
	int				(*set_sample_rate_callback)(jack_client_t*,JackSampleRateCallback,void*);
	void			(*on_shutdown)(jack_client_t*,JackShutdownCallback,void*);
	jack_port_t*	(*port_register)(jack_client_t*,const char* name,const char* type,unsigned long flags,unsigned long buffer_size);
	int				(*port_unregister)(jack_client_t*,jack_port_t*);
	void*			(*port_get_buffer)(jack_port_t*,jack_nframes_t);
	const char*		(*port_name)(const jack_port_t*);
	const char*		(*port_short_name)(const jack_port_t*);
	const char**	(*port_get_connections)(const jack_port_t*);
	int				(*port_disconnect)(jack_client_t*,jack_port_t*);
	const char**	(*get_ports)(jack_client_t*,const char* name_pattern,const char* type_pattern,unsigned long flags);
	int				(*connect)(jack_client_t*,const char* src,const char* dst);
	void			(*free)(void*);
	uint32_t		(*midi_get_event_count)(void* buffer);
	int				(*midi_event_get)(jack_midi_event_t* event,void* buffer,uint32_t index);
};

static JACK_API	jack;

static bool jack_load(char* error,int error_size)
{
	if(jack.lib)
		return true;

	const char* const names[]={"libjack.so.0","libjack.so"};
	for(const char* n:names)
	{
		jack.lib=dlopen(n,RTLD_NOW|RTLD_LOCAL);
		if(jack.lib)
			break;
	}

	if(!jack.lib)
	{
		snprintf(error,error_size,"Error: JACK Library Not Found (libjack.so.0)\n\nPlease install JACK (jack2) or PipeWire-JACK.");
		return false;
	}

	bool ok=true;
	auto sym=[&](const char* name)->void*
	{
		void* p=dlsym(jack.lib,name);
		if(!p)
		{
			fprintf(stderr,"aodix: libjack is missing '%s'\n",name);
			ok=false;
		}
		return p;
	};

	*(void**)&jack.client_open=sym("jack_client_open");
	*(void**)&jack.client_close=sym("jack_client_close");
	*(void**)&jack.get_client_name=sym("jack_get_client_name");
	*(void**)&jack.activate=sym("jack_activate");
	*(void**)&jack.deactivate=sym("jack_deactivate");
	*(void**)&jack.get_sample_rate=sym("jack_get_sample_rate");
	*(void**)&jack.get_buffer_size=sym("jack_get_buffer_size");
	*(void**)&jack.set_process_callback=sym("jack_set_process_callback");
	*(void**)&jack.set_buffer_size_callback=sym("jack_set_buffer_size_callback");
	*(void**)&jack.set_sample_rate_callback=sym("jack_set_sample_rate_callback");
	*(void**)&jack.on_shutdown=sym("jack_on_shutdown");
	*(void**)&jack.port_register=sym("jack_port_register");
	*(void**)&jack.port_unregister=sym("jack_port_unregister");
	*(void**)&jack.port_get_buffer=sym("jack_port_get_buffer");
	*(void**)&jack.port_name=sym("jack_port_name");
	*(void**)&jack.port_short_name=sym("jack_port_short_name");
	*(void**)&jack.port_get_connections=sym("jack_port_get_connections");
	*(void**)&jack.port_disconnect=sym("jack_port_disconnect");
	*(void**)&jack.get_ports=sym("jack_get_ports");
	*(void**)&jack.connect=sym("jack_connect");
	*(void**)&jack.free=sym("jack_free");
	*(void**)&jack.midi_get_event_count=sym("jack_midi_get_event_count");
	*(void**)&jack.midi_event_get=sym("jack_midi_event_get");

	if(!ok)
	{
		dlclose(jack.lib);
		jack.lib=NULL;
		snprintf(error,error_size,"Error: The Installed JACK Library Is Incomplete");
		return false;
	}

	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// single producer / single consumer queue for incoming midi short messages (audio thread -> midi thread)
#define MIDI_QUEUE_SIZE	4096

struct MIDI_QUEUE
{
	unsigned char		data[MIDI_QUEUE_SIZE][3];
	std::atomic<int>	write_pos;
	std::atomic<int>	read_pos;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct AUDIO_DRIVER_INFO
{
	// jack client (open for the whole session)
	jack_client_t* client;

	// processing enabled (false while suspended by audio_close)
	std::atomic<bool> running;

	// server has shut down (set by the jack thread, handled in the gui thread)
	std::atomic<bool> server_gone;

	// registered ports
	std::vector<jack_port_t*> input_ports;
	std::vector<jack_port_t*> output_ports;
	jack_port_t* midi_port;

	// channel names (shown in the master input/output pin menus)
	std::vector<std::string> input_names;
	std::vector<std::string> output_names;

	// midi input
	MIDI_QUEUE midi_queue;
	sem_t midi_sem;
	std::thread midi_thread;
	std::atomic<bool> midi_thread_quit;
	std::atomic<bool> midi_in_enabled;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static AUDIO_DRIVER_INFO	audio_driver_info;

extern CAodixCore*			gl_padx;
void midi_in_receive(unsigned char mi_data0,unsigned char mi_data1,unsigned char mi_data2);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// the critical section guarding the dsp state (recursive, shared by gui thread and audio thread)
static CRITICAL_SECTION		audio_critical_section;
static pthread_once_t		audio_cs_once=PTHREAD_ONCE_INIT;

static void audio_cs_create(void)
{
	InitializeCriticalSection(&audio_critical_section);
	sem_init(&audio_driver_info.midi_sem,0,0);
	audio_driver_info.midi_queue.write_pos=0;
	audio_driver_info.midi_queue.read_pos=0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
double audio_cb_double_clip(double d_input,double const range)
{
	// clip max
	if(d_input>range)
		d_input=range;

	// clip min
	if(d_input<-range)
		d_input=-range;

	// return clipped sample
	return d_input;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// midi input: audio thread side (no locks, no allocation)
static void audio_cb_read_midi(jack_nframes_t nframes)
{
	AUDIO_DRIVER_INFO* const pdi=&audio_driver_info;

	if(pdi->midi_port==NULL)
		return;

	void* const pbuf=jack.port_get_buffer(pdi->midi_port,nframes);
	uint32_t const num_events=jack.midi_get_event_count(pbuf);
	bool any=false;

	for(uint32_t e=0;e<num_events;e++)
	{
		jack_midi_event_t ev;
		if(jack.midi_event_get(&ev,pbuf,e)!=0 || ev.size<1 || ev.size>3)
			continue;

		// short messages only (sysex and system real time messages are ignored like in the original)
		if(ev.buffer[0]<0x80 || ev.buffer[0]>=0xF0)
			continue;

		if(!pdi->midi_in_enabled)
			continue;

		int const w=pdi->midi_queue.write_pos.load(std::memory_order_relaxed);
		int const next=(w+1)%MIDI_QUEUE_SIZE;
		if(next==pdi->midi_queue.read_pos.load(std::memory_order_acquire))
			break;

		pdi->midi_queue.data[w][0]=ev.buffer[0];
		pdi->midi_queue.data[w][1]=(ev.size>1)?ev.buffer[1]:0;
		pdi->midi_queue.data[w][2]=(ev.size>2)?ev.buffer[2]:0;
		pdi->midi_queue.write_pos.store(next,std::memory_order_release);
		any=true;
	}

	if(any)
		sem_post(&pdi->midi_sem);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// midi input: worker thread (plays the role of the windows midi callback thread)
static void audio_midi_thread(void)
{
	AUDIO_DRIVER_INFO* const pdi=&audio_driver_info;

	while(!pdi->midi_thread_quit)
	{
		sem_wait(&pdi->midi_sem);

		for(;;)
		{
			int const r=pdi->midi_queue.read_pos.load(std::memory_order_relaxed);
			if(r==pdi->midi_queue.write_pos.load(std::memory_order_acquire))
				break;

			unsigned char const d0=pdi->midi_queue.data[r][0];
			unsigned char const d1=pdi->midi_queue.data[r][1];
			unsigned char const d2=pdi->midi_queue.data[r][2];
			pdi->midi_queue.read_pos.store((r+1)%MIDI_QUEUE_SIZE,std::memory_order_release);

			if(!pdi->midi_thread_quit)
				midi_in_receive(d0,d1,d2);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// jack process callback (was asio_cb_buffer_switch_time_info)
static int audio_cb_process(jack_nframes_t nframes,void* arg)
{
	AUDIO_DRIVER_INFO* const pdi=&audio_driver_info;

	// get current timestamp cycle
	double const init_cycle=arg_sys_rdtsc();

	// get port buffers
	float* in_buf[AUDIO_MAX_I_CHANNELS];
	float* out_buf[AUDIO_MAX_O_CHANNELS];
	int const num_in=(int)pdi->input_ports.size();
	int const num_out=(int)pdi->output_ports.size();

	for(int i=0;i<num_in;i++)
		in_buf[i]=(float*)jack.port_get_buffer(pdi->input_ports[i],nframes);

	for(int o=0;o<num_out;o++)
	{
		out_buf[o]=(float*)jack.port_get_buffer(pdi->output_ports[o],nframes);
		memset(out_buf[o],0,sizeof(float)*nframes);
	}

	// midi input
	audio_cb_read_midi(nframes);

	// suspended (configuration / bounce dialog): silence
	if(!pdi->running)
		return 0;

	// enter critical section
	gl_padx->audio_enter_cs();

	// suspended while waiting for the lock
	if(!pdi->running)
	{
		gl_padx->audio_leave_cs();
		return 0;
	}

	// jack may use buffers larger than the aodix maximum block size: process in sub blocks
	jack_nframes_t done=0;

	while(done<nframes)
	{
		// get dsp num samples
		int const dsp_num_samples=(int)min<jack_nframes_t,jack_nframes_t>(nframes-done,MAX_DSP_BLOCK_SIZE);

		// aodix and its plugins work with a fixed block size
		if(gl_padx->dsp_block_size!=dsp_num_samples)
		{
			gl_padx->dsp_block_size=dsp_num_samples;
			for(int i=0;i<MAX_INSTANCES;i++)
				if(gl_padx->instance[i].peffect!=NULL)
					gl_padx->instance[i].peffect->setBlockSize(dsp_num_samples);
		}

		// clear dsp input buffers
		gl_padx->dsp_clear_input_buffers();

		// audio input processing: mix jack inputs into the assigned master input pins
		for(int pi=0;pi<NUM_DSP_INPUTS;pi++)
		{
			int const port=gl_padx->cfg.audio_input_pin[pi];
			if(port>=0 && port<num_in)
				arg_dsp_bmix(in_buf[port]+done,gl_padx->dsp_input_buffer[pi],dsp_num_samples);
		}

		// main host dsp process
		gl_padx->dsp_work();

		// audio output processing: mix master output pins into the assigned jack outputs
		for(int po=0;po<NUM_DSP_OUTPUTS;po++)
		{
			int const port=gl_padx->cfg.audio_output_pin[po];
			if(port>=0 && port<num_out)
			{
				float* const pdst=out_buf[port]+done;
				float const* const psrc=gl_padx->dsp_output_buffer[po];
				for(int s=0;s<dsp_num_samples;s++)
					pdst[s]=float(audio_cb_double_clip(double(pdst[s])+double(psrc[s]),1));
			}
		}

		done+=dsp_num_samples;
	}

	// leave critical section
	gl_padx->audio_leave_cs();

	// performance count
	double const elapsed_cycles=arg_sys_rdtsc()-init_cycle;

	// system cycles per dsp block
	double const d_system_cycles_per_dsp_block_frame=(gl_padx->d_system_clocks_per_sec*double(nframes))/gl_padx->cfg.audio_driver_sample_rate;

	// calculate cpu performance percent
	double const d_per_cent=(elapsed_cycles*100.0)/d_system_cycles_per_dsp_block_frame;

	// lowpass cpu meter
	gl_padx->dsp_cpu_cost+=(float)((d_per_cent-gl_padx->dsp_cpu_cost)*0.01);

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static int audio_cb_buffer_size(jack_nframes_t nframes,void* arg)
{
	// the process callback adapts the dsp block size by itself, just report it
	fprintf(stderr,"aodix: jack buffer size is now %u\n",nframes);
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static int audio_cb_sample_rate(jack_nframes_t nframes,void* arg)
{
	gl_padx->audio_enter_cs();

	// set new sample rate
	gl_padx->cfg.audio_driver_sample_rate=double(nframes);

	// notify instances
	for(int i=0;i<MAX_INSTANCES;i++)
	{
		ADX_INSTANCE* pi=&gl_padx->instance[i];
		if(pi->peffect!=NULL)
		{
			pi->peffect->setActive(false);
			pi->peffect->setSampleRate(double(nframes));
			pi->peffect->setActive(true);
		}
	}

	gl_padx->audio_leave_cs();
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void audio_cb_shutdown(void* arg)
{
	// called from a jack thread: only flag it, the gui thread cleans up (see audio_idle)
	audio_driver_info.running=false;
	audio_driver_info.server_gone=true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static int audio_wanted_ports(ADX_CONFIG const* pcfg)
{
	int const n=pcfg->audio_num_ports;
	if(n==4 || n==8 || n==16 || n==32)
		return n;
	return 2;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// register jack ports for the configured channel count (only before the client is activated)
static void audio_update_ports(int const wanted)
{
	AUDIO_DRIVER_INFO* const pdi=&audio_driver_info;

	while((int)pdi->input_ports.size()>wanted)
	{
		jack.port_unregister(pdi->client,pdi->input_ports.back());
		pdi->input_ports.pop_back();
	}

	while((int)pdi->output_ports.size()>wanted)
	{
		jack.port_unregister(pdi->client,pdi->output_ports.back());
		pdi->output_ports.pop_back();
	}

	while((int)pdi->input_ports.size()<wanted)
	{
		char name[32];
		snprintf(name,sizeof(name),"in_%d",(int)pdi->input_ports.size()+1);
		jack_port_t* p=jack.port_register(pdi->client,name,JACK_DEFAULT_AUDIO_TYPE,JackPortIsInput,0);
		if(p==NULL)
			break;
		pdi->input_ports.push_back(p);
	}

	while((int)pdi->output_ports.size()<wanted)
	{
		char name[32];
		snprintf(name,sizeof(name),"out_%d",(int)pdi->output_ports.size()+1);
		jack_port_t* p=jack.port_register(pdi->client,name,JACK_DEFAULT_AUDIO_TYPE,JackPortIsOutput,0);
		if(p==NULL)
			break;
		pdi->output_ports.push_back(p);
	}

	if(pdi->midi_port==NULL)
		pdi->midi_port=jack.port_register(pdi->client,"midi_in",JACK_DEFAULT_MIDI_TYPE,JackPortIsInput,0);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// connect our ports to the physical ports (only ports without connections, so user connections are kept)
static void audio_auto_connect(void)
{
	AUDIO_DRIVER_INFO* const pdi=&audio_driver_info;

	const char** playback=jack.get_ports(pdi->client,NULL,JACK_DEFAULT_AUDIO_TYPE,JackPortIsPhysical | JackPortIsInput);
	if(playback)
	{
		for(size_t o=0;o<pdi->output_ports.size() && playback[o];o++)
		{
			const char** con=jack.port_get_connections(pdi->output_ports[o]);
			if(con)
				jack.free(con);
			else
				jack.connect(pdi->client,jack.port_name(pdi->output_ports[o]),playback[o]);
		}
		jack.free(playback);
	}

	const char** capture=jack.get_ports(pdi->client,NULL,JACK_DEFAULT_AUDIO_TYPE,JackPortIsPhysical | JackPortIsOutput);
	if(capture)
	{
		for(size_t i=0;i<pdi->input_ports.size() && capture[i];i++)
		{
			const char** con=jack.port_get_connections(pdi->input_ports[i]);
			if(con)
				jack.free(con);
			else
				jack.connect(pdi->client,capture[i],jack.port_name(pdi->input_ports[i]));
		}
		jack.free(capture);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void audio_update_names(void)
{
	AUDIO_DRIVER_INFO* const pdi=&audio_driver_info;

	pdi->input_names.clear();
	pdi->output_names.clear();

	for(int pass=0;pass<2;pass++)
	{
		std::vector<jack_port_t*> const& ports=pass?pdi->output_ports:pdi->input_ports;
		std::vector<std::string>& names=pass?pdi->output_names:pdi->input_names;

		for(jack_port_t* p:ports)
		{
			// port name plus its first connection, e.g. "out_1 > system:playback_1"
			std::string n=jack.port_short_name(p);
			const char** con=jack.port_get_connections(p);
			if(con)
			{
				n+=pass?" > ":" < ";
				n+=con[0];
				if(con[1])
					n+=" ...";
				jack.free(con);
			}
			names.push_back(n);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::audio_enter_cs(void)
{
	pthread_once(&audio_cs_once,audio_cs_create);
	EnterCriticalSection(&audio_critical_section);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::audio_leave_cs(void)
{
	LeaveCriticalSection(&audio_critical_section);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::audio_init(HWND const hwnd)
{
	// create critical section object
	pthread_once(&audio_cs_once,audio_cs_create);

	AUDIO_DRIVER_INFO* const pdi=&audio_driver_info;

	// return inmediantly if no driver is selected
	if(cfg.audio_driver_id<0)
	{
		audio_num_inputs=0;
		audio_num_outputs=0;
		return;
	}

	char error[512];

	// load libjack
	if(!jack_load(error,sizeof(error)))
	{
		MessageBox(hwnd,error,"Aodix - JACK Initialization",MB_OK | MB_ICONERROR);
		return;
	}

	// open client (once per session)
	if(pdi->client==NULL)
	{
		int status=0;
		pdi->client=jack.client_open("Aodix",JackNoStartServer,&status);

		if(pdi->client==NULL)
		{
			MessageBox(hwnd,"Error: Could Not Connect To The JACK Server\n\nPlease start JACK (for example with QjackCtl) or use PipeWire-JACK,\nthen select the driver again in File > Configuration.","Aodix - JACK Initialization",MB_OK | MB_ICONERROR);
			audio_num_inputs=0;
			audio_num_outputs=0;
			return;
		}

		pdi->server_gone=false;
		pdi->running=false;
		pdi->midi_port=NULL;

		jack.set_process_callback(pdi->client,audio_cb_process,this);
		jack.set_buffer_size_callback(pdi->client,audio_cb_buffer_size,this);
		jack.set_sample_rate_callback(pdi->client,audio_cb_sample_rate,this);
		jack.on_shutdown(pdi->client,audio_cb_shutdown,this);

		// register ports before activation
		audio_update_ports(audio_wanted_ports(&cfg));

		// midi worker thread
		pdi->midi_thread_quit=false;
		pdi->midi_thread=std::thread(audio_midi_thread);

		if(jack.activate(pdi->client)!=0)
		{
			MessageBox(hwnd,"Error: jack_activate() Failed","Aodix - JACK Initialization",MB_OK | MB_ICONERROR);
			audio_shutdown();
			return;
		}

		audio_auto_connect();
	}
	// note: the number of ports is only changed when the client is opened (ports are never unregistered
	// while the process callback runs); a changed channel count takes effect after restarting aodix

	// enter critical section
	audio_enter_cs();

	// sample rate and block size are given by the jack server
	cfg.audio_driver_sample_rate=double(jack.get_sample_rate(pdi->client));
	dsp_block_size=min<int32_t,int32_t>((int32_t)jack.get_buffer_size(pdi->client),MAX_DSP_BLOCK_SIZE);

	// update channel counts
	audio_num_inputs=(int32_t)pdi->input_ports.size();
	audio_num_outputs=(int32_t)pdi->output_ports.size();

	// scan vst instances
	for(int32_t i=0;i<MAX_INSTANCES;i++)
	{
		ADX_INSTANCE* pi=&instance[i];

		// update sample rate and block size
		if(pi->peffect!=NULL)
		{
			pi->peffect->setActive(false);
			pi->peffect->setSampleRate(cfg.audio_driver_sample_rate);
			pi->peffect->setBlockSize(dsp_block_size);
			pi->peffect->setActive(true);
		}
	}

	// start processing
	pdi->running=true;

	// leave critical section
	audio_leave_cs();

	// channel names for the pin menus
	audio_update_names();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::audio_close(HWND const hwnd)
{
	// suspend processing (the jack client stays open so connections are kept)
	audio_enter_cs();
	audio_driver_info.running=false;
	audio_leave_cs();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::audio_shutdown(void)
{
	AUDIO_DRIVER_INFO* const pdi=&audio_driver_info;

	pdi->running=false;

	// stop midi worker
	if(pdi->midi_thread.joinable())
	{
		pdi->midi_thread_quit=true;
		sem_post(&pdi->midi_sem);
		pdi->midi_thread.join();
	}

	// close client
	if(pdi->client!=NULL)
	{
		if(!pdi->server_gone)
			jack.deactivate(pdi->client);
		jack.client_close(pdi->client);
		pdi->client=NULL;
	}

	pdi->input_ports.clear();
	pdi->output_ports.clear();
	pdi->midi_port=NULL;
	pdi->input_names.clear();
	pdi->output_names.clear();
	pdi->server_gone=false;

	audio_num_inputs=0;
	audio_num_outputs=0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::audio_idle(HWND const hwnd)
{
	// the jack server went away: clean up in the gui thread and tell the user
	if(audio_driver_info.server_gone)
	{
		audio_shutdown();
		p_hmidi_in=NULL;
		gui_is_dirty=1;
		MessageBox(hwnd,"The JACK Server Has Shut Down\n\nRestart JACK and select the driver again in File > Configuration.","Aodix - JACK",MB_OK | MB_ICONWARNING);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CAodixCore::audio_is_inited(void)
{
	return audio_driver_info.client!=NULL && audio_driver_info.running;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::audio_get_driver_name(int const id,char* buf,int const num_chars)
{
	// reset buffer if fails
	buf[0]=0;

	// retrieve driver name
	if(id==0)
	{
		if(audio_driver_info.client!=NULL)
			snprintf(buf,num_chars,"JACK (%u Hz, %u Samples)",jack.get_sample_rate(audio_driver_info.client),jack.get_buffer_size(audio_driver_info.client));
		else
			snprintf(buf,num_chars,"JACK Audio Connection Kit");
	}
	else
	{
		snprintf(buf,num_chars,"Click To Select Driver");
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int32_t CAodixCore::audio_init_list(void)
{
	// one driver: jack
	return 1;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::audio_fill_pin_menu(HMENU const hmenu,int const fill_inputs,int const init_id)
{
	// refresh port names (connections may have changed)
	if(audio_driver_info.client!=NULL)
		audio_update_names();

	// stack vars
	char buf[32];
	int opt_id=init_id;

	// head menu label
	if(fill_inputs)
		sprintf(buf,"Master Input Pin %d",user_input_pin);
	else
		sprintf(buf,"Master Output Pin %d",user_output_pin);

	// add head option
	arg_menu_add_item(hmenu,buf,opt_id++);

	// set default state
	MENUITEMINFO mii;
	memset((void*)(&mii),0,sizeof(mii));
	mii.cbSize=sizeof(MENUITEMINFO);
	mii.fMask=MIIM_STATE;
	mii.fState=MFS_DEFAULT | MFS_HILITE;

	// change menu item info
	SetMenuItemInfo(hmenu,0,TRUE,&mii);

	// add separator
	arg_menu_add_item(hmenu,NULL,opt_id++);

	// get channel names
	std::vector<std::string> const& names=fill_inputs?audio_driver_info.input_names:audio_driver_info.output_names;

	// get pin count
	int const pin_count=(int)names.size();

	// fill menu with pin channels
	for(int p=0;p<pin_count;p++)
	{
		char name[256];
		snprintf(name,sizeof(name),"%s",names[p].c_str());
		arg_menu_add_item(hmenu,name,opt_id++);
	}

	// add separator
	arg_menu_add_item(hmenu,NULL,opt_id++);

	// get hardware assignment from selected pin
	int pin_assignment=0;

	// no assigned pin
	if(fill_inputs)
	{
		// add no assigned input option
		arg_menu_add_item(hmenu,(char*)"No Input",opt_id++);

		// get pin input selected assignment
		pin_assignment=cfg.audio_input_pin[user_input_pin];
	}
	else
	{
		// add no assigned output option
		arg_menu_add_item(hmenu,(char*)"No Output",opt_id++);

		// get pin output selected assignment
		pin_assignment=cfg.audio_output_pin[user_output_pin];
	}

	// check assigned pin
	if(pin_assignment<pin_count)
		CheckMenuRadioItem(hmenu,2,3+pin_count,2+pin_assignment,MF_BYPOSITION);
	else
		CheckMenuRadioItem(hmenu,2,3+pin_count,3+pin_count,MF_BYPOSITION);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::audio_control_panel(void)
{
	// linux port: the jack server is configured with an external tool
	extern HWND gl_hwnd_main;

	if(system("command -v qjackctl >/dev/null 2>&1")==0)
	{
		if(system("qjackctl >/dev/null 2>&1 &")!=0)
			fprintf(stderr,"aodix: could not start qjackctl\n");
	}
	else
	{
		MessageBox(gl_hwnd_main,"The JACK server is configured with an external tool\n(for example QjackCtl, Cadence or your distribution's audio settings).","Aodix - Audio",MB_OK | MB_ICONINFORMATION);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// midi input devices: the physical (and bridged) jack midi sources
static std::vector<std::string> audio_midi_sources(void)
{
	std::vector<std::string> r;
	if(audio_driver_info.client==NULL)
		return r;

	const char** ports=jack.get_ports(audio_driver_info.client,NULL,JACK_DEFAULT_MIDI_TYPE,JackPortIsOutput | JackPortIsPhysical);
	if(ports)
	{
		for(int i=0;ports[i];i++)
			r.push_back(ports[i]);
		jack.free(ports);
	}
	return r;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int CAodixCore::midi_in_get_num_devices(void)
{
	return (int)audio_midi_sources().size();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::midi_in_get_device_name(int const index,char* buf,int const num_chars)
{
	std::vector<std::string> const s=audio_midi_sources();
	if(index>=0 && index<(int)s.size())
		snprintf(buf,num_chars,"%s",s[index].c_str());
	else
		snprintf(buf,num_chars,"---");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::midi_in_init(void)
{
	// set midi handler(s) pointer to null
	p_hmidi_in=NULL;

	AUDIO_DRIVER_INFO* const pdi=&audio_driver_info;

	// midi needs the jack client
	if(pdi->client==NULL || pdi->midi_port==NULL)
		return;

	// accept incoming midi
	pdi->midi_in_enabled=true;
	p_hmidi_in=(void*)pdi->midi_port;

	// open all midi input devices: connect all hardware midi sources to our midi input
	if(cfg.midi_in_dv_open)
	{
		std::vector<std::string> const sources=audio_midi_sources();
		for(std::string const& src:sources)
			jack.connect(pdi->client,src.c_str(),jack.port_name(pdi->midi_port));
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::midi_in_close(void)
{
	AUDIO_DRIVER_INFO* const pdi=&audio_driver_info;

	// stop accepting midi; connections made by the user in a patchbay are kept unless aodix opened them
	pdi->midi_in_enabled=false;

	if(pdi->client!=NULL && pdi->midi_port!=NULL && cfg.midi_in_dv_open)
		jack.port_disconnect(pdi->client,pdi->midi_port);

	// set null midi handler(s) pointer
	p_hmidi_in=NULL;
}
