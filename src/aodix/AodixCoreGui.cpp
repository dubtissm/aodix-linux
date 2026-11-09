/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Core Gui Implementation
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AodixCore.h"

#include <algorithm>
#include <strings.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
WNDPROC gl_wp_orig_edit_proc;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// keynote strings
char const gl_str_not[]={"C-C#D-D#E-F-F#G-G#A-A#B-"};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
LRESULT APIENTRY gui_edit_subclass_proc(HWND hwnd,UINT uMsg,WPARAM wParam,LPARAM lParam)
{
	// extern aodix core pointer and window
	extern CAodixCore*	gl_padx;
	extern HWND			gl_hwnd_main;

	// handle return
	if(uMsg==WM_KEYDOWN && wParam==VK_RETURN)
	{
		gl_padx->gui_process_text_action(gl_hwnd_main);
		return 0;
	}

	// original procedure
	return CallWindowProc(gl_wp_orig_edit_proc,hwnd,uMsg,wParam,lParam);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::gui_create_edit(HWND const hwnd,int x,int y,int const w,int const h,char* default_text,int text_action_id)
{
	// destroy previous text box if popup textbox is already opened
	if(user_edit_text_action_id>0)
		DestroyWindow(hwnd_edit);

	// create edit box
	hwnd_edit=CreateWindow("EDIT",NULL,WS_CHILD | WS_VISIBLE | ES_LEFT,x+4,y+4,w-8,h-8,hwnd,(HMENU)NULL,(HINSTANCE)GetWindowLongPtr(hwnd,GWLP_HINSTANCE),NULL);

	// subclass the edit control.
	gl_wp_orig_edit_proc=(WNDPROC)SetWindowLongPtr(hwnd_edit,GWLP_WNDPROC,(LONG_PTR)gui_edit_subclass_proc);

	// set action id
	user_edit_text_action_id=text_action_id;

	// add text to the window.
	SendMessage(hwnd_edit,WM_SETTEXT,0,(LPARAM)default_text);

	// set all selection
	SendMessage(hwnd_edit,EM_SETSEL,0,-1);

	// set font
	SendMessage(hwnd_edit,WM_SETFONT,(WPARAM)hfont_terminal,TRUE);

	// set text focus
	SetFocus(hwnd_edit);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::gui_process_text_action(HWND const hwnd)
{
	// check if textbox action is performed
	if(user_edit_text_action_id>0)
	{
		// string holder
		char str[32];

		// get text (max 32 chars, incl 0)
		GetWindowText(hwnd_edit,str,32);

		// change program name
		if(user_edit_text_action_id==1)
		{
			// get instance
			ADX_INSTANCE* pi=&instance[user_instance];

			// check if effect is instanced
			if(pi->peffect!=NULL)
			{
				// clamp vst program name
				str[24]=0;

				// dispatch set program name
				pi->peffect->setProgramName(str);

				// update vst editor window caption
				host_update_window_caption(pi->peffect);
			}
		}

		// change track name
		if(user_edit_text_action_id>=2 && user_edit_text_action_id<(2+MAX_TRACKS))
		{
			//get track pointer
			ADX_TRACK* pt=&project.pattern[user_pat].track[user_edit_text_action_id-2];

			// clear and rename track label
			memset(pt->name,0,32);
			strcpy(pt->name,str);
		}

		// change current pattern name
		if(user_edit_text_action_id==512)
		{
			//get pattern pointer
			ADX_PATTERN* pp=&project.pattern[user_pat];

			// clear and rename pattern label
			memset(pp->name,0,32);
			strcpy(pp->name,str);
		}

		// change project name
		if(user_edit_text_action_id==513)
		{
			// clear and rename project label
			memset(project.name,0,32);
			strcpy(project.name,str);
		}

		// change project info
		if(user_edit_text_action_id==514)
		{
			// clear and rename project info
			memset(project.info,0,32);
			strcpy(project.info,str);
		}

		// change current instance alias
		if(user_edit_text_action_id==515)
		{
			// get instance
			ADX_INSTANCE* pi=&instance[user_instance];

			// check if effect is instanced
			if(pi->peffect!=NULL)
			{
				// clear and change instance alias label
				memset(pi->alias,0,32);
				strcpy(pi->alias,str);

				// update vst editor window caption
				host_update_window_caption(pi->peffect);
			}
		}

		// end text action
		user_edit_text_action_id=0;

		// destroy the text window
		DestroyWindow(hwnd_edit);

		// post refresh
		gui_is_dirty=1;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::gui_format_note(int const key,char* buf)
{
	// format buffer
	buf[0]=gl_str_not[(key%12)*2+0];
	buf[1]=gl_str_not[(key%12)*2+1];
	buf[2]='0'+key/12;
	buf[3]=0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::gui_format_pos(int const pos,char* buf)
{
	sprintf(buf,"%.3d:%.1d:%.3d",pos/(project.master_numerator*project.master_ppqn),(pos/project.master_ppqn)%project.master_numerator,pos%project.master_ppqn);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::gui_format_time(int const samples,char* buf)
{
	// calculate double time
	double const samples_per_ms=cfg.audio_driver_sample_rate*0.001;
	double const ms=double(samples)/samples_per_ms;

	// integer time
	int const i_mse=int(ms);
	int const i_sec=i_mse/1000;

	// format string
	sprintf(buf,"%.2d:%.2d:%.3d",(i_sec/60)%60,i_sec%60,i_mse%1000);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::gui_timer(HWND const hwnd)
{
	// get area size
	RECT r;
	GetClientRect(hwnd,&r);
	int const w=r.right-r.left;
	int const h=r.bottom-r.top;

	// temporal vars
	int const seq_area_y=264;
	int const seq_area_h=h-seq_area_y;
	int const seq_cent_y=seq_area_y+seq_area_h/2;
	int const seq_visi_t=(w-user_pr_width)/TRACK_WIDTH;

	// get master play position
	int const play_pos=seq_sample_to_pos(master_transport_sampleframe);

	// get current pattern pointer
	ADX_PATTERN* pp=&project.pattern[user_pat];

	// temporal char holder
	char buf[256];

	// get paint dc
	HDC const hdc=GetDC(hwnd);

	// update watchclock(s) when transport active (playing)
	if(master_time_info.flags & ADX_TRANSPORT_PLAYING)
	{
		// master position display
		paint_seq_pos_big(hdc,10,25,play_pos);

		// master timer
		gui_format_time(master_transport_sampleframe,buf);
		paint_txt(hdc,135,21,buf,9,2);
	}

	// master tempo
	sprintf(buf,"%.3d.%.1d",int(project.master_tempo),int(project.master_tempo*10.0)%10);
	paint_txt(hdc,71,53,buf,5,0);

	// master cpu
	sprintf(buf,"%.2d.%.1d",int(dsp_cpu_cost),int(dsp_cpu_cost*10.0)%10);
	paint_txt(hdc,159,37,buf,4,2);

	// refresh master output vumeter
	paint_master_vumeter(hdc);

	// master vumeter decay
	for(int o=0;o<NUM_DSP_OUTPUTS;o++)
	{
		// check vumeter level
		if(dsp_output_vumeter[o]>1.0e-8f)
			dsp_output_vumeter[o]*=0.9f;
	}

	// update track midi vumeters
	if(user_page==0)
		paint_track_midi_vumeters(hdc,TRACK_WIDTH+user_pr_width,seq_area_y-30,seq_visi_t);

	// transport playback position (linux port: drawn as an overlay over the window image, so it never has to be
	// erased from the sequencer picture; the position is interpolated between audio blocks for a steady movement)
	double const now_ms=adx_now_ms();
	double const frame_dt=(gui_ov_last_tick_ms>0.0)?std::min(0.1,(now_ms-gui_ov_last_tick_ms)*0.001):0.016;
	gui_ov_last_tick_ms=now_ms;

	if((master_time_info.flags & ADX_TRANSPORT_PLAYING) && user_page==0)
	{
		// interpolated sample position: the audio thread advances in whole blocks
		int const frame=master_transport_sampleframe;
		if(frame!=gui_ov_last_frame)
		{
			gui_ov_last_frame=frame;
			gui_ov_last_frame_ms=now_ms;
		}
		double const ahead=std::min(double(dsp_block_size),(now_ms-gui_ov_last_frame_ms)*0.001*cfg.audio_driver_sample_rate);
		int const smooth_pos=seq_sample_to_pos(frame+int(ahead));

		// play position line
		int const i_ty=seq_cent_y+(smooth_pos-pp->usr_pos)/pp->usr_ppp;

		// scroll page follow view if live active
		if(i_ty!=master_transport_last_y)
		{
			master_transport_last_y=i_ty;
			// (linux port: at 95% of the visible height already, the original waited until the line had left the window)
			if(cfg.rec_live && i_ty>seq_area_y+seq_area_h*95/100)
			{
				// set new position
				pp->usr_pos=edit_quantize(arg_tool_clipped_assign(play_pos+(seq_area_h/2)*pp->usr_ppp,0,MAX_SIGNED_INT));

				// post refresh
				gui_is_dirty=1;
			}
		}

		// overlay area: the line
		bool const changed=!gui_ov_active || i_ty!=gui_ov_play_y || w!=gui_ov_width;
		gui_ov_active=true;
		gui_ov_play_y=i_ty;
		gui_ov_seq_y=seq_area_y;
		gui_ov_width=w;
		gui_ov_first_x=TRACK_WIDTH+user_pr_width;
		gui_ov_tracks=seq_visi_t;
		bool const flashing=gui_flash_update(frame_dt,seq_cent_y,pp,h);
		if(changed || flashing)
		{
			// the line, plus the blocks of the flashing events
			RECT ov;
			ov.left=0;
			ov.right=w;
			ov.top=std::max(seq_area_y,i_ty);
			ov.bottom=std::min(h,i_ty+2);
			for(ADX_FLASH const& f:gui_flashes)
			{
				int const fy=seq_cent_y+(f.pos-pp->usr_pos)/pp->usr_ppp;
				int const fh=gui_flash_height(f);
				ov.top=std::max(seq_area_y,std::min<int>(ov.top,fy));
				ov.bottom=std::min(h,std::max<int>(ov.bottom,fy+fh));
			}
			adxplat::overlay_changed(hwnd,ov.bottom>ov.top?&ov:NULL);
		}
	}
	else
	{
		// transport stopped or another page: remove the overlay
		gui_flashes.clear();
		gui_flash_read=dsp_flash_write.load(std::memory_order_acquire);
		if(gui_ov_active)
		{
			gui_ov_active=false;
			adxplat::overlay_changed(hwnd,NULL);
		}
		gui_ov_last_frame=-1;
		for(int t=0;t<MAX_TRACKS;t++)
		{

		}
	}

	// waiting midi-input cc (flash rec-learn indicator)
	if(user_midi_learn)
		BitBlt(hdc,853,177,32,16,hdc_gfx,128+((GetTickCount()/512)&1)*32,572,SRCCOPY);

	// end paint dc
	ReleaseDC(hwnd,hdc);

	// dispatch update flag
	if(gui_is_dirty)
	{
		// set update flag off
		gui_is_dirty=0;

		// post redraw
		InvalidateRect(hwnd,NULL,FALSE);
	}

	// linux port: audio driver housekeeping (jack server shutdown)
	audio_idle(hwnd);

	// host idle
	host_idle();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::gui_delete_current_instance(HWND const hwnd)
{
	// get current selected instance pointer
	ADX_INSTANCE* pi=&instance[user_instance];

	// confirm dialog
	if(pi->peffect!=NULL && MessageBox(hwnd,"Warning: This Operation Cant Be Undone\n\nAre You Sure?","Aodix - Delete Instance",MB_YESNO | MB_ICONWARNING)==IDYES)
	{
		// enter critical section
		audio_enter_cs();

		// disconnect master input wires connected to current machine
		for(int p=0;p<NUM_DSP_INPUTS;p++)
		{
			// get master input pin pointer
			ADX_PIN* pp=&master_input_pin[p];

			// scan wires
			for(int w=0;w<pp->num_wires;w++)
			{
				// get master input wire pointer
				ADX_WIRE* pw=&pp->pwire[w];

				// disconnect wire
				if(pw->instance_index==user_instance)
					edit_del_wire(pp,w--);
			}
		}

		// disconnect all machines connected to current machine (wich will be deleted)
		for(int i=0;i<MAX_INSTANCES;i++)
		{
			// ignore current machine
			if(i!=user_instance)
			{
				// get source instance
				ADX_INSTANCE* pcoi=&instance[i];

				// check if source instance is loaded
				if(pcoi->peffect!=NULL)
				{
					// scan source output pins
					for(int o=0;o<pcoi->peffect->numOutputs;o++)
					{
						// get master input pin pointer
						ADX_PIN* pp=&pcoi->pout_pin[o];

						// scan wires
						for(int w=0;w<pp->num_wires;w++)
						{
							// get instance wire pointer
							ADX_WIRE* pw=&pp->pwire[w];

							// disconnect wire
							if(pw->instance_index==user_instance)
								edit_del_wire(pp,w--);
						}
					}

					// scan source midi-out link wires
					for(int w=0;w<pcoi->mout_pin.num_wires;w++)
					{
						// get instance wire pointer
						ADX_WIRE* pw=&pcoi->mout_pin.pwire[w];

						// if source midi-out link is connected to current instance to be deleted, unwire that connection
						if(pw->instance_index==user_instance)
							edit_del_wire(&pcoi->mout_pin,w--);
					}
				}
			}
		}

		// free instance
		instance_free(pi);

		// leave critical section
		audio_leave_cs();
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::gui_scroll_to_instance()
{
	// instance before instance list offset
	if(user_instance<user_instance_list_offset)
		user_instance_list_offset=user_instance;

	// instance after instance list offset
	if(user_instance>(user_instance_list_offset+10))
		user_instance_list_offset=user_instance-10;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CAodixCore::gui_scroll_iterate(HWND const hwnd,int const ym,int const seq_area_y,int const h)
{
	// get current pattern pointer
	ADX_PATTERN* pp=&project.pattern[user_pat];

	// top scroller
	if(ym<seq_area_y)
		pp->usr_pos=arg_tool_clipped_assign(pp->usr_pos-pp->usr_ppp*8,0,MAX_SIGNED_INT);

	// bottom scroller
	if(ym>(h-19))
		pp->usr_pos=arg_tool_clipped_assign(pp->usr_pos+pp->usr_ppp*8,0,MAX_SIGNED_INT);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int CAodixCore::gui_get_event_height(ADX_EVENT* pe)
{
	return arg_tool_clipped_assign((1-pe->szd)*NR_EVENT_HEIGHT+pe->szd*(pe->par/project.pattern[user_pat].usr_ppp),2,MAX_SIGNED_INT);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// linux port: play position overlay (called by the platform whenever a part of the window is sent to the screen)
void CAodixCore::gui_overlay_paint(adxplat::OverlayCanvas* cv)
{
	if(!gui_ov_active)
		return;

	int const y=gui_ov_play_y;


	// flashing events: their block in the theme color, fading into the block
	if(gui_flash_ms()>0)
	{
		for(ADX_FLASH const& f:gui_flashes)
		{
			int const t=f.trk-user_trk_offset;
			if(t<0 || t>=gui_ov_tracks)
				continue;
			int const fx=gui_ov_first_x+t*TRACK_WIDTH;
			int fy=gui_ov_cent_y+(f.pos-gui_ov_usr_pos)/gui_ov_usr_ppp;
			int fh=gui_flash_height(f);
			if(fy<gui_ov_seq_y)
			{
				fh-=gui_ov_seq_y-fy;
				fy=gui_ov_seq_y;
			}
			if(fh>0)
				adxplat::overlay_fill_keep(cv,fx,fy,TRACK_WIDTH,fh,f.color,int(f.level*255.0f+0.5f),f.text);
		}
	}

	// play position line on top (classic: inverted like the original)
	if(y>=gui_ov_seq_y)
	{
		if(gl_adx_theme.play_line_invert)
			adxplat::overlay_invert(cv,0,y,gui_ov_width,2);
		else
			adxplat::overlay_fill(cv,0,y,gui_ov_width,2,gl_adx_theme.play_line,255);
	}
}

static void adx_overlay_proc(HWND,adxplat::OverlayCanvas* cv,void* user)
{
	((CAodixCore*)user)->gui_overlay_paint(cv);
}

void adx_install_overlay(HWND hwnd,CAodixCore* core)
{
	adxplat::set_overlay(hwnd,adx_overlay_proc,core);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// linux port: events that started playing flash (taken from the audio thread), then fade. returns true while any flashes
bool CAodixCore::gui_flash_update(double const dt,int const seq_cent_y,ADX_PATTERN const* pp,int const h)
{
	// geometry for the overlay
	gui_ov_cent_y=seq_cent_y;
	gui_ov_usr_pos=pp->usr_pos;
	gui_ov_usr_ppp=std::max(1,pp->usr_ppp);
	gui_ov_bottom=h;

	bool const had=!gui_flashes.empty();

	// fade
	for(size_t i=0;i<gui_flashes.size();)
	{
		gui_flashes[i].level-=float(dt)*1000.0f/float(std::max(1,gui_flash_ms()));
		if(gui_flashes[i].level<=0.0f)
			gui_flashes.erase(gui_flashes.begin()+i);
		else
			i++;
	}

	// new triggers (when the gui fell far behind, only the newest ones; none when flashing is off)
	unsigned const w=dsp_flash_write.load(std::memory_order_acquire);
	if(gui_flash_ms()==0)
	{
		gui_flash_read=w;
		gui_flashes.clear();
		return had;
	}
	if(w-gui_flash_read>1024)
		gui_flash_read=w-1024;
	for(;gui_flash_read!=w;gui_flash_read++)
	{
		ADX_FLASH_TRIGGER const t=dsp_flash_trigger[gui_flash_read&1023];
		bool found=false;
		for(ADX_FLASH& f:gui_flashes)
		{
			if(f.pos==t.pos && f.trk==t.trk && f.kind==t.kind)
			{
				f.level=1.0f;
				f.color=gui_event_flash_color(t);
				f.text=gui_event_text_color(t);
				found=true;
				break;
			}
		}
		if(!found && gui_flashes.size()<256)
		{
			ADX_FLASH f;
			f.pos=t.pos;
			f.len=t.len;
			f.trk=t.trk;
			f.kind=t.kind;
			f.szd=t.len>0;
			f.color=gui_event_flash_color(t);
			f.text=gui_event_text_color(t);
			f.level=1.0f;
			gui_flashes.push_back(f);
		}
	}

	return had || !gui_flashes.empty();
}

// height of a flashing event block, like gui_get_event_height
int CAodixCore::gui_flash_height(ADX_FLASH const& f)
{
	if(!f.szd)
		return NR_EVENT_HEIGHT;
	return std::max(2,f.len/std::max(1,gui_ov_usr_ppp));
}

// flash color of an event: the theme color if the theme sets one, otherwise the block color made much lighter
COLORREF CAodixCore::gui_event_flash_color(ADX_FLASH_TRIGGER const& t)
{
	// pattern calls have their own flash color (not the one of the notes they play)
	COLORREF const theme=(t.typ==EVT_PAT)?gl_adx_theme.flash_block:(t.kind?gl_adx_theme.flash_automation:gl_adx_theme.flash_note);
	if(theme!=ADX_THEME_AUTO)
		return theme;

	// block colors as painted by the sequencer
	COLORREF back=gl_adx_theme.note_plain;
	switch(t.typ)
	{
	case EVT_NOT:
		if(cfg.note_colors)
		{
			int const inst_div_24=(t.da0*0xAAB)>>16;
			int const inst_mod_24=t.da0-inst_div_24*24;
			int const hue=(inst_mod_24<<4)|((inst_div_24&1)<<3)|((inst_div_24&2)<<1)|((inst_div_24&4)>>1)|((inst_div_24&8)>>3);
			back=adx_color_add(adx_theme_note_color(hue),gl_adx_theme.note_back_add);
		}
		break;
	case EVT_PAT: back=gl_adx_theme.pattern; break;
	case EVT_JMP: back=gl_adx_theme.jump; break;
	case EVT_MID: back=gl_adx_theme.midi; break;
	case EVT_AUT: back=adx_color_add(gl_adx_theme.automation,t.da3/2); break;
	case EVT_TMP: back=adx_color_add(gl_adx_theme.tempo,t.da0/2); break;
	}
	return adx_color_lerp(back,0x00FFFFFF,ADX_FLASH_LIGHTEN);
}

// flash time from the configuration (combo: no flash, 100, 200, 300, 400, 600, 800, 1000 ms; default 400 ms)
static int const g_flash_times[]={0,100,200,300,400,600,800,1000};

int CAodixCore::gui_flash_ms(void)
{
	int const idx=cfg.flash_time?int(cfg.flash_time)-1:4;
	return (idx>=0 && idx<8)?g_flash_times[idx]:400;
}

// stored flash time (combo index + 1) of a time in ms: the nearest offered time
int adx_flash_time_stored(int ms)
{
	int best=0;
	for(int i=1;i<8;i++)
		if(abs(g_flash_times[i]-ms)<abs(g_flash_times[best]-ms))
			best=i;
	return best+1;
}

// text color of an event block as painted by the sequencer
COLORREF CAodixCore::gui_event_text_color(ADX_FLASH_TRIGGER const& t)
{
	switch(t.typ)
	{
	case EVT_NOT: return adx_color_add(gl_adx_theme.note_text,arg_tool_clipped_assign(int(t.da3),0,127)*0x00010101);
	case EVT_PAT: return gl_adx_theme.pattern_text;
	case EVT_JMP: return gl_adx_theme.jump_text;
	case EVT_MID: return gl_adx_theme.midi_text;
	case EVT_AUT: return gl_adx_theme.automation_text;
	case EVT_TMP: return gl_adx_theme.tempo_text;
	}
	return 0x00FFFFFF;
}
