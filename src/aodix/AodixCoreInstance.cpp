/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Core Instance Implementation
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AodixCore.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::instance_dll(HWND const hwnd,ADX_INSTANCE* pi,char* filename,int const param_x,int const param_y)
{
	// linux port: load the plugin through the format independent plugin loader (vst3 bundle).
	// when loading a project, the stored unique id selects the plugin class inside the bundle.
	char error[512];
	error[0]=0;

	pi->peffect=adx_plugin_load(filename,instance_eff_id_currently_loading,this,error,sizeof(error));

	// no effect created
	if(pi->peffect==NULL)
	{
		char buf[1024];
		snprintf(buf,sizeof(buf),"Error: Plugin Could Not Be Loaded\n\n%s\n\n%s",filename,error);
		MessageBox(hwnd,buf,"Aodix - VST Host",MB_OK | MB_ICONERROR);
		return;
	}

	// zero midi queue
	pi->midi_queue_size=0;

	// reset instance routing screen position
	pi->x=param_x;
	pi->y=param_y;

	// reset instance properties
	pi->process_mute=0;
	pi->process_thru=0;

	// reset midi-out pin
	pi->mout_pin.pwire=NULL;
	pi->mout_pin.num_wires=0;

	// set dll path (linux port: when a project is loaded, filename is pi->dll_path itself; snprintf onto its own source
	// cleared the first character, so projects were saved with an empty path)
	if(filename!=pi->dll_path)
		snprintf(pi->dll_path,sizeof(pi->dll_path),"%s",filename);

	int const pre_i=pi->peffect->numInputs;
	int const pre_o=pi->peffect->numOutputs;
	int const pre_p=pi->peffect->numParams;

	// create input pins
	if(pi->peffect->numInputs>0)
	{
		// create input pins
		pi->pins=new float*[pi->peffect->numInputs];

		// create input pins buffer
		for(int i=0;i<pi->peffect->numInputs;i++)
			pi->pins[i]=new float[MAX_DSP_BLOCK_SIZE];
	}

	// create output pins
	if(pi->peffect->numOutputs>0)
	{
		// create output pins
		pi->pous=new float*[pi->peffect->numOutputs];

		// create output pins buffer
		for(int o=0;o<pi->peffect->numOutputs;o++)
			pi->pous[o]=new float[MAX_DSP_BLOCK_SIZE];

		// maximum assigned master output pin (at least 1)
		int num_master_output_pins=1;
		for(int oo=0;oo<NUM_DSP_OUTPUTS;oo++)
			if(cfg.audio_output_pin[oo]<audio_num_outputs)
				num_master_output_pins=oo+1;

		// create audio output pin(s) array
		pi->pout_pin=new ADX_PIN[pi->peffect->numOutputs];

		// init pin output array
		for(int o=0;o<pi->peffect->numOutputs;o++)
		{
			// get pin pointer
			ADX_PIN* pp=&pi->pout_pin[o];

			// reset wire array
			pp->pwire=NULL;
			pp->num_wires=0;

			// add wire to master
			if (cfg.instance_autolink)
				edit_add_wire(pp, MASTER_INSTANCE, o%num_master_output_pins, 1.0);
		}
	}

	// check num vst params
	if(pi->peffect->numParams>0)
	{
		// create midi cc array
		pi->pmidi_cc=new unsigned char[pi->peffect->numParams];

		// reset midi cc controllers to 0xFF (255), unassigned
		for(int p=0;p<pi->peffect->numParams;p++)
			pi->pmidi_cc[p]=0xFF;
	}

	// init effect name
	char effect_name[65];
	memset(effect_name,0,sizeof(effect_name));
	pi->peffect->getEffectName(effect_name);

	// check if plugin effect name was set
	if(effect_name[0]!=0)
	{
		// copy to alias name and truncate
		memcpy(pi->alias,effect_name,sizeof(pi->alias));
		pi->alias[sizeof(pi->alias) - 1] = 0;
	}
	else
	{
		// set dll title in string
		GetFileTitle(pi->dll_path,pi->alias,24);

		// kick away extension
		char* const pdot=strrchr(pi->alias,'.');
		if(pdot!=NULL)
			*pdot=0;
	}

	// success instance, init plugin
	pi->peffect->setSampleRate(cfg.audio_driver_sample_rate);
	pi->peffect->setBlockSize(dsp_block_size);
	pi->peffect->setActive(1!=0);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::instance_free(ADX_INSTANCE* pi)
{
	// linux port: a new plugin in this slot gets a new dsp thread
	dsp_instance_slot[pi-instance]=-1;

	// delete vst instance
	if(pi->peffect!=NULL)
	{
		// delete input pins
		if(pi->peffect->numInputs>0)
		{
			// delete input buffers
			for(int i=0;i<pi->peffect->numInputs;i++)
				delete[] pi->pins[i];

			// free pin array
			delete[] pi->pins;
			pi->pins=NULL;
		}

		// delete output pins
		if(pi->peffect->numOutputs>0)
		{
			// scan outputs
			for(int o=0;o<pi->peffect->numOutputs;o++)
			{
				// delete output buffer
				delete[] pi->pous[o];

				// clear output pin
				edit_clr_pin(&pi->pout_pin[o]);
			}

			// free pouts array
			delete[] pi->pous;
			pi->pous=NULL;

			// free output pin array
			delete[] pi->pout_pin;
			pi->pout_pin=NULL;
		}

		// destroy midi cc matrix
		if(pi->peffect->numParams>0)
		{
			delete[] pi->pmidi_cc;
			pi->pmidi_cc=NULL;
		}

		// clear midi-out pin
		edit_clr_pin(&pi->mout_pin);

		// close plugin's editor
		instance_close_editor(pi);

		// close plugin's instance (deleting the plugin object also unloads its library)
		AdxPlugin* const pplug=pi->peffect;
		pi->peffect=NULL;
		pplug->setActive(false);
		delete pplug;

		// reset instance struct members
		pi->hwnd=NULL;
		pi->midi_queue_size=0;
		pi->x=0;
		pi->y=0;
		pi->process_mute=0;
		pi->process_thru=0;

		// set no dll path
		pi->dll_path[0]=0;

		// resest alias
		memset(pi->alias,0,32);
		sprintf(pi->alias,"---");
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ADX_INSTANCE* CAodixCore::instance_get_from_effect(AdxPlugin* peffect,int* pindex)
{
	// scan all instances
	for(int i=0;i<MAX_INSTANCES;i++)
	{
		// instance pointer
		ADX_INSTANCE* pi=&instance[i];

		// check if is same instance
		if(pi->peffect==peffect)
		{
			// set index
			*pindex=i;

			// return pointer
			return pi;
		}
	}

	// else
	return NULL;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::instance_open_editor(ADX_INSTANCE* pi,HWND const hwnd)
{
	// close if previously open
	instance_close_editor(pi);

	// open vst gui
	if(hwnd!=NULL && pi->peffect && (pi->peffect->hasEditor))
	{
		// get parent window area size
		RECT r;
		GetClientRect(hwnd,&r);
		int const w=r.right-r.left;
		int const h=r.bottom-r.top;

		// get plugin editor window size
		int win_w=0;
		int win_h=0;
		if(!pi->peffect->editorGetSize(&win_w,&win_h) || win_w<=0 || win_h<=0)
		{
			win_w=400;
			win_h=300;
		}

		// position window (linux port: the window manager adds the frame, so no frame size correction.
		// the editor is never scaled: its size is in physical pixels, the position in (scaled) aodix coordinates)
		int const win_x=w/2-adxplat::ui_scale_to_logical(win_w)/2;
		int const win_y=h/2-adxplat::ui_scale_to_logical(win_h)/2;

		// pass screen coords
		POINT p;
		p.x=win_x;
		p.y=win_y;
		ClientToScreen(hwnd,&p);

		// create plugin parent window
		// linux port: resizable vst3 editors get a sizing border
		DWORD const win_style=WS_SYSMENU | WS_CAPTION | (pi->peffect->editorCanResize() ? WS_THICKFRAME : 0);

		pi->hwnd=CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_ADX_NATIVE_PIXELS,"VSTAodixHost","Aodix - VST Instance Window",win_style,p.x,p.y,win_w,win_h,hwnd,NULL,hinstance_app,NULL);

		// set window effect parameter
		SetWindowLongPtr(pi->hwnd,GWLP_USERDATA,(LONG_PTR)pi->peffect);

		// update caption
		host_update_window_caption(pi->peffect);

		// show window (the x11 window must exist and be mapped before the plugin view is attached)
		ShowWindow(pi->hwnd,SW_SHOWNORMAL);

		// open editor window (embedded into the native x11 window of the frame)
		if(!pi->peffect->editorOpen(adxplat::x11_window(pi->hwnd)))
		{
			DestroyWindow(pi->hwnd);
			pi->hwnd=NULL;
			MessageBox(hwnd,"Error: Plugin Editor Could Not Be Opened","Aodix - VST Host",MB_OK | MB_ICONERROR);
			return;
		}

		// check for new size
		int nw=win_w;
		int nh=win_h;
		pi->peffect->editorGetSize(&nw,&nh);

		// misteriously resized(!)
		if(nw>0 && nh>0 && (nw!=win_w || nh!=win_h))
		{
			p.x=w/2-adxplat::ui_scale_to_logical(nw)/2;
			p.y=h/2-adxplat::ui_scale_to_logical(nh)/2;
			ClientToScreen(hwnd,&p);
			SetWindowPos(pi->hwnd,NULL,p.x,p.y,nw,nh,SWP_NOZORDER);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::instance_close_editor(ADX_INSTANCE* pi)
{
	// close window if opened
	if(pi->peffect!=NULL && pi->hwnd!=NULL && IsWindow(pi->hwnd))
	{
		// kill child plugin editor window
		if(pi->peffect->hasEditor)
			pi->peffect->editorClose();

		// close window
		DestroyWindow(pi->hwnd);

		// set null handle
		pi->hwnd=NULL;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::instance_add_midi_event(ADX_INSTANCE* pi,int const track,unsigned char const md0,unsigned char const md1,unsigned char const md2,unsigned char const md3,int const deltaframe)
{
	// check if effect is instanced
	if(pi->peffect!=NULL)
	{
		// ignore incoming events if list is full
		if(pi->midi_queue_size<MAX_BLOCK_EVENTS)
		{
			// track midi vumeter update
			if((md0&0xF0)==0x90)
				master_midi_vumeter[track]=md2;

			// get new event pointer
			ADX_MIDI_EVENT* pe=&pi->midi_event[pi->midi_queue_size];

			// prepare event
			pe->deltaFrames=deltaframe;
			pe->midiData[0]=md0;
			pe->midiData[1]=md1;
			pe->midiData[2]=md2;
			pe->midiData[3]=md3;

			// increment queue
			pi->midi_queue_size++;
		}
		else
		{
			// alert of instance process midi buffer overflow
			// (linux port: this can run in the audio thread, so no message box - just log it)
			fprintf(stderr,"aodix: midi event queue overflow (instance midi process)\n");
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::instance_midi_panic(ADX_INSTANCE* pi,bool const all_notes_off,bool const all_sounds_off)
{
	// check if effect is instanced
	if(pi->peffect!=NULL)
	{
		// check all notes off flag
		if(all_notes_off)
		{
			// all notes off thru all channels
			for(int c=0;c<16;c++)
				instance_add_midi_event(pi,user_trk,0xB0+c,123,0,0,0);
		}

		// check all sounds off flag
		if(all_sounds_off)
		{
			// all sounds off thru all channels
			for(int c=0;c<16;c++)
				instance_add_midi_event(pi,user_trk,0xB0+c,120,0,0,0);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::instance_set_param(ADX_INSTANCE* pi,int const param_index,float const param_value)
{
	// check if effect is instanced and alter parameter
	if(pi->peffect!=NULL)
		pi->peffect->setParameter(param_index,param_value);
}
