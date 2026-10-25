/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Core Header
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define MAX_INSTANCES		256
#define MAX_EVENTS			262144
#define MAX_TRACKS			256
#define MAX_PATTERNS		256
#define MAX_MARKERS			256
#define MAX_SIGNED_INT		0x7FFFFFFF
#define MAX_DSP_BLOCK_SIZE	2048
#define MAX_VST_FOLDERS		4
#define MAX_CLAP_FOLDERS	4
#define MAX_VST_LIB_ENTRIES	1024
#define NUM_RECENT_FILES	4
#define NUM_DSP_INPUTS		32
#define NUM_DSP_OUTPUTS		32
#define	TRACK_WIDTH			96
#define NR_EVENT_HEIGHT		14
#define MASTER_INSTANCE		256
#define INPUT_INSTANCE		258
#define IDM_VST_LIB			8192

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// maximum number of midi events queued per instance and processing block
#define MAX_BLOCK_EVENTS	1024

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// linux port: the win32 api subset used by aodix is provided by the platform layer (X11)
#include "../platform/win32compat.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "resource.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "../argulib/Argulib.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// linux port: plugins are accessed through the format independent AdxPlugin interface (VST3)
#include "../plugin/AdxPlugin.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AodixLinux.h"
#include "AodixDspPool.h"
#include <vector>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// window procedures
LRESULT CALLBACK main_wnd_proc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK plug_wnd_proc(HWND,UINT,WPARAM,LPARAM);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// dialog procedures
LRESULT CALLBACK conf_dlg_proc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK boun_dlg_proc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK abou_dlg_proc(HWND,UINT,WPARAM,LPARAM);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct ADX_CONFIG
{
	// reserved
	char reserved[_MAX_PATH];

	// user GUI parameters
	int	instance_autolink;
	int	stop_wrap;
	int	rec_live;
	int	keyboard_layout;

	// audio driver index (linux port: 0 = jack, -1 = no audio)
	int	audio_driver_id;

	// audio driver sample rate (linux port: reported by the jack server)
	double audio_driver_sample_rate;

	// midi input
	int midi_in_ch_rout;
	int midi_in_vl_rout;
	int midi_in_dv_open;

	// display settings
	unsigned char note_colors;

	// linux port: number of jack audio ports (in and out), 0 = default (2)
	unsigned char audio_num_ports;

	// linux port: user interface scaling, 0 = 100%, 1 = 150%, 2 = 200%
	unsigned char ui_scale;

	// linux port: number of dsp threads, 0 = automatic, 1 = no parallel processing, n = n threads
	unsigned char dsp_threads;

	// linux port: buffer size / sample rate requested from pipewire (combo index + 1, 0 = 256 samples / 48000 hz)
	unsigned char pw_buffer_size;
	unsigned char pw_sample_rate;

	// reserved fields
	char reserved2[50];

	// registration info
	char user_name[32];
	char user_reserved[32];

	// audio input/output pins assigns (linux port: jack port index)
	int audio_input_pin[NUM_DSP_INPUTS];
	int audio_output_pin[NUM_DSP_OUTPUTS];

	// vst plugin paths
	char vst_path[MAX_VST_FOLDERS][_MAX_PATH];

	// recent opened files paths
	char recent_file[NUM_RECENT_FILES][_MAX_PATH];

	// skin folder path
	char skin_path[_MAX_PATH];

	// open in fullscreen mode
	int fullscreen;

	// linux port: clap plugin paths (appended to the original structure; older files simply end before)
	char clap_path[MAX_CLAP_FOLDERS][_MAX_PATH];
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
enum ADX_EVENT_TYPE : unsigned char
{
	EVT_NOT, EVT_PAT, EVT_JMP, // sized events
	EVT_MID, EVT_AUT, EVT_TMP, // unsized events
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct ADX_EVENT
{
	// event position and parameter
	int pos;
	int par;

	// properties
	unsigned char pat;
	unsigned char trk;
	unsigned char typ;
	unsigned char szd;

	// data
	unsigned char da0;
	unsigned char da1;
	unsigned char da2;
	unsigned char da3;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct ADX_WIRE
{
	short instance_index;
	short pin_index;
	float value;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct ADX_PIN
{
	ADX_WIRE* pwire;
	int num_wires;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct ADX_INSTANCE
{
	// handle to vst instance editor frame window
	HWND hwnd;

	// the plugin instance pointer (linux port: format independent plugin object, owns the loaded library)
	AdxPlugin* peffect;

	// inputs and outputs pins
	float**	pins;
	float**	pous;

	// audio output pins / midi output connector vector
	ADX_PIN* pout_pin;
	ADX_PIN  mout_pin;

	// plugin alias name
	char alias[32];

	// dll path (where the vst dll is stored)
	char dll_path[_MAX_PATH];

	// counter of midi events queue size in processing block
	int midi_queue_size;

	// midi events for processing block
	ADX_MIDI_EVENT midi_event[MAX_BLOCK_EVENTS];

	// graphical screen position in routing
	int	x;
	int	y;

	// instance properties
	unsigned char process_mute;
	unsigned char process_thru;

	// midi-cc matrix vector
	unsigned char* pmidi_cc;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct ADX_TRACK
{
	// track label
	char name[32];

	// mute / solo events
	unsigned char mute;
	unsigned char solo;
	unsigned char res0;
	unsigned char res1;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct ADX_MARKER
{
	int	pos;
	int	flg;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct ADX_PATTERN
{
	// pattern label
	char name[32];

	// pattern tracks
	ADX_TRACK track[MAX_TRACKS];

	// cue data of this pattern
	int	cue_sta;
	int	cue_end;
	int cue_stp;
	int cue_res;

	// user position
	int usr_pos;
	int usr_ppp;
	int usr_pre;
	int usr_mod;

	// reserved
	int reserved[8];

	// pattern markers array
	ADX_MARKER marker[MAX_MARKERS];
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct ADX_PROJECT
{
	// project name and info
	char name[32];
	char info[32];

	// project tempo (in beats per minute)
	double master_tempo;

	// patterns
	ADX_PATTERN pattern[MAX_PATTERNS];

	// master info
	int	master_numerator;
	int	master_denominator;
	int master_ppqn;

	int ext_offset; // ADX file extended data offset
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// linux port: these structures are written to / read from .adx files as raw memory, their layout must
// stay identical to the 32-bit windows version (see docs/adx-spec.txt)
static_assert(sizeof(ADX_EVENT)==0x10,"ADX_EVENT layout changed");
static_assert(sizeof(ADX_WIRE)==0x08,"ADX_WIRE layout changed");
static_assert(sizeof(ADX_TRACK)==0x24,"ADX_TRACK layout changed");
static_assert(sizeof(ADX_MARKER)==0x08,"ADX_MARKER layout changed");
static_assert(sizeof(ADX_PATTERN)==0x2C60,"ADX_PATTERN layout changed");
static_assert(sizeof(ADX_PROJECT)==0x2C6058,"ADX_PROJECT layout changed");

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct ADX_VST_LIB
{
	// path of each dll (linux port: vst3 bundle path)
	char dll_path[MAX_VST_LIB_ENTRIES][_MAX_PATH];

	// linux port: unique id of the plugin class inside the bundle (a bundle may contain several plugins)
	int32_t dll_id[MAX_VST_LIB_ENTRIES];

	// num vst dlls found
	int num_dlls;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// linux port: parallel dsp processing (AodixCoreDspGraph.cpp)

// one audio contribution: source output pin mixed into a destination input pin
struct ADX_DSP_MIX
{
	float*	src;
	float*	dst;
	float	gain;
};

// one midi-out wire (source instance and wire velocity factor) delivered to a destination
struct ADX_DSP_MIDI_LINK
{
	int		src;
	float	value;
};

// midi events produced by an instance during its process call
#define MAX_MIDI_CAPTURE_SEGMENTS 8
struct ADX_MIDI_CAPTURE
{
	int				num_events;
	int				num_segments;
	int				segment_end[MAX_MIDI_CAPTURE_SEGMENTS];
	ADX_MIDI_EVENT	event[MAX_BLOCK_EVENTS];
};

// capture target of the calling thread (set while an instance is processed)
extern thread_local ADX_MIDI_CAPTURE* g_dsp_midi_capture;

// linux port: combo index of the stored pipewire buffer size / sample rate (AodixCoreAudio.cpp)
int audio_pw_buffer_index(unsigned char stored);
int audio_pw_rate_index(unsigned char stored);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CAodixCore : public AdxPluginHost
{
public:
	CAodixCore(HINSTANCE const hinstance);
	~CAodixCore(void);

public:
	void reset_user(void);
	void reset_project(void);
	void reset_pattern(ADX_PATTERN* pp);

public:
	void config_write(void);
	void config_read(void);

public:
	void recent_files_push(char* new_file);

public:
	void gui_create_edit(HWND const hwnd,int x,int y,int const w,int const h,char* default_text,int text_action_id);
	void gui_process_text_action(HWND const hwnd);
	void gui_format_note(int const key,char* buf);
	void gui_format_pos(int const pos,char* buf);
	void gui_format_time(int const samples,char* buf);
	void gui_mouse_down(HWND const hwnd,bool const is_double_click);
	void gui_mouse_up(HWND const hwnd);
	void gui_mouse_move(HWND const hwnd);
	void gui_mouse_scan_pin(HWND const hwnd,int const xm,int const ym,ADX_PIN* ppins,int const num_pins,int const pin_x,int const pin_y,int const is_double_click,int const is_midi_pin);
	void gui_timer(HWND const hwnd);
	void gui_command(HWND const hwnd,int const id);
	void gui_mouse_wheel(HWND const hwnd,int const delta);
	void gui_key_down(HWND const hwnd,int const keycode,int const flags);
	void gui_key_up(HWND const hwnd,int const keycode);
	void gui_key_pc_piano(int const keycode,int* pkeyval);
	void gui_delete_current_instance(HWND const hwnd);
	void gui_scroll_to_instance(void);
	void gui_vst_build_lib(void);
	void gui_scroll_iterate(HWND const hwnd,int const ym,int const seq_area_y,int const h);

public:
	int gui_vst_search(HMENU const pa_hmenu,char* pfolder,int mode=0);	// mode: 0 = vst3, 1 = ladspa, 2 = clap
	int	gui_get_event_height(ADX_EVENT* pe);

public:
	void paint_master_vumeter(HDC const hdc);
	void paint_block_shade(HDC const hdc,int const x,int const y,int const w,int const h,int const lx1,int const ly1,int const lx2,int const ly2);
	void paint_txt(HDC const hdc,int const x,int const y,char* txt,int const num_chars,int const type);
	void paint_seq_pos_big(HDC const hdc,int const x,int const y,int const pos);
	void paint_event(HDC const hdc,int const x,int const y,int const w,int const h,COLORREF const text_color,COLORREF const back_color,char* text);
	void paint_marker(HDC const hdc,COLORREF const color,int const type,char* label,int const y,int const w,int const label_offset,int const selected);
	void paint_wire(HDC const hdc,int const x1,int const y1,int const x2,int const y2,COLORREF const clr);
	void paint_pin_array(HDC const hdc,int const x,int const y,int const num_pins,ADX_PIN* ppin,int const master_o_x,int const master_o_y,int const rout_area_y);
	void paint_track_midi_vumeters(HDC const hdc,int const x,int const y,int const visible_tracks);
	void paint(HWND const hwnd,HDC const hdc,int const w,int const h);

public:
	void paint_sequencer(HWND const hwnd,HDC const hdc,int const w,int const h);
	void paint_routing(HWND const hwnd,HDC const hdc,int const w,int const h);

public:
	void dsp_work(void);
	void dsp_clear_input_buffers(void);
	void dsp_transport_play(void);
	void dsp_transport_stop(void);
	int  dsp_play_events(int i_samples_per_point,int block_sample_sta,int block_sample_end,int deltaframe,int const track_on[],double* tempo_change);
	void dsp_stop_playing_notes(int pos,int pat,int trk,bool check_muted,int deltaframe);
	void dsp_vumeter_drive(float* psrc,float& vum,int const num_samples);

	// linux port: parallel dsp processing
	void dsp_configure_threads(void);
	int  dsp_auto_threads(void);
	void dsp_route_and_process(int* hpc);
	void dsp_node_work(int node);
	void dsp_deliver_midi(int src,int dst,float value);
	static void dsp_node_work_cb(void* ctx,int node);

public:
	int	seq_sample_to_pos(int const sample);
	int	seq_pos_to_sample(int const pos);

public:
	void seq_delete_event(int const index);
	void seq_delete_events_at(int const pat,int const pos,int const pos_rng,int const trk,int const trk_rng);
	void seq_delete_events_at_pattern(int const pat);
	void seq_add_event(int const pos,unsigned char const pat,unsigned char const trk,unsigned char const typ,unsigned char const da0,unsigned char const da1,unsigned char const da2,unsigned char const da3,int const overwrite);
	void seq_add_evmid(int const pos,unsigned char const pat,unsigned char const trk,unsigned char const ins,unsigned char const da0,unsigned char const da1,unsigned char const da2,int const overwrite);
	void seq_sort();

public:
	void instance_dll(HWND const hwnd,ADX_INSTANCE* pi,char* filename,int const param_x,int const param_y);
	void instance_free(ADX_INSTANCE* pi);
	void instance_open_editor(ADX_INSTANCE* pi,HWND const hwnd);
	void instance_close_editor(ADX_INSTANCE* pi);
	void instance_add_midi_event(ADX_INSTANCE* pi,int const track,unsigned char const md0,unsigned char const md1,unsigned char const md2,unsigned char const md3,int const deltaframe);
	void instance_midi_panic(ADX_INSTANCE* pi,bool const all_notes_off,bool const all_sounds_off);
	void instance_set_param(ADX_INSTANCE* pi,int const param_index,float const param_value);

public:
	void host_idle(void);
	void host_on_automation(AdxPlugin* peffect,int const param,float const value);
	void host_on_events(AdxPlugin* peffect,const ADX_MIDI_EVENT* pevents,int const num_events);
	void host_update_window_caption(AdxPlugin* peffect);

public:
	// AdxPluginHost (callbacks from plugins)
	void plugin_automate(AdxPlugin* plugin,int param,float value) override;
	void plugin_midi_out(AdxPlugin* plugin,const ADX_MIDI_EVENT* events,int num_events) override;
	void plugin_update_display(AdxPlugin* plugin) override;
	void plugin_resize_editor(AdxPlugin* plugin,int width,int height) override;
	const ADX_TIME_INFO* plugin_time_info(void) override;

public:
	ADX_INSTANCE* instance_get_from_effect(AdxPlugin* peffect,int* pindex);

public:
	void edit_undo_snapshot(bool combine=false);
	void edit_undo_combine_next(void);
	void edit_undo(void);
	void edit_insert(int const all_tracks);
	void edit_back(int const all_tracks);
	void edit_resize(int const all_tracks);
	void edit_adjust_size(int amt,int ignore_event);
	void edit_copy(int const cut);
	void edit_paste(void);
	void edit_select_all(void);
	void edit_interpolate(void);
	void edit_transpose(int const amt,int const apply_quantize);
	void edit_randomize(void);
	void edit_add_new_marker(ADX_PATTERN* pp,int const position);

public:
	bool edit_is_solo_mode(int pat);
	void edit_toggle_mute(int pat,int trk);
	void edit_toggle_solo(int pat,int trk);

public:
	void edit_add_wire(ADX_PIN* pp,int const instance_index,int const pin_index,float const gain);
	void edit_del_wire(ADX_PIN* pp,int const index);
	void edit_clr_pin(ADX_PIN* pp);

public:
	ADX_EVENT* edit_find_event(int pat,int pos,int trk);
	ADX_EVENT* edit_find_next_event(int pat,int pos,int trk,int len);
	ADX_EVENT* edit_find_prev_event(int pat,int pos,int trk,int len);

public:
	int	edit_get_quantization(void);
	int edit_quantize(int const position);

public:
	void import_adx_file_dlg(HWND const hwnd);
	void import_adx_pin(ADX_PIN* pp,int num_pins,FILE* pfile);
	void import_adx_program(ADX_INSTANCE* pi,FILE* pfile);
	void import_adx_file(HWND const hwnd,char* filename);
	void import_search_file(char* pfolder,char* pfilename,char* pmatchfile);
	int  import_localize_vst_dll(HWND const hwnd,int index,char* pfilename);
	int32_t import_reverse_long(int32_t const data);
	void import_cub_file_dlg(HWND const hwnd);
	void import_cub_file(HWND const hwnd,char* filename);
	void import_midi_file_dlg(HWND const hwnd);
	void import_midi_file(HWND const hwnd,char* filename);
	int32_t import_midi_read_long(FILE* pfile);
	int  import_midi_read_short(FILE* pfile);
	int32_t import_midi_read_var_len(FILE* pfile);
	void import_midi_on_sysex(FILE* pfile,int const pattern_index,int const track,int const i_event_pos,unsigned char const stat);

public:
	void export_adx_file_dlg(HWND const hwnd);
	void export_adx_pin(ADX_PIN* pin_array,int num_pins,FILE* pfile);
	void export_adx_program(ADX_INSTANCE* pi,FILE* pfile);
	void export_adx_file(HWND const hwnd,char* filename);
	void export_cub_file_dlg(HWND const hwnd,int const is_fxp);
	void export_cub_file(HWND const hwnd,char* filename,int const is_fxp);
	void export_mid_file_dlg(HWND const hwnd);
	void export_mid_file(HWND const hwnd,char* filename);

public:
	void midi_in_init(void);
	void midi_in_process(BYTE const data0,BYTE const data1,BYTE const data2);
	void midi_in_close(void);
	int  midi_in_get_num_devices(void);
	void midi_in_get_device_name(int const index,char* buf,int const num_chars);

public:
	void audio_enter_cs(void);
	void audio_leave_cs(void);
	void audio_init(HWND const hwnd);
	void audio_close(HWND const hwnd);
	bool audio_is_inited(void);
	void audio_get_driver_name(int const id,char* buf,int const num_chars);
	int32_t audio_init_list(void);
	void audio_fill_pin_menu(HMENU const hmenu,int const fill_inputs,int const init_id);
	void audio_control_panel(void);
	void audio_shutdown(void);
	void audio_idle(HWND const hwnd);

public:
	// application instance handler
	HINSTANCE hinstance_app;

	// window handler
	HWND hwnd_edit;

	// brush
	HBRUSH hbrush_edit;

	// gfx handler
	HDC hdc_gui;
	HDC hdc_gfx;
	HDC hdc_knb;

	// cursors
	HCURSOR	hcursor_diag;
	HCURSOR	hcursor_move;
	HCURSOR	hcursor_arro;
	HCURSOR	hcursor_szwe;
	HCURSOR	hcursor_szns;
	HCURSOR	hcursor_beam;
	HCURSOR	hcursor_wait;
	HCURSOR	hcursor_hacl;
	HCURSOR	hcursor_haop;

	// popup menus
	HMENU hmenu_vst_lib;
	HMENU hmenu_timesig;
	HMENU hmenu_ppqn;
	HMENU hmenu_patterns;
	HMENU hmenu_quantize;
	HMENU hmenu_midi_ch;
	HMENU hmenu_kbd_octave;
	HMENU hmenu_midi_mask;
	HMENU hmenu_program;
	HMENU hmenu_instance;
	HMENU hmenu_audio_input;
	HMENU hmenu_audio_output;

	// fonts
	HFONT hfont_terminal;

	// midi in devices opened (linux port: replaces the HMIDIIN array, only tested for null in the gui)
	void* p_hmidi_in;

public:
	// dsp variables
	float dsp_input_buffer[NUM_DSP_INPUTS][MAX_DSP_BLOCK_SIZE];
	float dsp_output_buffer[NUM_DSP_OUTPUTS][MAX_DSP_BLOCK_SIZE];

	// linux port: parallel dsp processing (processing order, dependency graph and per node input lists of the current block)
	AdxDspPool						dsp_pool;
	int								dsp_num_nodes;
	int								dsp_node_instance[MAX_INSTANCES];
	int								dsp_instance_node[MAX_INSTANCES];
	int								dsp_node_pending[MAX_INSTANCES];
	int								dsp_node_succ_off[MAX_INSTANCES+1];
	int								dsp_node_mix_off[MAX_INSTANCES+1];
	int								dsp_node_midi_off[MAX_INSTANCES+1];
	std::vector<int>				dsp_node_succ;
	std::vector<ADX_DSP_MIX>		dsp_node_mix;
	std::vector<ADX_DSP_MIDI_LINK>	dsp_node_midi;
	ADX_MIDI_CAPTURE*				dsp_midi_capture;
	float							dsp_instance_cost[MAX_INSTANCES];	// measured processing time per instance (microseconds, smoothed)
	bool							dsp_parallel;						// current block is processed in parallel
	float dsp_output_vumeter[NUM_DSP_OUTPUTS];
	float dsp_cpu_cost;
	int32_t  dsp_block_size;
	int32_t  dsp_highest_opin_count;

public:
	// master variables
	int	master_transport_sampleframe;
	int	master_transport_last_y;
	int	master_midi_vumeter[MAX_TRACKS];
	int	master_i_x;
	int	master_i_y;
	int	master_o_x;
	int	master_o_y;

public:
	ADX_PIN master_input_pin[NUM_DSP_INPUTS];

public:
	ADX_TIME_INFO	master_time_info;

public:
	char cfg_fil[_MAX_PATH];
	char hlp_fil[_MAX_PATH];

public:
	ADX_CONFIG		cfg;
	ADX_PROJECT		project;
	ADX_INSTANCE	instance[MAX_INSTANCES];
	ADX_VST_LIB		vst_lib;

public:
	int32_t instance_eff_id_currently_loading;

public:
	// event vectors
	ADX_EVENT seq_event[MAX_EVENTS];
	ADX_EVENT clipboard_event[MAX_EVENTS];
	ADX_EVENT undo_event[MAX_EVENTS];

public:
	// sequencer info
	int seq_num_events;

public:
	// clipboard info
	int clipboard_num_events;
	int clipboard_pos_sta;
	int clipboard_trk_sta;
	int clipboard_pos_len;
	int clipboard_trk_len;

public:
	// undo buffer info
	int undo_num_events;
	bool undo_combine;

public:
	// user variables
	int	user_page;
	int user_record;
	int user_quantize;
	int user_midi_ch;
	int	user_kbd_note_offset;
	int	user_kbd_velo;
	int user_midi_mask;
	int user_midi_learn;
	int	user_pat;
	int user_trk;
	int user_trk_offset;
	int	user_row;
	int user_instance;
	int user_instance_list_offset;
	int user_parameter;
	int user_parameter_list_offset;
	int user_lxm;
	int user_lym;
	int	user_edit_overwrite;
	int user_edit_step;
	int	user_block_pos_sta;
	int	user_block_trk_sta;
	int	user_block_pos_end;
	int	user_block_trk_end;
	int user_input_pin;
	int user_output_pin;
	int	user_edit_text_action_id;
	int user_rout_offset_x;
	int user_rout_offset_y;
	int user_pr_width;
	int user_pr_note_width;
	int user_pat_prev;

public:
	enum
	{
		PRESS_NONE,
		PRESS_CYCLE,
		PRESS_LIVE,
		PRESS_STOP_WRAP,
		PRESS_TEMPO,
		PRESS_PR_SEP,
		PRESS_CUE_STOP,
		PRESS_PLAY,
		PRESS_STOP,
		PRESS_REC,
		PRESS_AUTO,
		PRESS_STEP,
		PRESS_OVER,
		PRESS_PAGE,
		PRESS_SEQ_ZOOM,
		PRESS_SEQ_MODE,
		PRESS_VELOCITY,
		PRESS_PRE_RELEASE,
		PRESS_INST_PREV,
		PRESS_INST_NEXT,
		PRESS_PROG_PREV,
		PRESS_PROG_NEXT,
		PRESS_INST_SCROLL,
		PRESS_PARAM_SCROLL,
		PRESS_PARAM_TWEAK,
		PRESS_MIDI_WIRE,
		PRESS_PAT_PREV,
		PRESS_PAT_NEXT,
		PRESS_CUE_START,
		PRESS_CUE_END,
		PRESS_MARKER,
		PRESS_SEQ_DRAG,
		PRESS_EVENT_MOVE,
		PRESS_EVENT_SIZE,
		PRESS_BLOCK_MARK,
		PRESS_INST_MOVE,
		PRESS_AUDIO_WIRE,
		PRESS_MASTER_IN,
		PRESS_MASTER_OUT,
		PRESS_ROUTING_PAN,
		PRESS_PR_EVENT_MOVE,
		PRESS_PR_BLOCK_MARK,
		PRESS_RESERVED1,
		PRESS_RESERVED2,
		PRESS_WIRE_GAIN,
		PRESS_WIRE_DELETE,
	} user_pressed;

public:
	int user_event_drag;
	int	user_marker_drag;
	int user_drag_offset;

	int user_dragging_rout_instance_index;
	int user_dragging_rout_pin_index;

	// draggin wire
	ADX_WIRE* user_pressed_wire;
	int user_pressed_wire_x;
	int user_pressed_wire_y;

	// user 32-bit float register for track the vst parameter value
	float user_parameter_val;

public:
	char user_last_file[_MAX_PATH];
	char user_midi_in_monitor[9];

public:
	// when is set, one gui refresh is posted for next timer iteration
	int	gui_is_dirty;

public:
	// system cpu speed
	double d_system_clocks_per_sec;

public:
	// explain itself ;-)
	int	aodix_version;

public:
	// audio driver properties
	int32_t audio_num_drivers;
	int32_t audio_num_inputs;
	int32_t audio_num_outputs;
};
