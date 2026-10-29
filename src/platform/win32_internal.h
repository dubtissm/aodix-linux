/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - Win32 compatibility layer, internal definitions shared by the implementation files
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "win32compat.h"

#include <string>
#include <vector>
#include <deque>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// internal pixel format: 0x00RRGGBB (matches 24/32-bit TrueColor X11 visuals on little endian machines)
inline uint32_t cr_to_px(COLORREF c){return ((c&0xFF)<<16)|(c&0xFF00)|((c>>16)&0xFF);}
inline COLORREF px_to_cr(uint32_t p){return ((p>>16)&0xFF)|(p&0xFF00)|((p&0xFF)<<16);}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct DeviceContext
{
	GdiBitmap*		bitmap;			// drawing surface
	GdiBitmap*		default_bitmap;	// stock 1x1 bitmap of memory dcs
	Win32Window*	window;			// window dc owner (null for memory dc)
	bool			is_memory;
	int				ox;				// logical origin inside the surface
	int				oy;
	RECT			base_clip;		// surface coordinates
	bool			has_user_clip;
	RECT			user_clip;		// logical coordinates
	COLORREF		text_color;
	COLORREF		bk_color;
	int				bk_mode;
	GdiPen*			pen;
	GdiBrush*		brush;
	GdiFont*		font;
	int				cur_x;
	int				cur_y;
};

// compute effective clip rectangle in surface coordinates
RECT		dc_clip(DeviceContext* dc);
void		dc_mark_dirty(DeviceContext* dc,int x1,int y1,int x2,int y2);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// menus
struct MenuItem
{
	std::string	text;
	UINT		id;
	UINT		type;		// MFT_* flags (separator, menubarbreak, radiocheck)
	UINT		state;		// MFS_* flags (checked, grayed, default)
	HMENU		submenu;
};

struct Win32Menu
{
	uint32_t				magic;
	bool					is_bar;
	std::vector<MenuItem>	items;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// cursors
struct Win32Cursor
{
	unsigned long	xcursor;
	bool			stock;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// window classes
struct WindowClass
{
	std::string	name;
	WNDPROC		proc;
	UINT		style;
	HCURSOR		cursor;
	HBRUSH		background;
	int			menu_id;
	HINSTANCE	hinstance;
};

WindowClass* find_class(const char* name);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// per-control data for the built-in controls (edit, button, static, combobox, listbox, progress)
struct ControlData
{
	int							kind;
	// edit
	int							sel_start;
	int							sel_end;
	int							caret;
	int							scroll;
	// buttons
	int							check;
	bool						pressed;
	bool						hot;
	// lists
	std::vector<std::string>	items;
	int							cur_sel;
	int							top_index;
	// progress
	int							range_min;
	int							range_max;
	int							pos;
	COLORREF					bar_color;
	COLORREF					bk_color;
	ControlData():kind(0),sel_start(0),sel_end(0),caret(0),scroll(0),check(0),pressed(false),hot(false),cur_sel(-1),top_index(0),range_min(0),range_max(100),pos(0),bar_color(0x00C08000),bk_color(0x00FFFFFF){}
};

enum { CTL_NONE=0, CTL_EDIT, CTL_BUTTON, CTL_CHECKBOX, CTL_GROUPBOX, CTL_STATIC, CTL_COMBOBOX, CTL_LISTBOX, CTL_PROGRESS, CTL_ICON };

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct DialogData
{
	DLGPROC		proc;
	bool		ended;
	INT_PTR		result;
	int			default_id;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct Win32Window
{
	uint32_t					magic;
	WindowClass*				cls;
	WNDPROC						proc;
	DWORD						style;
	DWORD						ex_style;
	std::string					text;
	Win32Window*				parent;		// child windows
	Win32Window*				owner;		// top level windows (dialogs, popups)
	std::vector<Win32Window*>	children;
	int							id;
	LONG_PTR					userdata;
	HINSTANCE					hinstance;
	RECT						rect;		// child: rect in parent client coords; top level: client size in rect.right/bottom
	bool						visible;
	bool						enabled;
	bool						destroyed;
	HMENU						menu;		// menu bar
	int							menubar_h;
	int							menubar_hot;
	HFONT						font;
	HCURSOR						cursor;
	// invalidation
	bool						invalid;
	// top level only (backed by an X window)
	unsigned long				xwin;
	void*						xic;
	GdiBitmap*					surface;
	void*						ximage;
	void*						shminfo;
	bool						has_dirty;
	RECT						dirty;
	int							root_x;		// last known root position of the surface origin
	int							root_y;
	bool						root_valid;
	bool						is_popup;	// override redirect popup (menus, dropdowns)
	bool						mapped;
	bool						want_fullscreen;
	bool						want_maximized;
	bool						track_placement;	// linux port: remember the normal (not maximized) geometry (main window)
	bool						have_normal;
	int							normal_x,normal_y,normal_w,normal_h;	// frame position, client size without menu bar (physical px)
	unsigned long				xcursor_current;
	// built-in controls and dialogs
	ControlData*				ctl;
	DialogData*					dlg;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// window helpers (win32_window.cpp)
bool			is_valid_window(HWND hwnd);
Win32Window*	top_level_of(Win32Window* w);
void			client_origin_in_surface(Win32Window* w,int* x,int* y);	// origin of client area inside the top level surface
void			post_internal(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam);
void			invalidate_children(Win32Window* w);
void			repaint_menubar(Win32Window* w);
void			repaint_all_windows(void);
int				menubar_height(HMENU menu);
void			flush_all_surfaces(void);
bool			pump_one(bool block);	// process pending X events / timers; returns false when nothing happened (non-blocking)
Win32Window*	create_popup_surface(Win32Window* owner,int x,int y,int w,int h);
void			destroy_popup_surface(Win32Window* w);
void			move_popup_surface(Win32Window* w,int x,int y,int width,int height);
void			screen_size(int* w,int* h);
DWORD			now_ms(void);

// input hook used by modal menu tracking: returns true if the event was consumed
typedef bool (*input_hook_fn)(void* xevent,void* user);
void			set_input_hook(input_hook_fn fn,void* user);
int				x11_keysym_to_vk(unsigned long keysym);
bool			grab_input(Win32Window* popup);
void			ungrab_input(void);

// device context creation (win32_gdi.cpp)
DeviceContext*	create_window_dc(Win32Window* w);
DeviceContext*	create_surface_dc(Win32Window* top);
void			destroy_dc(DeviceContext* dc);

// text rendering (win32_gdi.cpp)
int				text_width(int font_kind,const char* text,int count);
int				font_height(int font_kind);
int				font_ascent(int font_kind);
void			draw_text_surface(DeviceContext* dc,int x,int y,const char* text,int count,int font_kind,COLORREF color);
GdiFont*		stock_font(int kind);

// simple drawing helpers working in logical dc coordinates (win32_gdi.cpp)
void			fill_rect(HDC hdc,int x,int y,int w,int h,COLORREF color);
void			frame_3d(HDC hdc,int x,int y,int w,int h,bool sunken,bool thick);
void			draw_text_in(HDC hdc,const RECT& r,const char* text,int font_kind,COLORREF color,int align,bool vcenter);	// align: 0 left, 1 center, 2 right
void			draw_focus_rect(HDC hdc,int x,int y,int w,int h);

// classic windows colors used by menus and dialogs (changeable with adxplat::set_ui_colors)
extern adxplat::UiColors g_ui_colors;
#define CLR_FACE		(g_ui_colors.face)
#define CLR_HILIGHT		(g_ui_colors.hilight)
#define CLR_LIGHT		(g_ui_colors.light)
#define CLR_SHADOW		(g_ui_colors.shadow)
#define CLR_DKSHADOW	(g_ui_colors.dkshadow)
#define CLR_WINDOW		(g_ui_colors.window)
#define CLR_TEXT		(g_ui_colors.text)
#define CLR_GRAYTEXT	(g_ui_colors.graytext)
#define CLR_SEL			(g_ui_colors.sel)
#define CLR_SELTEXT		(g_ui_colors.seltext)

// built-in control classes (win32_controls.cpp)
void			register_builtin_classes(void);

// menus (win32_menu.cpp)
void			menubar_paint(Win32Window* w);
void			menubar_click(Win32Window* w,int x,int y);
int				menubar_hit(Win32Window* w,int x,int y);
UINT			track_popup(HMENU hmenu,int x,int y,HWND owner,int menubar_index);

// dialogs (win32_dialog.cpp)
bool			dialog_handle_key(Win32Window* top,int vk,bool down);
