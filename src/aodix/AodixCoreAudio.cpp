/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Core Audio Implementation (linux port)
//
// Replaces the original ASIO module (AodixCoreAsio.cpp). The audio callback structure is kept: the driver
// fills dsp_input_buffer, calls dsp_work() inside the critical section and copies dsp_output_buffer to the
// hardware. On linux the driver is JACK.
//
// Port status: stage 1 (this file) provides the driver interface without an active audio connection.
// The JACK client is added in stage 2.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AodixCore.h"

#include <string>
#include <vector>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define AUDIO_MAX_I_CHANNELS	32
#define AUDIO_MAX_O_CHANNELS	32

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct AUDIO_DRIVER_INFO
{
	// driver running
	bool running;

	// channel names (shown in the master input/output pin menus)
	std::vector<std::string> input_names;
	std::vector<std::string> output_names;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static AUDIO_DRIVER_INFO	audio_driver_info;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// the critical section guarding the dsp state (recursive, shared by gui thread and audio thread)
static CRITICAL_SECTION		audio_critical_section;
static pthread_once_t		audio_cs_once=PTHREAD_ONCE_INIT;

static void audio_cs_create(void)
{
	InitializeCriticalSection(&audio_critical_section);
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

	// set default blocksize
	dsp_block_size=MAX_DSP_BLOCK_SIZE;

	// no channels until a driver is running
	audio_num_inputs=0;
	audio_num_outputs=0;
	audio_driver_info.running=false;
	audio_driver_info.input_names.clear();
	audio_driver_info.output_names.clear();

	// return inmediantly if no driver is selected
	if(cfg.audio_driver_id<0)
		return;

	// stage 1 of the port: no audio driver yet (jack follows in stage 2)
	fprintf(stderr,"aodix: audio output (JACK) is not available in this build yet\n");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::audio_close(HWND const hwnd)
{
	audio_driver_info.running=false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CAodixCore::audio_is_inited(void)
{
	return audio_driver_info.running;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::audio_get_driver_name(int const id,char* buf,int const num_chars)
{
	// reset buffer if fails
	buf[0]=0;

	// retrieve driver name
	if(id==0)
		snprintf(buf,num_chars,"JACK Audio Connection Kit");
	else
		snprintf(buf,num_chars,"Click To Select Driver");
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
		char name[64];
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
