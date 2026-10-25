/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Core Host Implementation
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AodixCore.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// linux port: the VST 2 audiomaster callback is replaced by the AdxPluginHost interface. Plugin formats
// (vst3 / clap) translate their host callbacks to these methods.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::plugin_automate(AdxPlugin* plugin,int param,float value)
{
	// instance wants to automate a parameter (was audioMasterAutomate)
	host_on_automation(plugin,param,value);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::plugin_midi_out(AdxPlugin* plugin,const ADX_MIDI_EVENT* events,int num_events)
{
	// instance wants host to process events (was audioMasterProcessEvents)
	host_on_events(plugin,events,num_events);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::plugin_update_display(AdxPlugin* plugin)
{
	// instance wants host to update its display (was audioMasterUpdateDisplay)
	host_update_window_caption(plugin);

	// something has changed, post gui update
	gui_is_dirty=1;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::plugin_resize_editor(AdxPlugin* plugin,int width,int height)
{
	// instance notifies change in editor's size (was audioMasterSizeWindow, unsupported in the original)
	int index=0;
	ADX_INSTANCE* pi=instance_get_from_effect(plugin,&index);

	if(pi!=NULL && pi->hwnd!=NULL && IsWindow(pi->hwnd))
		SetWindowPos(pi->hwnd,NULL,0,0,width,height,SWP_NOMOVE | SWP_NOZORDER);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const ADX_TIME_INFO* CAodixCore::plugin_time_info(void)
{
	// instance wants timeinfo (was audioMasterGetTime)
	return &master_time_info;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::host_idle(void)
{
	// scan all instances
	for(int i=0;i<MAX_INSTANCES;i++)
	{
		// get plugin instance
		ADX_INSTANCE* pi=&instance[i];

		// instance is allocated?
		if(pi->peffect!=NULL)
		{
			// call plugin's idle (linux port: also drives the editor)
			pi->peffect->idle();
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::host_update_window_caption(AdxPlugin* peffect)
{
	// instance index
	int i_index=0;

	// get instance pointer and index
	ADX_INSTANCE* pi=instance_get_from_effect(peffect,&i_index);

	// update plugin window if opened
	if(pi!=NULL && pi->peffect!=NULL && pi->hwnd!=NULL && IsWindow(pi->hwnd))
	{
		// get selected program
		int const p_index=pi->peffect->getProgram();

		// set plugin window caption
		char buf[128];
		char prg_buf[32];
		prg_buf[0]=0;

		// get program name
		peffect->getProgramName(prg_buf);

		// format string
		sprintf(buf,"%.2X: %s - %.2X: %s",i_index,pi->alias,p_index,prg_buf);

		// set window caption
		SetWindowText(pi->hwnd,buf);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::host_on_automation(AdxPlugin* peffect,int const param,float const value)
{
	extern HWND gl_hwnd_main;

	// check if effect sending automation is loaded and param index is in valid range
	if(peffect!=NULL && param>=0 && param<peffect->numParams)
	{
		// automation enabled and effect is not null
		if((master_time_info.flags & ADX_AUTOMATION_WRITING) && peffect!=NULL)
		{
			// instance index and next event position
			int instance_index=0;
			int new_event_pos=project.pattern[user_pat].usr_pos;

			// get instance pointer and index
			ADX_INSTANCE* pi=instance_get_from_effect(peffect,&instance_index);

			// write automation event (live else offline)
			if((master_time_info.flags & ADX_TRANSPORT_PLAYING) && cfg.rec_live)
				new_event_pos=seq_sample_to_pos(master_transport_sampleframe);

			// update undo (combining)
			edit_undo_snapshot(true);

			// add automation event
			seq_add_event(new_event_pos,user_pat,user_trk,EVT_AUT,instance_index,(param>>8)&0xFF,(param&0xFF),int(value*255.0f),user_edit_overwrite);
		}

		// get user instance
		ADX_INSTANCE* pui=&instance[user_instance];

		// select param if instance automated is same as user selected instance
		if(peffect==pui->peffect)
		{
			// select param
			user_parameter=param;

			// new parameter index before parameter list offset
			if(user_parameter<user_parameter_list_offset)
				user_parameter_list_offset=user_parameter;

			// new parameter after parameter list offset
			if(user_parameter>(user_parameter_list_offset+10))
				user_parameter_list_offset=user_parameter-10;
		}

		// post refresh
		gui_is_dirty=1;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::host_on_events(AdxPlugin* peffect,const ADX_MIDI_EVENT* pevents,int const num_events)
{
	// instance index
	int i_index=0;

	// get instance pointer and index
	ADX_INSTANCE* pi=instance_get_from_effect(peffect,&i_index);

	// update plugin window if opened
	if(pi!=NULL && pi->peffect!=NULL)
	{
		// scan midi-out wire links
		for(int w=0;w<pi->mout_pin.num_wires;w++)
		{
			// get wire pointer
			ADX_WIRE* pw=&pi->mout_pin.pwire[w];

			// get destination midi link instance
			ADX_INSTANCE* pi_mol=&instance[pw->instance_index];

			// check that effect is opened
			if(pi_mol!=NULL)
			{
				// scan incoming event stream
				for(int e=0;e<num_events;e++)
				{
					// get event pointer
					const ADX_MIDI_EVENT* pmi=&pevents[e];

					// check destinator queue
					if(pi_mol->midi_queue_size<MAX_BLOCK_EVENTS)
					{
						// get instance event pointer
						ADX_MIDI_EVENT* ptr_routed_event=&pi_mol->midi_event[pi_mol->midi_queue_size];

						// copy event from vst host audio master
						*ptr_routed_event=*pmi;

						// scale velocity
						if((ptr_routed_event->midiData[0]&0xF0)==0x90)
							ptr_routed_event->midiData[2]=arg_tool_clipped_assign(int(float(ptr_routed_event->midiData[2])*pw->value),1,127);

						// increment queue
						pi_mol->midi_queue_size++;
					}
				}
			}
		}
	}
}
