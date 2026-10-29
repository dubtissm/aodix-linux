/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - Win32 compatibility layer: modal dialogs, message boxes and the compiled-in resources
// (the dialog templates and the main menu are transcribed from the original Aodix.rc)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "win32_internal.h"
#include "../aodix/resource.h"

#include <algorithm>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// resource templates
struct DlgItem
{
	const char*	cls;
	const char*	text;
	int			id;
	int			x,y,w,h;		// dialog units
	DWORD		style;
	DWORD		ex_style;
};

struct DlgTemplate
{
	int						id;
	const char*				caption;
	int						w,h;	// dialog units
	std::vector<DlgItem>	items;
};

#define S_GROUP		(WS_CHILD|WS_VISIBLE|BS_GROUPBOX)
#define S_CTEXT		(WS_CHILD|WS_VISIBLE|SS_CENTER)
#define S_EDIT		(WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL)
#define S_PUSH		(WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON)
#define S_DEFPUSH	(WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON)
#define S_CHECK		(WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX)
#define S_COMBO		(WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL)
#define S_LIST		(WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|LBS_NOINTEGRALHEIGHT)

static std::vector<DlgTemplate> g_templates=
{
	{
		IDD_ABOUTBOX,"Aodix - About",288,103,
		{
			{"BUTTON","",-1,4,0,280,100,S_GROUP,0},
			{"STATIC","Arguru Software Aodix",-1,8,8,272,12,S_CTEXT|SS_CENTERIMAGE,WS_EX_STATICEDGE},
			{"STATIC","By Juan Antonio Arguelles Rius && Zafer Kantar (up to v4.2.0.1)",-1,8,24,272,8,S_CTEXT|SS_CENTERIMAGE,0},
			{"STATIC","Graphics: Paul Merchant && Marc De Haar",-1,8,36,272,8,S_CTEXT|SS_CENTERIMAGE,0},
			{"STATIC","Additional development: J. van't Hoog (up to v4.2.0.3)",-1,8,48,272,8,S_CTEXT|SS_CENTERIMAGE,0},
			{"STATIC","Linux version && further development: Marcus Honnacker via Claude Opus 5.5",-1,8,60,272,8,S_CTEXT|SS_CENTERIMAGE,0},
			{"STATIC","",-1,133,76,20,20,WS_CHILD|WS_VISIBLE|SS_ICON,0},
			{"STATIC","",-1,8,72,272,1,WS_CHILD|WS_VISIBLE|SS_BLACKFRAME,WS_EX_STATICEDGE},
		}
	},
	{
		IDD_BOUNCE,"Aodix - Project Bounce",256,154,
		{
			{"BUTTON","",-1,4,0,248,56,S_GROUP,0},
			{"STATIC","&Output File (WAV 32-bit Float)",-1,8,8,240,12,S_CTEXT|SS_CENTERIMAGE,WS_EX_STATICEDGE},
			{"EDIT","",IDC_BOUNCE_EDIT1,8,24,220,12,S_EDIT,0},
			{"BUTTON","...",IDC_BOUNCE_BUTTON3,232,24,16,12,S_PUSH,0},
			{"BUTTON","&Start",IDC_BOUNCE_BUTTON1,8,40,64,12,S_PUSH,0},
			{"BUTTON","Stop",IDC_BOUNCE_BUTTON2,76,40,64,12,S_PUSH,0},
			{"STATIC","Ready",IDC_BOUNCE_STATUS1,144,40,104,12,S_CTEXT|SS_CENTERIMAGE,WS_EX_STATICEDGE},
			{"BUTTON","",-1,4,56,248,56,S_GROUP,0},
			{"STATIC","Options",-1,8,64,240,12,S_CTEXT|SS_CENTERIMAGE,WS_EX_STATICEDGE},
			{"BUTTON","&Render Cue Range",IDC_BOUNCE_CHECK1,8,80,240,12,S_CHECK,0},
			{"BUTTON","&Multifile",IDC_BOUNCE_CHECK2,8,96,240,12,S_CHECK,0},
			{"BUTTON","",-1,4,112,248,40,S_GROUP,0},
			{"STATIC","Rendering Progress",-1,8,120,240,12,S_CTEXT|SS_CENTERIMAGE,WS_EX_STATICEDGE},
			{"msctls_progress32","",IDC_BOUNCE_PROGRESS1,8,136,240,12,WS_CHILD|WS_VISIBLE|WS_BORDER,0},
		}
	},
	{
		// note: the "ASIO" group of the original dialog configures the JACK audio connection in the linux port
		IDD_CONFIG,"Aodix - Configuration",540,322,
		{
			{"BUTTON","",-1,4,0,264,40,S_GROUP,0},
			{"STATIC","User Information",-1,8,8,256,12,S_CTEXT|SS_CENTERIMAGE,WS_EX_STATICEDGE},
			{"STATIC","&User Name",-1,8,24,64,12,S_CTEXT|SS_CENTERIMAGE,0},
			{"EDIT","",IDC_USER_NAME_EDIT,72,24,192,12,S_EDIT,0},
			{"BUTTON","",-1,4,40,264,88,S_GROUP,0},
			{"STATIC","Audio (JACK / PipeWire)",-1,8,48,256,12,S_CTEXT|SS_CENTERIMAGE,WS_EX_STATICEDGE},
			{"STATIC","Current &Driver",-1,8,64,64,12,S_CTEXT|SS_CENTERIMAGE,0},
			{"COMBOBOX","",IDC_ASIO_DRIVER_COMBO,72,64,192,60,S_COMBO,0},
			{"STATIC","&Channels *",-1,8,80,64,12,S_CTEXT|SS_CENTERIMAGE,0},
			{"COMBOBOX","",IDC_ASIO_RATE_COMBO,72,80,128,188,S_COMBO,0},
			{"BUTTON","Co&ntrol Panel...",IDC_ASIO_CONTROL_PANEL,204,80,60,12,S_PUSH,0},
			{"STATIC","DSP &Threads",-1,8,96,64,12,S_CTEXT|SS_CENTERIMAGE,0},
			{"COMBOBOX","",IDC_DSP_THREADS_COMBO,72,96,192,188,S_COMBO,0},
			{"STATIC","&Buffer Size *",-1,8,112,64,12,S_CTEXT|SS_CENTERIMAGE,0},
			{"COMBOBOX","",IDC_PW_BUFFER_COMBO,72,112,88,188,S_COMBO,0},
			{"STATIC","Sample &Rate *",-1,160,112,48,12,S_CTEXT|SS_CENTERIMAGE,0},
			{"COMBOBOX","",IDC_PW_RATE_COMBO,208,112,56,188,S_COMBO,0},
			{"BUTTON","",-1,4,128,264,88,S_GROUP,0},
			{"STATIC","VST Plugins",-1,8,136,256,12,S_CTEXT|SS_CENTERIMAGE,WS_EX_STATICEDGE},
			{"STATIC","VST &Path 1",-1,8,152,64,12,S_CTEXT|SS_CENTERIMAGE,0},
			{"EDIT","",IDC_VST_PATH_EDIT1,72,152,172,12,S_EDIT,0},
			{"BUTTON","...",IDC_VST_PATH_BUTTON1,248,152,16,12,S_PUSH,0},
			{"STATIC","VST Path 2",-1,8,168,64,12,S_CTEXT|SS_CENTERIMAGE,0},
			{"EDIT","",IDC_VST_PATH_EDIT2,72,168,172,12,S_EDIT,0},
			{"BUTTON","...",IDC_VST_PATH_BUTTON2,248,168,16,12,S_PUSH,0},
			{"STATIC","VST Path 3",-1,8,184,64,12,S_CTEXT|SS_CENTERIMAGE,0},
			{"EDIT","",IDC_VST_PATH_EDIT3,72,184,172,12,S_EDIT,0},
			{"BUTTON","...",IDC_VST_PATH_BUTTON3,248,184,16,12,S_PUSH,0},
			{"STATIC","VST Path 4",-1,8,200,64,12,S_CTEXT|SS_CENTERIMAGE,0},
			{"EDIT","",IDC_VST_PATH_EDIT4,72,200,172,12,S_EDIT,0},
			{"BUTTON","...",IDC_VST_PATH_BUTTON4,248,200,16,12,S_PUSH,0},
			{"BUTTON","",-1,4,216,264,88,S_GROUP,0},
			{"STATIC","CLAP Plugins",-1,8,224,256,12,S_CTEXT|SS_CENTERIMAGE,WS_EX_STATICEDGE},
			{"STATIC","CLAP Path 1",-1,8,240,64,12,S_CTEXT|SS_CENTERIMAGE,0},
			{"EDIT","",IDC_CLAP_PATH_EDIT1,72,240,172,12,S_EDIT,0},
			{"BUTTON","...",IDC_CLAP_PATH_BUTTON1,248,240,16,12,S_PUSH,0},
			{"STATIC","CLAP Path 2",-1,8,256,64,12,S_CTEXT|SS_CENTERIMAGE,0},
			{"EDIT","",IDC_CLAP_PATH_EDIT2,72,256,172,12,S_EDIT,0},
			{"BUTTON","...",IDC_CLAP_PATH_BUTTON2,248,256,16,12,S_PUSH,0},
			{"STATIC","CLAP Path 3",-1,8,272,64,12,S_CTEXT|SS_CENTERIMAGE,0},
			{"EDIT","",IDC_CLAP_PATH_EDIT3,72,272,172,12,S_EDIT,0},
			{"BUTTON","...",IDC_CLAP_PATH_BUTTON3,248,272,16,12,S_PUSH,0},
			{"STATIC","CLAP Path 4",-1,8,288,64,12,S_CTEXT|SS_CENTERIMAGE,0},
			{"EDIT","",IDC_CLAP_PATH_EDIT4,72,288,172,12,S_EDIT,0},
			{"BUTTON","...",IDC_CLAP_PATH_BUTTON4,248,288,16,12,S_PUSH,0},
			{"BUTTON","",-1,272,0,264,104,S_GROUP,0},
			{"STATIC","User Interface",-1,276,8,256,12,S_CTEXT|SS_CENTERIMAGE,WS_EX_STATICEDGE},
			{"STATIC","&Keyboard Layout",-1,276,24,64,12,S_CTEXT|SS_CENTERIMAGE,0},
			{"COMBOBOX","",IDC_KBD_LAYOUT_COMBO,340,24,192,60,S_COMBO,0},
			{"STATIC","&Skin Bitmaps Path *",-1,276,40,64,12,S_CTEXT|SS_CENTERIMAGE,0},
			{"EDIT","",IDC_SKIN_PATH_EDIT,340,40,172,12,S_EDIT,0},
			{"BUTTON","...",IDC_SKIN_PATH_BUTTON,516,40,16,12,S_PUSH,0},
			{"STATIC","Color &Theme",-1,276,56,64,12,S_CTEXT|SS_CENTERIMAGE,0},
			{"EDIT","",IDC_THEME_PATH_EDIT,340,56,172,12,S_EDIT,0},
			{"BUTTON","...",IDC_THEME_PATH_BUTTON,516,56,16,12,S_PUSH,0},
			{"STATIC","&UI Scaling *",-1,276,72,64,12,S_CTEXT|SS_CENTERIMAGE,0},
			{"COMBOBOX","",IDC_UI_SCALE_COMBO,340,72,192,60,S_COMBO,0},
			{"BUTTON","&Instance To Master Autolink",IDC_AUTOLINK_CHECK,276,88,128,12,S_CHECK,0},
			{"BUTTON","Run In &Fullscreen Mode *",IDC_FULLSCREEN_CHECK,404,88,128,12,S_CHECK,0},
			{"BUTTON","",-1,272,104,264,200,S_GROUP,0},
			{"STATIC","MIDI Input",-1,276,112,256,12,S_CTEXT|SS_CENTERIMAGE,WS_EX_STATICEDGE},
			{"LISTBOX","",IDC_MIDI_IN_LIST,276,128,256,140,S_LIST,0},
			{"BUTTON","Open &MIDI In Devices",IDC_CHECK_MIDI_IN_OPEN,276,272,128,12,S_CHECK,0},
			{"BUTTON","Override &Channel Messages",IDC_CHECK_MIDI_CH_ROUT,404,272,128,12,S_CHECK,0},
			{"BUTTON","Override Note &Velocity Messages",IDC_CHECK_MIDI_VL_ROUT,276,288,128,12,S_CHECK,0},
			{"BUTTON","&Apply Changes",IDC_APPLY_BUTTON,4,308,128,12,S_DEFPUSH,0},
			{"BUTTON","Cancel",IDC_CANCEL_BUTTON,136,308,128,12,S_PUSH,0},
			{"STATIC","* Changes Will Take Effect After Restarting Aodix",-1,272,308,260,12,S_CTEXT|SS_CENTERIMAGE,0},
		}
	},
};

// dialog base units are derived from the dialog font exactly like windows does it
// (average width of the alphabet, font height), so the layouts of the original templates fit the text
static int dlg_base_x(void)
{
	static int bx=0;
	if(!bx)
		bx=(text_width(FONT_UI,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",52)/26+1)/2;
	return bx;
}

static int dlg_base_y(void)
{
	return font_height(FONT_UI);
}

static int dlu_x(int v){return (v*dlg_base_x()+2)/4;}
static int dlu_y(int v){return (v*dlg_base_y()+4)/8;}

HMENU load_menu_resource(int id)
{
	if(id!=IDR_AODIX)
		return nullptr;

	HMENU bar=CreateMenu();

	HMENU file=CreatePopupMenu();
	AppendMenu(file,MF_STRING,ID_FILE_NEW40001,"New...\tCtrl+N");
	AppendMenu(file,MF_SEPARATOR,0,nullptr);
	AppendMenu(file,MF_STRING,ID_FILE_OPEN40002,"Open...\tCtrl+O");
	AppendMenu(file,MF_STRING,ID_FILE_SAVE40025,"Save\tCtrl+S");
	AppendMenu(file,MF_STRING,ID_FILE_SAVEAS,"Save As...\tCtrl+Shift+S");
	AppendMenu(file,MF_SEPARATOR,0,nullptr);
	AppendMenu(file,MF_STRING,ID_FILE_BOUNCE40029,"Bounce...\tCtrl+W");
	AppendMenu(file,MF_SEPARATOR,0,nullptr);
	HMENU import=CreatePopupMenu();
	AppendMenu(import,MF_STRING,ID_FILE_IMPORTMIDIFILE,"MIDI File...");
	AppendMenu(import,MF_STRING|MF_GRAYED,ID_IMPORT_WAVEFILE,"WAVE File...");
	AppendMenu(file,MF_POPUP,(UINT_PTR)import,"Import");
	AppendMenu(file,MF_STRING|MF_GRAYED,ID_FILE_EXPORTMIDIFILE,"Export MIDI File...");
	AppendMenu(file,MF_SEPARATOR,0,nullptr);
	AppendMenu(file,MF_STRING,ID_FILE_CONFIGURATION,"Configuration...\tCtrl+,");
	AppendMenu(file,MF_SEPARATOR,0,nullptr);
	AppendMenu(file,MF_STRING,ID_FILE_EXIT,"Exit...");
	AppendMenu(file,MF_SEPARATOR,0,nullptr);
	AppendMenu(file,MF_STRING,ID_FILE_RECENTFILE1,"Recent File 1");
	AppendMenu(file,MF_STRING,ID_FILE_RECENTFILE2,"Recent File 2");
	AppendMenu(file,MF_STRING,ID_FILE_RECENTFILE3,"Recent File 3");
	AppendMenu(file,MF_STRING,ID_FILE_RECENTFILE4,"Recent File 4");
	AppendMenu(bar,MF_POPUP,(UINT_PTR)file,"&File");

	HMENU edit=CreatePopupMenu();
	AppendMenu(edit,MF_STRING,ID_EDIT_UNDOCLIPBOARDOPERATION,"Undo\tCtrl+Z");
	AppendMenu(edit,MF_SEPARATOR,0,nullptr);
	AppendMenu(edit,MF_STRING,ID_EDIT_CUT40012,"Cut\tCtrl+X");
	AppendMenu(edit,MF_STRING,ID_EDIT_COPY40013,"Copy\tCtrl+C");
	AppendMenu(edit,MF_STRING,ID_EDIT_PASTE40014,"Paste\tCtrl+V");
	AppendMenu(edit,MF_SEPARATOR,0,nullptr);
	AppendMenu(edit,MF_STRING,ID_EDIT_SELECTALL,"Select All\tCtrl+A");
	AppendMenu(edit,MF_SEPARATOR,0,nullptr);
	AppendMenu(edit,MF_STRING,ID_EDIT_QUANTIZENOTES,"Quantize Notes\tCtrl+Q");
	AppendMenu(edit,MF_STRING,ID_EDIT_RANDOMIZE,"Randomize Data\tCtrl+R");
	AppendMenu(edit,MF_STRING,ID_EDIT_INTERPOLATE,"Interpolate Data\tCtrl+I");
	AppendMenu(bar,MF_POPUP,(UINT_PTR)edit,"&Edit");

	HMENU help=CreatePopupMenu();
	AppendMenu(help,MF_STRING,ID_HELP_CONTENT,"Content...");
	AppendMenu(help,MF_STRING,ID_HELP_ABOUT,"About...");
	AppendMenu(help,MF_SEPARATOR,0,nullptr);
	AppendMenu(help,MF_STRING,ID_HELP_WEBSITE,"Development...");
	AppendMenu(bar,MF_POPUP,(UINT_PTR)help,"&Help");

	return bar;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// dialog helpers
static Win32Window* create_dialog_window(const char* caption,int w,int h,HWND parent,DLGPROC proc)
{
	Win32Window* owner=is_valid_window(parent)?top_level_of(parent):nullptr;
	HWND dlg=CreateWindowEx(WS_EX_TOOLWINDOW,"#32770",caption,WS_POPUP|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,0,w,h,owner,nullptr,nullptr,nullptr);
	if(!dlg)
		return nullptr;

	dlg->dlg=new DialogData();
	dlg->dlg->proc=proc;
	dlg->dlg->ended=false;
	dlg->dlg->result=0;
	dlg->dlg->default_id=IDOK;
	dlg->font=stock_font(FONT_UI);
	return dlg;
}

// position the dialog centered over its owner (DS_CENTER) and show it with a fixed size
void dialog_place_and_show(Win32Window* dlg);

static HWND first_tabstop(Win32Window* dlg)
{
	for(Win32Window* c:dlg->children)
		if((c->style & WS_TABSTOP) && c->enabled && c->visible)
			return c;
	return nullptr;
}

static INT_PTR run_modal(Win32Window* dlg)
{
	Win32Window* owner=dlg->owner;
	bool const owner_was_enabled=owner && owner->enabled;
	if(owner)
		EnableWindow(owner,FALSE);

	HWND const hdlg=dlg;

	while(is_valid_window(hdlg) && !hdlg->dlg->ended)
	{
		MSG m;
		if(!GetMessage(&m,nullptr,0,0))
		{
			PostQuitMessage((int)m.wParam);
			break;
		}

		// dialog keyboard interface (enter / escape / tab)
		if((m.message==WM_KEYDOWN || m.message==WM_CHAR) && is_valid_window(m.hwnd) && top_level_of(m.hwnd)==hdlg)
		{
			if(m.message==WM_KEYDOWN && dialog_handle_key(hdlg,(int)m.wParam,true))
				continue;
			if(m.message==WM_CHAR && (m.wParam==13 || m.wParam==27 || m.wParam==9))
				continue;
		}

		TranslateMessage(&m);
		DispatchMessage(&m);
	}

	INT_PTR result=0;
	if(is_valid_window(hdlg))
	{
		result=hdlg->dlg->result;
		DestroyWindow(hdlg);
	}

	if(owner && is_valid_window(owner))
	{
		EnableWindow(owner,owner_was_enabled);
		owner->invalid=true;
	}

	return result;
}

bool dialog_handle_key(Win32Window* top,int vk,bool down)
{
	if(!down || !top->dlg)
		return false;

	HWND focus=GetFocus();

	if(vk==VK_ESCAPE)
	{
		post_internal(top,WM_COMMAND,MAKEWPARAM(IDCANCEL,BN_CLICKED),0);
		return true;
	}

	if(vk==VK_RETURN)
	{
		// focused push button is clicked, otherwise the default button / IDOK
		if(focus && focus->parent==top && focus->ctl && focus->ctl->kind==CTL_BUTTON)
		{
			post_internal(top,WM_COMMAND,MAKEWPARAM(focus->id,BN_CLICKED),(LPARAM)focus);
			return true;
		}
		int id=IDOK;
		for(Win32Window* c:top->children)
			if((c->style & 0x0F)==BS_DEFPUSHBUTTON && c->cls && strcasecmp(c->cls->name.c_str(),"BUTTON")==0 && c->enabled)
				id=c->id;
		post_internal(top,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),0);
		return true;
	}

	if(vk==VK_TAB)
	{
		std::vector<Win32Window*> stops;
		for(Win32Window* c:top->children)
			if((c->style & WS_TABSTOP) && c->enabled && c->visible)
				stops.push_back(c);
		if(stops.empty())
			return true;
		int idx=-1;
		for(size_t i=0;i<stops.size();i++)
			if(stops[i]==focus)
				idx=(int)i;
		bool const back=GetKeyState(VK_SHIFT)<0;
		int const n=(int)stops.size();
		idx=back?((idx<=0)?n-1:idx-1):((idx+1)%n);
		SetFocus(stops[idx]);
		if(stops[idx]->ctl && stops[idx]->ctl->kind==CTL_EDIT)
			SendMessage(stops[idx],EM_SETSEL,0,-1);
		else if(stops[idx]->cls && strcasecmp(stops[idx]->cls->name.c_str(),"EDIT")==0)
			SendMessage(stops[idx],EM_SETSEL,0,-1);
		return true;
	}

	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
INT_PTR DialogBox(HINSTANCE,LPCSTR template_name,HWND parent,DLGPROC proc)
{
	int const id=(int)(uintptr_t)template_name;
	const DlgTemplate* t=nullptr;
	for(const DlgTemplate& d:g_templates)
		if(d.id==id)
			t=&d;
	if(!t)
	{
		fprintf(stderr,"aodix: DialogBox: unknown template %d\n",id);
		return -1;
	}

	Win32Window* dlg=create_dialog_window(t->caption,dlu_x(t->w),dlu_y(t->h),parent,proc);
	if(!dlg)
		return -1;

	for(const DlgItem& it:t->items)
	{
		int const x=dlu_x(it.x);
		int const y=dlu_y(it.y);
		int const w=dlu_x(it.w);
		int h=dlu_y(it.h);
		bool const is_combo=strcasecmp(it.cls,"COMBOBOX")==0;
		if(is_combo)
			h=dlu_y(12)+1;

		HWND c=CreateWindowEx(it.ex_style,it.cls,it.text,it.style,x,y,w,h,dlg,(HMENU)(intptr_t)it.id,nullptr,nullptr);
		if(c)
		{
			c->font=stock_font(FONT_UI);
			if(is_combo)
			{
				if(!c->ctl)
					c->ctl=new ControlData();
				c->ctl->range_max=std::max(4,(dlu_y(it.h)-h)/font_height(FONT_UI));
			}
			if((it.style & 0x0F)==BS_DEFPUSHBUTTON && strcasecmp(it.cls,"BUTTON")==0)
				dlg->dlg->default_id=it.id;
		}
	}

	if(proc(dlg,WM_INITDIALOG,0,0))
	{
		HWND const f=first_tabstop(dlg);
		if(f && (!GetFocus() || top_level_of(GetFocus())!=dlg))
		{
			SetFocus(f);
			if(f->cls && strcasecmp(f->cls->name.c_str(),"EDIT")==0)
				SendMessage(f,EM_SETSEL,0,-1);
		}
	}

	if(!is_valid_window(dlg))
		return -1;

	dialog_place_and_show(dlg);
	return run_modal(dlg);
}

BOOL EndDialog(HWND hdlg,INT_PTR result)
{
	if(!is_valid_window(hdlg) || !hdlg->dlg)
		return FALSE;
	hdlg->dlg->ended=true;
	hdlg->dlg->result=result;
	return TRUE;
}

HWND GetDlgItem(HWND hdlg,int id)
{
	if(!is_valid_window(hdlg))
		return nullptr;
	for(Win32Window* c:hdlg->children)
		if(c->id==id)
			return c;
	return nullptr;
}

BOOL SetDlgItemText(HWND hdlg,int id,LPCSTR text)
{
	HWND c=GetDlgItem(hdlg,id);
	if(!c)
		return FALSE;
	return SetWindowText(c,text);
}

UINT GetDlgItemText(HWND hdlg,int id,LPSTR text,int max_count)
{
	HWND c=GetDlgItem(hdlg,id);
	if(!c)
	{
		if(text && max_count>0)
			text[0]=0;
		return 0;
	}
	return (UINT)GetWindowText(c,text,max_count);
}

BOOL CheckDlgButton(HWND hdlg,int id,UINT check)
{
	HWND c=GetDlgItem(hdlg,id);
	if(!c)
		return FALSE;
	SendMessage(c,BM_SETCHECK,check,0);
	return TRUE;
}

UINT IsDlgButtonChecked(HWND hdlg,int id)
{
	HWND c=GetDlgItem(hdlg,id);
	if(!c)
		return 0;
	return (UINT)SendMessage(c,BM_GETCHECK,0,0);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// message box
struct MsgBoxInfo
{
	std::vector<std::string>	lines;
	UINT						type;
};


static LRESULT msgbox_icon_proc(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam)
{
	if(msg==WM_PAINT)
	{
		PAINTSTRUCT ps;
		HDC hdc=BeginPaint(hwnd,&ps);
		fill_rect(hdc,0,0,32,32,CLR_FACE);
		UINT const icon=hwnd->userdata & 0xF0;
		COLORREF fill=0x00C06000;
		const char* glyph="i";
		if(icon==MB_ICONERROR) { fill=0x000000D0; glyph="x"; }
		if(icon==MB_ICONQUESTION) { fill=0x00C06000; glyph="?"; }
		if(icon==MB_ICONWARNING) { fill=0x0000C0E0; glyph="!"; }

		if(icon==MB_ICONWARNING)
		{
			// triangle
			for(int y=0;y<26;y++)
			{
				int const half=y/2+1;
				fill_rect(hdc,16-half,3+y,half*2,1,fill);
			}
			RECT r={0,10,32,30};
			draw_text_in(hdc,r,glyph,FONT_UI_BOLD,0,1,true);
		}
		else
		{
			// circle
			for(int y=0;y<28;y++)
			{
				double const dy=y-13.5;
				int const half=(int)sqrt(std::max(0.0,14.0*14.0-dy*dy));
				fill_rect(hdc,16-half,2+y,half*2,1,fill);
			}
			RECT r={0,2,32,30};
			draw_text_in(hdc,r,glyph,FONT_UI_BOLD,0x00FFFFFF,1,true);
		}
		EndPaint(hwnd,&ps);
		return 0;
	}
	return DefWindowProc(hwnd,msg,wparam,lparam);
}

static INT_PTR msgbox_dlg_proc(HWND hdlg,UINT msg,WPARAM wparam,LPARAM)
{
	if(msg==WM_COMMAND)
	{
		int const id=LOWORD(wparam);
		// escape / close maps to cancel or no
		if(id==IDCANCEL)
		{
			UINT const t=hdlg->userdata & 0x0F;
			if(t==MB_YESNO)
				EndDialog(hdlg,IDNO);
			else if(t==MB_OK)
				EndDialog(hdlg,IDOK);
			else
				EndDialog(hdlg,IDCANCEL);
			return TRUE;
		}
		if(id==IDOK || id==IDYES || id==IDNO)
		{
			EndDialog(hdlg,id);
			return TRUE;
		}
	}
	return FALSE;
}

int MessageBox(HWND hwnd,LPCSTR text,LPCSTR caption,UINT type)
{
	static bool icon_class_registered=false;
	if(!icon_class_registered)
	{
		WNDCLASS wc;
		memset(&wc,0,sizeof(wc));
		wc.lpfnWndProc=msgbox_icon_proc;
		wc.lpszClassName="#msgboxicon";
		RegisterClass(&wc);
		icon_class_registered=true;
	}

	// always print to the console as well (useful when no display or for bug reports)
	fprintf(stderr,"aodix: [%s] %s\n",caption?caption:"",text?text:"");

	MsgBoxInfo info;
	info.type=type;
	std::string s=text?text:"";
	size_t p=0;
	for(;;)
	{
		size_t const nl=s.find('\n',p);
		info.lines.push_back(s.substr(p,nl==std::string::npos?std::string::npos:nl-p));
		if(nl==std::string::npos)
			break;
		p=nl+1;
	}

	int text_w=0;
	for(const std::string& l:info.lines)
		text_w=std::max(text_w,text_width(FONT_UI,l.c_str(),-1));
	int const line_h=font_height(FONT_UI);
	int const text_h=line_h*(int)info.lines.size();
	bool const has_icon=(type & 0xF0)!=0;

	UINT const btype=type & 0x0F;
	std::vector<std::pair<int,const char*>> buttons;
	if(btype==MB_YESNO) buttons={{IDYES,"&Yes"},{IDNO,"&No"}};
	else if(btype==MB_OKCANCEL) buttons={{IDOK,"OK"},{IDCANCEL,"Cancel"}};
	else if(btype==MB_YESNOCANCEL) buttons={{IDYES,"&Yes"},{IDNO,"&No"},{IDCANCEL,"Cancel"}};
	else buttons={{IDOK,"OK"}};

	int const bw=75,bh=23,bgap=6;
	int const buttons_w=(int)buttons.size()*bw+((int)buttons.size()-1)*bgap;
	int const icon_w=has_icon?32+12:0;
	int const w=std::max(buttons_w+24,icon_w+text_w+36);
	int const content_h=std::max(text_h,has_icon?32:0);
	int const h=12+content_h+16+bh+12;

	Win32Window* owner=is_valid_window(hwnd)?top_level_of(hwnd):nullptr;
	Win32Window* dlg=create_dialog_window(caption?caption:"",w,h,owner,msgbox_dlg_proc);
	if(!dlg)
		return IDOK;
	dlg->userdata=type;

	if(has_icon)
	{
		HWND ic=CreateWindowEx(0,"#msgboxicon","",WS_CHILD|WS_VISIBLE,12,12,32,32,dlg,nullptr,nullptr,nullptr);
		if(ic)
			ic->userdata=type;
	}

	int const tx=12+icon_w;
	int const ty=12+(content_h-text_h)/2;
	for(size_t i=0;i<info.lines.size();i++)
	{
		HWND l=CreateWindowEx(0,"STATIC",info.lines[i].c_str(),WS_CHILD|WS_VISIBLE|SS_LEFT,tx,ty+(int)i*line_h,w-tx-8,line_h,dlg,nullptr,nullptr,nullptr);
		if(l)
			l->font=stock_font(FONT_UI);
	}

	int bx=(w-buttons_w)/2;
	HWND first=nullptr;
	for(auto& b:buttons)
	{
		HWND bt=CreateWindowEx(0,"BUTTON",b.second,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,bx,h-12-bh,bw,bh,dlg,(HMENU)(intptr_t)b.first,nullptr,nullptr);
		if(bt)
		{
			bt->font=stock_font(FONT_UI);
			if(!first)
				first=bt;
		}
		bx+=bw+bgap;
	}

	if(first)
	{
		SetFocus(first);
		dlg->dlg->default_id=first->id;
	}

	dialog_place_and_show(dlg);
	INT_PTR const r=run_modal(dlg);
	return r?(int)r:((btype==MB_YESNO)?IDNO:IDOK);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// built-in file / folder chooser (used when neither zenity nor kdialog is installed)
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fnmatch.h>
#include <limits.h>

struct FileDlgState
{
	std::string									dir;
	std::vector<std::pair<std::string,std::string>> filters;
	int											filter_index;
	bool										save;
	bool										folder;
	std::string									default_ext;
	std::vector<std::pair<std::string,bool>>	entries;	// name, is_dir
	std::string									result;
};

#define FD_PATH		100
#define FD_LIST		101
#define FD_NAME		102
#define FD_FILTER	103

static bool fd_match(const FileDlgState* st,const std::string& name)
{
	if(st->filters.empty())
		return true;
	std::string const pats=st->filters[st->filter_index].second;
	size_t s=0;
	while(s<=pats.size())
	{
		size_t const e=pats.find(';',s);
		std::string const p=pats.substr(s,e==std::string::npos?std::string::npos:e-s);
		if(!p.empty() && fnmatch(p.c_str(),name.c_str(),FNM_CASEFOLD)==0)
			return true;
		if(e==std::string::npos)
			break;
		s=e+1;
	}
	return false;
}

static void fd_fill(HWND hdlg,FileDlgState* st)
{
	st->entries.clear();
	DIR* d=opendir(st->dir.c_str());
	std::vector<std::pair<std::string,bool>> dirs,files;
	if(d)
	{
		while(struct dirent* e=readdir(d))
		{
			std::string const n=e->d_name;
			if(n=="." || n=="..")
				continue;
			if(n[0]=='.')
				continue;
			struct stat sb;
			std::string const full=st->dir+"/"+n;
			bool const is_dir=(stat(full.c_str(),&sb)==0 && S_ISDIR(sb.st_mode));
			if(is_dir)
				dirs.push_back({n,true});
			else if(!st->folder && fd_match(st,n))
				files.push_back({n,false});
		}
		closedir(d);
	}
	std::sort(dirs.begin(),dirs.end(),[](const std::pair<std::string,bool>& a,const std::pair<std::string,bool>& b){return strcasecmp(a.first.c_str(),b.first.c_str())<0;});
	std::sort(files.begin(),files.end(),[](const std::pair<std::string,bool>& a,const std::pair<std::string,bool>& b){return strcasecmp(a.first.c_str(),b.first.c_str())<0;});

	if(st->dir!="/")
		st->entries.push_back({"..",true});
	for(auto& x:dirs) st->entries.push_back(x);
	for(auto& x:files) st->entries.push_back(x);

	HWND list=GetDlgItem(hdlg,FD_LIST);
	SendMessage(list,LB_RESETCONTENT,0,0);
	for(auto& e:st->entries)
	{
		std::string const label=e.second?("["+e.first+"]"):e.first;
		SendMessage(list,LB_ADDSTRING,0,(LPARAM)label.c_str());
	}
	SetDlgItemText(hdlg,FD_PATH,st->dir.c_str());
}

static std::string fd_normalize(const std::string& p)
{
	char buf[PATH_MAX];
	if(realpath(p.c_str(),buf))
		return buf;
	return p;
}

static void fd_enter(HWND hdlg,FileDlgState* st,const std::string& name)
{
	if(name=="..")
	{
		size_t const s=st->dir.find_last_of('/');
		st->dir=(s==0 || s==std::string::npos)?"/":st->dir.substr(0,s);
	}
	else
		st->dir=fd_normalize(st->dir=="/"?"/"+name:st->dir+"/"+name);
	fd_fill(hdlg,st);
}

static bool fd_accept(HWND hdlg,FileDlgState* st)
{
	char buf[PATH_MAX];
	GetDlgItemText(hdlg,FD_NAME,buf,sizeof(buf));
	std::string name=buf;

	if(name.empty())
	{
		if(st->folder)
		{
			st->result=st->dir;
			return true;
		}
		return false;
	}

	if(name[0]=='~')
	{
		const char* home=getenv("HOME");
		name=std::string(home?home:"")+name.substr(1);
	}
	std::string const full=(name[0]=='/')?name:(st->dir=="/"?"/"+name:st->dir+"/"+name);

	struct stat sb;
	bool const exists=stat(full.c_str(),&sb)==0;

	if(exists && S_ISDIR(sb.st_mode))
	{
		if(st->folder)
		{
			st->result=fd_normalize(full);
			return true;
		}
		st->dir=fd_normalize(full);
		SetDlgItemText(hdlg,FD_NAME,"");
		fd_fill(hdlg,st);
		return false;
	}

	if(st->folder)
		return false;

	std::string res=full;
	if(st->save && !st->default_ext.empty())
	{
		size_t const slash=res.find_last_of('/');
		if(res.find('.',slash==std::string::npos?0:slash)==std::string::npos)
			res+="."+st->default_ext;
	}

	if(!st->save && !exists)
	{
		MessageBox(hdlg,"File not found.","Aodix",MB_OK|MB_ICONWARNING);
		return false;
	}

	if(st->save && stat(res.c_str(),&sb)==0)
	{
		std::string const q=res+"\nalready exists. Do you want to replace it?";
		if(MessageBox(hdlg,q.c_str(),"Aodix",MB_YESNO|MB_ICONWARNING)!=IDYES)
			return false;
	}

	st->result=res;
	return true;
}

static INT_PTR file_dlg_proc(HWND hdlg,UINT msg,WPARAM wparam,LPARAM)
{
	FileDlgState* st=(FileDlgState*)hdlg->userdata;
	if(msg!=WM_COMMAND || !st)
		return FALSE;

	int const id=LOWORD(wparam);
	int const code=HIWORD(wparam);
	HWND list=GetDlgItem(hdlg,FD_LIST);

	if(id==FD_LIST)
	{
		int const sel=(int)SendMessage(list,LB_GETCURSEL,0,0);
		if(sel<0 || sel>=(int)st->entries.size())
			return TRUE;
		auto const& e=st->entries[sel];
		if(code==LBN_SELCHANGE && !e.second)
			SetDlgItemText(hdlg,FD_NAME,e.first.c_str());
		if(code==LBN_DBLCLK)
		{
			if(e.second)
				fd_enter(hdlg,st,e.first);
			else if(fd_accept(hdlg,st))
				EndDialog(hdlg,IDOK);
		}
		return TRUE;
	}

	if(id==FD_FILTER && code==CBN_SELCHANGE)
	{
		st->filter_index=std::max(0,(int)SendMessage(GetDlgItem(hdlg,FD_FILTER),CB_GETCURSEL,0,0));
		fd_fill(hdlg,st);
		return TRUE;
	}

	if(id==IDOK)
	{
		// a selected directory in the list is entered first (or accepted in folder mode)
		int const sel=(int)SendMessage(list,LB_GETCURSEL,0,0);
		char buf[PATH_MAX];
		GetDlgItemText(hdlg,FD_NAME,buf,sizeof(buf));
		if(GetFocus()==list && sel>=0 && sel<(int)st->entries.size() && st->entries[sel].second)
		{
			if(st->folder && st->entries[sel].first!="..")
			{
				st->result=fd_normalize(st->dir+"/"+st->entries[sel].first);
				EndDialog(hdlg,IDOK);
			}
			else
				fd_enter(hdlg,st,st->entries[sel].first);
			return TRUE;
		}
		if(fd_accept(hdlg,st))
			EndDialog(hdlg,IDOK);
		return TRUE;
	}

	if(id==IDCANCEL)
	{
		EndDialog(hdlg,IDCANCEL);
		return TRUE;
	}

	return FALSE;
}

bool builtin_file_dialog(HWND owner,const char* title,char* path,int path_size,const std::vector<std::pair<std::string,std::string>>& filters,bool save,bool folder,const char* default_ext)
{
	FileDlgState st;
	st.filters=filters;
	st.filter_index=0;
	st.save=save;
	st.folder=folder;
	st.default_ext=default_ext?default_ext:"";

	// start directory / name from the passed path
	std::string p=path?path:"";
	std::string name;
	struct stat sb;
	if(!p.empty() && stat(p.c_str(),&sb)==0 && S_ISDIR(sb.st_mode))
		st.dir=fd_normalize(p);
	else if(!p.empty() && p.find('/')!=std::string::npos)
	{
		size_t const s=p.find_last_of('/');
		st.dir=fd_normalize(s==0?"/":p.substr(0,s));
		name=p.substr(s+1);
	}
	else
	{
		char cwd[PATH_MAX];
		st.dir=getcwd(cwd,sizeof(cwd))?cwd:"/";
		name=p;
	}
	if(stat(st.dir.c_str(),&sb)!=0)
		st.dir=getenv("HOME")?getenv("HOME"):"/";
	if(name.find('*')!=std::string::npos)
		name.clear();

	int const w=480,h=348;
	Win32Window* dlg=create_dialog_window(title?title:(folder?"Select Directory":"Open"),w,h,owner,file_dlg_proc);
	if(!dlg)
		return false;
	dlg->userdata=(LONG_PTR)&st;

	auto mk=[&](const char* cls,const char* text,int id,int x,int y,int cw,int ch,DWORD style,DWORD ex)->HWND
	{
		HWND c=CreateWindowEx(ex,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,cw,ch,dlg,(HMENU)(intptr_t)id,nullptr,nullptr);
		if(c)
			c->font=stock_font(FONT_UI);
		return c;
	};

	mk("STATIC","",FD_PATH,8,8,w-16,20,SS_LEFT|SS_CENTERIMAGE,WS_EX_STATICEDGE);
	mk("LISTBOX","",FD_LIST,8,34,w-16,240,WS_TABSTOP|WS_VSCROLL,0);
	mk("STATIC",folder?"Folder:":"File name:",-1,8,284,70,21,SS_LEFT|SS_CENTERIMAGE,0);
	mk("EDIT",name.c_str(),FD_NAME,80,284,w-88-170,21,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,0);
	HWND filter=nullptr;
	if(!folder)
	{
		mk("STATIC","Type:",-1,8,312,70,21,SS_LEFT|SS_CENTERIMAGE,0);
		filter=mk("COMBOBOX","",FD_FILTER,80,312,w-88-170,21,WS_TABSTOP|CBS_DROPDOWNLIST,0);
		for(auto& f:filters)
			SendMessage(filter,CB_ADDSTRING,0,(LPARAM)f.first.c_str());
		if(filters.empty())
			SendMessage(filter,CB_ADDSTRING,0,(LPARAM)"All Files");
		SendMessage(filter,CB_SETCURSEL,0,0);
	}
	mk("BUTTON",save?"&Save":(folder?"&Select":"&Open"),IDOK,w-8-150,284,150,23,WS_TABSTOP|BS_DEFPUSHBUTTON,0);
	mk("BUTTON","Cancel",IDCANCEL,w-8-150,312,150,23,WS_TABSTOP|BS_PUSHBUTTON,0);
	dlg->dlg->default_id=IDOK;

	fd_fill(dlg,&st);
	HWND const ed=GetDlgItem(dlg,FD_NAME);
	SetFocus(ed);
	SendMessage(ed,EM_SETSEL,0,-1);

	dialog_place_and_show(dlg);
	INT_PTR const r=run_modal(dlg);

	if(r==IDOK && !st.result.empty())
	{
		strncpy(path,st.result.c_str(),path_size-1);
		path[path_size-1]=0;
		return true;
	}
	return false;
}
