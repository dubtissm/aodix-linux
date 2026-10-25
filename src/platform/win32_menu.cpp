/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - Win32 compatibility layer: menus (menu bar and popup menus, classic windows look)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "win32_internal.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <algorithm>
#include <unordered_set>

#define MENU_MAGIC 0x4D454E55

static std::unordered_set<Win32Menu*> g_menus;

static bool valid_menu(HMENU m)
{
	return m && g_menus.count(m) && m->magic==MENU_MAGIC;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
HMENU CreateMenu(void)
{
	Win32Menu* m=new Win32Menu();
	m->magic=MENU_MAGIC;
	m->is_bar=true;
	g_menus.insert(m);
	return m;
}

HMENU CreatePopupMenu(void)
{
	Win32Menu* m=new Win32Menu();
	m->magic=MENU_MAGIC;
	m->is_bar=false;
	g_menus.insert(m);
	return m;
}

BOOL DestroyMenu(HMENU hmenu)
{
	if(!valid_menu(hmenu))
		return FALSE;
	for(MenuItem& it:hmenu->items)
		if(it.submenu)
			DestroyMenu(it.submenu);
	hmenu->magic=0;
	g_menus.erase(hmenu);
	delete hmenu;
	return TRUE;
}

int GetMenuItemCount(HMENU hmenu)
{
	return valid_menu(hmenu)?(int)hmenu->items.size():-1;
}

HMENU GetSubMenu(HMENU hmenu,int pos)
{
	if(!valid_menu(hmenu) || pos<0 || pos>=(int)hmenu->items.size())
		return nullptr;
	return hmenu->items[pos].submenu;
}

// find item by position or by command id (recursively)
static MenuItem* find_item(HMENU hmenu,UINT item,bool by_position)
{
	if(!valid_menu(hmenu))
		return nullptr;

	if(by_position)
	{
		if(item>=hmenu->items.size())
			return nullptr;
		return &hmenu->items[item];
	}

	for(MenuItem& it:hmenu->items)
	{
		if(it.submenu)
		{
			MenuItem* r=find_item(it.submenu,item,false);
			if(r)
				return r;
		}
		else if(it.id==item && !(it.type & MFT_SEPARATOR))
			return &it;
	}
	return nullptr;
}

BOOL InsertMenuItem(HMENU hmenu,UINT item,BOOL by_position,const MENUITEMINFO* mii)
{
	if(!valid_menu(hmenu) || !mii)
		return FALSE;

	MenuItem it;
	it.id=(mii->fMask & MIIM_ID)?mii->wID:0;
	it.type=(mii->fMask & (MIIM_TYPE|MIIM_FTYPE))?mii->fType:0;
	it.state=(mii->fMask & MIIM_STATE)?mii->fState:0;
	it.submenu=(mii->fMask & MIIM_SUBMENU)?mii->hSubMenu:nullptr;
	if((mii->fMask & (MIIM_TYPE|MIIM_STRING)) && !(it.type & MFT_SEPARATOR) && mii->dwTypeData)
		it.text=mii->dwTypeData;

	size_t pos=hmenu->items.size();
	if(by_position)
		pos=std::min<size_t>(item,hmenu->items.size());
	else
	{
		for(size_t i=0;i<hmenu->items.size();i++)
		{
			if(hmenu->items[i].id==item && !hmenu->items[i].submenu && !(hmenu->items[i].type & MFT_SEPARATOR))
			{
				pos=i;
				break;
			}
		}
	}

	hmenu->items.insert(hmenu->items.begin()+pos,it);
	return TRUE;
}

BOOL AppendMenu(HMENU hmenu,UINT flags,UINT_PTR id_or_submenu,LPCSTR text)
{
	if(!valid_menu(hmenu))
		return FALSE;

	MenuItem it;
	it.type=flags & (MF_SEPARATOR|MF_MENUBARBREAK|MF_MENUBREAK);
	it.state=flags & (MF_GRAYED|MF_DISABLED|MF_CHECKED);
	it.submenu=(flags & MF_POPUP)?(HMENU)id_or_submenu:nullptr;
	it.id=(flags & MF_POPUP)?0:(UINT)id_or_submenu;
	if(text && !(flags & MF_SEPARATOR))
		it.text=text;
	hmenu->items.push_back(it);
	return TRUE;
}

BOOL ModifyMenu(HMENU hmenu,UINT position,UINT flags,UINT_PTR id_or_submenu,LPCSTR text)
{
	MenuItem* it=find_item(hmenu,position,(flags & MF_BYPOSITION)!=0);
	if(!it)
		return FALSE;

	it->type=flags & (MF_SEPARATOR|MF_MENUBARBREAK|MF_MENUBREAK);
	it->state=(it->state & MFS_DEFAULT)|(flags & (MF_GRAYED|MF_DISABLED|MF_CHECKED));
	if(flags & MF_POPUP)
	{
		it->submenu=(HMENU)id_or_submenu;
		it->id=0;
	}
	else
	{
		it->id=(UINT)id_or_submenu;
	}
	it->text=(text && !(flags & MF_SEPARATOR))?text:"";
	return TRUE;
}

DWORD CheckMenuItem(HMENU hmenu,UINT item,UINT check)
{
	MenuItem* it=find_item(hmenu,item,(check & MF_BYPOSITION)!=0);
	if(!it)
		return (DWORD)-1;
	DWORD const old=(it->state & MFS_CHECKED)?MF_CHECKED:MF_UNCHECKED;
	if(check & MF_CHECKED)
		it->state|=MFS_CHECKED;
	else
		it->state&=~MFS_CHECKED;
	it->type&=~MFT_RADIOCHECK;
	return old;
}

BOOL CheckMenuRadioItem(HMENU hmenu,UINT first,UINT last,UINT check,UINT flags)
{
	if(!valid_menu(hmenu))
		return FALSE;

	if(flags & MF_BYPOSITION)
	{
		for(UINT i=first;i<=last && i<hmenu->items.size();i++)
		{
			MenuItem& it=hmenu->items[i];
			if(i==check)
			{
				it.state|=MFS_CHECKED;
				it.type|=MFT_RADIOCHECK;
			}
			else
			{
				it.state&=~MFS_CHECKED;
			}
		}
		return TRUE;
	}

	for(MenuItem& it:hmenu->items)
	{
		if(it.id>=first && it.id<=last)
		{
			if(it.id==check)
			{
				it.state|=MFS_CHECKED;
				it.type|=MFT_RADIOCHECK;
			}
			else
				it.state&=~MFS_CHECKED;
		}
	}
	return TRUE;
}

BOOL EnableMenuItem(HMENU hmenu,UINT item,UINT enable)
{
	MenuItem* it=find_item(hmenu,item,(enable & MF_BYPOSITION)!=0);
	if(!it)
		return -1;
	it->state&=~(MF_GRAYED|MF_DISABLED);
	it->state|=enable & (MF_GRAYED|MF_DISABLED);
	return TRUE;
}

BOOL SetMenuItemInfo(HMENU hmenu,UINT item,BOOL by_position,const MENUITEMINFO* mii)
{
	MenuItem* it=find_item(hmenu,item,by_position!=0);
	if(!it || !mii)
		return FALSE;
	if(mii->fMask & MIIM_STATE)
		it->state=mii->fState & ~MFS_HILITE;
	if(mii->fMask & MIIM_ID)
		it->id=mii->wID;
	if(mii->fMask & MIIM_SUBMENU)
		it->submenu=mii->hSubMenu;
	if(mii->fMask & (MIIM_TYPE|MIIM_FTYPE))
		it->type=mii->fType;
	if((mii->fMask & (MIIM_TYPE|MIIM_STRING)) && mii->dwTypeData)
		it->text=mii->dwTypeData;
	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// menu text helpers: "Label\tAccel", '&' marks the mnemonic
static void split_text(const std::string& t,std::string& label,std::string& accel)
{
	size_t const tab=t.find('\t');
	if(tab==std::string::npos)
	{
		label=t;
		accel.clear();
	}
	else
	{
		label=t.substr(0,tab);
		accel=t.substr(tab+1);
	}
}

static std::string strip_amp(const std::string& s)
{
	std::string o;
	for(size_t i=0;i<s.size();i++)
	{
		if(s[i]=='&')
		{
			if(i+1<s.size() && s[i+1]=='&')
			{
				o+='&';
				i++;
			}
			continue;
		}
		o+=s[i];
	}
	return o;
}

static char mnemonic_of(const std::string& s)
{
	for(size_t i=0;i+1<s.size();i++)
	{
		if(s[i]=='&')
		{
			if(s[i+1]=='&')
			{
				i++;
				continue;
			}
			char c=s[i+1];
			if(c>='a' && c<='z')
				c-=32;
			return c;
		}
	}
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// menu bar
int menubar_height(HMENU)
{
	return font_height(FONT_UI)+6;
}

struct BarItemRect { int x; int w; };

static std::vector<BarItemRect> bar_layout(Win32Window* w)
{
	std::vector<BarItemRect> r;
	int x=2;
	for(MenuItem& it:w->menu->items)
	{
		int const tw=text_width(FONT_UI,strip_amp(it.text).c_str(),-1)+14;
		r.push_back({x,tw});
		x+=tw;
	}
	return r;
}

static int g_bar_open_index=-1;
static Win32Window* g_bar_open_window=nullptr;

void menubar_paint(Win32Window* w)
{
	if(!w->menu || !w->surface)
		return;

	HDC dc=create_surface_dc(w);
	int const h=w->menubar_h;
	fill_rect(dc,0,0,w->surface->w,h,CLR_FACE);

	std::vector<BarItemRect> lay=bar_layout(w);
	for(size_t i=0;i<w->menu->items.size();i++)
	{
		MenuItem& it=w->menu->items[i];
		bool const open=(g_bar_open_window==w && g_bar_open_index==(int)i);
		bool const hot=(w->menubar_hot==(int)i);
		int const x=lay[i].x;
		int const iw=lay[i].w;

		if(open)
		{
			fill_rect(dc,x,1,iw,h-2,CLR_SEL);
		}
		else if(hot)
		{
			frame_3d(dc,x,1,iw,h-2,false,false);
		}

		RECT r;
		r.left=x;
		r.top=0;
		r.right=x+iw;
		r.bottom=h;
		COLORREF const tc=(it.state & MF_GRAYED)?CLR_GRAYTEXT:(open?CLR_SELTEXT:CLR_TEXT);
		draw_text_in(dc,r,it.text.c_str(),FONT_UI,tc,1,true);
	}

	// separator line between menu bar and client area
	destroy_dc(dc);
}

int menubar_hit(Win32Window* w,int x,int y)
{
	if(!w->menu || y<0 || y>=w->menubar_h)
		return -1;
	std::vector<BarItemRect> lay=bar_layout(w);
	for(size_t i=0;i<lay.size();i++)
		if(x>=lay[i].x && x<lay[i].x+lay[i].w)
			return (int)i;
	return -1;
}

static int g_menubar_switch_to=-1;

void menubar_click(Win32Window* w,int x,int y)
{
	int index=menubar_hit(w,x,y);
	if(index<0)
		return;

	HWND const hwnd=w;

	// let the application update the menu (recent files etc.)
	SendMessage(hwnd,WM_INITMENU,(WPARAM)w->menu,0);

	while(index>=0 && is_valid_window(hwnd))
	{
		MenuItem& it=w->menu->items[index];
		std::vector<BarItemRect> lay=bar_layout(w);

		g_bar_open_window=w;
		g_bar_open_index=index;
		w->menubar_hot=-1;
		repaint_menubar(w);

		// client (0,0) is the bottom left of the menu bar in screen coordinates
		POINT p;
		p.x=lay[index].x;
		p.y=0;
		ClientToScreen(hwnd,&p);

		g_menubar_switch_to=-1;
		UINT cmd=0;
		if(it.submenu && !(it.state & MF_GRAYED))
			cmd=track_popup(it.submenu,p.x,p.y,hwnd,index);

		g_bar_open_window=nullptr;
		g_bar_open_index=-1;
		if(is_valid_window(hwnd))
			repaint_menubar(w);

		if(cmd)
		{
			post_internal(hwnd,WM_COMMAND,MAKEWPARAM(cmd,0),0);
			return;
		}

		index=g_menubar_switch_to;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// popup menus
#define POPUP_ITEM_H	18
#define POPUP_SEP_H		9
#define POPUP_BORDER	3
#define POPUP_GUTTER	20
#define POPUP_ARROW		18

struct ItemLayout
{
	int x;
	int y;
	int w;
	int h;
};

struct PopupLevel
{
	HMENU					menu;
	Win32Window*			win;
	std::vector<ItemLayout>	lay;
	int						hot;
	int						x;		// root position
	int						y;
	int						w;
	int						h;
	int						parent_item;
};

struct TrackState
{
	std::vector<PopupLevel>	levels;
	HWND					owner;
	int						menubar_index;
	bool					done;
	UINT					result;
	bool					moved;
};

static void layout_popup(PopupLevel& lv,int* out_w,int* out_h)
{
	HMENU m=lv.menu;
	lv.lay.resize(m->items.size());

	// compute columns (MF_MENUBARBREAK / MF_MENUBREAK starts a new column)
	std::vector<std::pair<int,int>> cols;	// [first,last)
	int first=0;
	for(size_t i=0;i<m->items.size();i++)
	{
		if(i>0 && (m->items[i].type & (MF_MENUBARBREAK|MF_MENUBREAK)))
		{
			cols.push_back({first,(int)i});
			first=(int)i;
		}
	}
	cols.push_back({first,(int)m->items.size()});

	int x=POPUP_BORDER;
	int max_h=0;

	for(auto& c:cols)
	{
		int lw=0,aw=0;
		bool has_sub=false;
		for(int i=c.first;i<c.second;i++)
		{
			MenuItem& it=m->items[i];
			std::string label,accel;
			split_text(it.text,label,accel);
			int const kind=(it.state & MFS_DEFAULT)?FONT_UI_BOLD:FONT_UI;
			lw=std::max(lw,text_width(kind,strip_amp(label).c_str(),-1));
			if(!accel.empty())
				aw=std::max(aw,text_width(FONT_UI,accel.c_str(),-1));
			if(it.submenu)
				has_sub=true;
		}
		int const colw=POPUP_GUTTER+lw+(aw?aw+24:0)+(has_sub?POPUP_ARROW:10)+4;

		int y=POPUP_BORDER;
		for(int i=c.first;i<c.second;i++)
		{
			MenuItem& it=m->items[i];
			int const ih=(it.type & MFT_SEPARATOR)?POPUP_SEP_H:POPUP_ITEM_H;
			lv.lay[i]={x,y,colw,ih};
			y+=ih;
		}
		max_h=std::max(max_h,y);
		x+=colw+((&c!=&cols.back())?4:0);
	}

	if(m->items.empty())
		max_h=POPUP_BORDER+POPUP_ITEM_H;

	*out_w=x+POPUP_BORDER;
	*out_h=max_h+POPUP_BORDER;
}

static void paint_popup(PopupLevel& lv)
{
	if(!lv.win || !lv.win->surface)
		return;

	HDC dc=create_surface_dc(lv.win);
	fill_rect(dc,0,0,lv.w,lv.h,CLR_FACE);
	frame_3d(dc,0,0,lv.w,lv.h,false,true);

	HMENU m=lv.menu;
	for(size_t i=0;i<m->items.size();i++)
	{
		MenuItem& it=m->items[i];
		ItemLayout& l=lv.lay[i];

		// column separator
		if(i>0 && (it.type & MF_MENUBARBREAK))
		{
			fill_rect(dc,l.x-3,POPUP_BORDER,1,lv.h-POPUP_BORDER*2,CLR_SHADOW);
			fill_rect(dc,l.x-2,POPUP_BORDER,1,lv.h-POPUP_BORDER*2,CLR_HILIGHT);
		}

		if(it.type & MFT_SEPARATOR)
		{
			fill_rect(dc,l.x+1,l.y+POPUP_SEP_H/2-1,l.w-2,1,CLR_SHADOW);
			fill_rect(dc,l.x+1,l.y+POPUP_SEP_H/2,l.w-2,1,CLR_HILIGHT);
			continue;
		}

		bool const hot=(lv.hot==(int)i);
		bool const grayed=(it.state & (MF_GRAYED|MF_DISABLED))!=0;
		COLORREF tc=grayed?CLR_GRAYTEXT:CLR_TEXT;

		if(hot)
		{
			fill_rect(dc,l.x,l.y,l.w,l.h,CLR_SEL);
			if(!grayed)
				tc=CLR_SELTEXT;
		}

		// check mark / radio bullet
		if(it.state & MFS_CHECKED)
		{
			int const cx=l.x+8;
			int const cy=l.y+l.h/2;
			if(it.type & MFT_RADIOCHECK)
			{
				fill_rect(dc,cx-1,cy-3,3,7,tc);
				fill_rect(dc,cx-3,cy-1,7,3,tc);
				fill_rect(dc,cx-2,cy-2,5,5,tc);
			}
			else
			{
				for(int k=0;k<3;k++)
					fill_rect(dc,cx-3+k,cy-1+k,1,3,tc);
				for(int k=0;k<4;k++)
					fill_rect(dc,cx+k,cy+1-k,1,3,tc);
			}
		}

		std::string label,accel;
		split_text(it.text,label,accel);
		int const kind=(it.state & MFS_DEFAULT)?FONT_UI_BOLD:FONT_UI;

		RECT r;
		r.left=l.x+POPUP_GUTTER;
		r.top=l.y;
		r.right=l.x+l.w-4;
		r.bottom=l.y+l.h;

		if(grayed && !hot)
		{
			RECT r2=r;
			r2.left++;
			r2.top++;
			r2.right++;
			r2.bottom++;
			draw_text_in(dc,r2,label.c_str(),kind,CLR_HILIGHT,0,true);
			if(!accel.empty())
			{
				RECT ra=r2;
				ra.right-=it.submenu?POPUP_ARROW:10;
				draw_text_in(dc,ra,accel.c_str(),FONT_UI,CLR_HILIGHT,2,true);
			}
		}

		draw_text_in(dc,r,label.c_str(),kind,tc,0,true);

		if(!accel.empty())
		{
			RECT ra=r;
			ra.right-=it.submenu?POPUP_ARROW:10;
			draw_text_in(dc,ra,accel.c_str(),FONT_UI,tc,2,true);
		}

		// submenu arrow
		if(it.submenu)
		{
			int const ax=l.x+l.w-10;
			int const ay=l.y+l.h/2;
			for(int k=0;k<4;k++)
				fill_rect(dc,ax+k,ay-3+k,1,7-k*2,tc);
		}
	}

	destroy_dc(dc);
	flush_all_surfaces();
}

static void open_level(TrackState& ts,HMENU menu,int x,int y,int parent_item,int parent_right_x)
{
	PopupLevel lv;
	lv.menu=menu;
	lv.hot=-1;
	lv.parent_item=parent_item;
	int w=0,h=0;
	layout_popup(lv,&w,&h);

	int sw=0,sh=0;
	screen_size(&sw,&sh);

	// keep inside the screen
	if(x+w>sw)
	{
		if(parent_right_x>=0 && !ts.levels.empty())
			x=ts.levels.back().x-w+3;
		else
			x=sw-w;
	}
	if(y+h>sh)
		y=std::max(0,sh-h);
	if(x<0)
		x=0;

	lv.x=x;
	lv.y=y;
	lv.w=w;
	lv.h=h;

	Win32Window* owner_top=top_level_of(ts.owner);
	lv.win=create_popup_surface(owner_top,x,y,w,h);
	ts.levels.push_back(lv);
	paint_popup(ts.levels.back());
	grab_input(ts.levels.back().win);
}

static void close_levels_above(TrackState& ts,size_t keep)
{
	while(ts.levels.size()>keep)
	{
		destroy_popup_surface(ts.levels.back().win);
		ts.levels.pop_back();
	}
	if(!ts.levels.empty())
		grab_input(ts.levels.back().win);
}

static int level_at(TrackState& ts,int rx,int ry)
{
	for(int i=(int)ts.levels.size()-1;i>=0;i--)
	{
		PopupLevel& lv=ts.levels[i];
		if(rx>=lv.x && rx<lv.x+lv.w && ry>=lv.y && ry<lv.y+lv.h)
			return i;
	}
	return -1;
}

static int item_at(PopupLevel& lv,int rx,int ry)
{
	int const lx=rx-lv.x;
	int const ly=ry-lv.y;
	for(size_t i=0;i<lv.lay.size();i++)
	{
		ItemLayout& l=lv.lay[i];
		if(lx>=l.x && lx<l.x+l.w && ly>=l.y && ly<l.y+l.h)
			return (int)i;
	}
	return -1;
}

static bool item_selectable(MenuItem& it)
{
	return !(it.type & MFT_SEPARATOR) && !(it.state & (MF_GRAYED|MF_DISABLED));
}

static void set_hot(TrackState& ts,int level,int item)
{
	PopupLevel& lv=ts.levels[level];
	if(lv.hot==item)
		return;
	lv.hot=item;
	paint_popup(lv);
}

static void open_submenu_of(TrackState& ts,int level)
{
	PopupLevel& lv=ts.levels[level];
	if(lv.hot<0)
		return;
	MenuItem& it=lv.menu->items[lv.hot];
	if(!it.submenu || (it.state & MF_GRAYED))
		return;
	if((int)ts.levels.size()>level+1 && ts.levels[level+1].parent_item==lv.hot)
		return;
	close_levels_above(ts,level+1);
	ItemLayout& l=lv.lay[lv.hot];
	open_level(ts,it.submenu,lv.x+l.x+l.w-2,lv.y+l.y-POPUP_BORDER,lv.hot,lv.x+lv.w);
}

static int next_selectable(PopupLevel& lv,int from,int dir)
{
	int const n=(int)lv.menu->items.size();
	if(n==0)
		return -1;
	int i=from;
	for(int k=0;k<n;k++)
	{
		i=(i+dir+n)%n;
		if(!(lv.menu->items[i].type & MFT_SEPARATOR))
			return i;
	}
	return -1;
}

static bool menu_input_hook(void* xevent,void* user)
{
	XEvent& ev=*(XEvent*)xevent;
	TrackState& ts=*(TrackState*)user;

	if(ts.levels.empty())
		return true;

	if(ev.type==MotionNotify)
	{
		int const rx=ev.xmotion.x_root;
		int const ry=ev.xmotion.y_root;
		ts.moved=true;

		int const li=level_at(ts,rx,ry);
		if(li>=0)
		{
			int const item=item_at(ts.levels[li],rx,ry);
			// leaving a submenu back to its parent: close deeper levels unless hovering the parent item
			if(item>=0)
			{
				set_hot(ts,li,item);
				if((int)ts.levels.size()>li+1 && ts.levels[li+1].parent_item!=item)
					close_levels_above(ts,li+1);
				open_submenu_of(ts,li);
			}
		}
		else if(ts.menubar_index>=0)
		{
			// moving over another menu bar item switches menus
			Win32Window* top=top_level_of(ts.owner);
			if(top && top->menu)
			{
				int const sx=rx-top->root_x;
				int const sy=ry-top->root_y;
				int const hit=menubar_hit(top,sx,sy);
				if(hit>=0 && hit!=ts.menubar_index)
				{
					g_menubar_switch_to=hit;
					ts.done=true;
					ts.result=0;
				}
			}
		}
		return true;
	}

	if(ev.type==ButtonPress || ev.type==ButtonRelease)
	{
		int const rx=ev.xbutton.x_root;
		int const ry=ev.xbutton.y_root;
		int const li=level_at(ts,rx,ry);

		if(ev.xbutton.button==Button4 || ev.xbutton.button==Button5)
			return true;

		if(li<0)
		{
			if(ev.type==ButtonPress)
			{
				// click on the owner's menu bar item that opened this menu just closes it
				ts.done=true;
				ts.result=0;
				if(ts.menubar_index>=0)
				{
					Win32Window* top=top_level_of(ts.owner);
					if(top && top->menu)
					{
						int const hit=menubar_hit(top,rx-top->root_x,ry-top->root_y);
						if(hit>=0 && hit!=ts.menubar_index)
							g_menubar_switch_to=hit;
					}
				}
			}
			return true;
		}

		PopupLevel& lv=ts.levels[li];
		int const item=item_at(lv,rx,ry);
		if(item<0)
			return true;

		MenuItem& it=lv.menu->items[item];
		if(it.submenu)
		{
			set_hot(ts,li,item);
			open_submenu_of(ts,li);
			return true;
		}

		if(ev.type==ButtonRelease && item_selectable(it))
		{
			ts.done=true;
			ts.result=it.id;
		}
		return true;
	}

	if(ev.type==KeyPress)
	{
		KeySym const ks=XLookupKeysym(&ev.xkey,0);
		int const li=(int)ts.levels.size()-1;
		PopupLevel& lv=ts.levels[li];

		switch(ks)
		{
		case XK_Escape:
			if(li>0)
				close_levels_above(ts,li);
			else
			{
				ts.done=true;
				ts.result=0;
			}
			break;

		case XK_Up:
			set_hot(ts,li,next_selectable(lv,lv.hot<0?0:lv.hot,-1));
			break;

		case XK_Down:
			set_hot(ts,li,next_selectable(lv,lv.hot<0?-1:lv.hot,1));
			break;

		case XK_Right:
			if(lv.hot>=0 && lv.menu->items[lv.hot].submenu)
			{
				open_submenu_of(ts,li);
				PopupLevel& sub=ts.levels.back();
				set_hot(ts,(int)ts.levels.size()-1,next_selectable(sub,-1,1));
			}
			else if(ts.menubar_index>=0)
			{
				Win32Window* top=top_level_of(ts.owner);
				if(top && top->menu)
				{
					g_menubar_switch_to=(ts.menubar_index+1)%(int)top->menu->items.size();
					ts.done=true;
				}
			}
			break;

		case XK_Left:
			if(li>0)
				close_levels_above(ts,li);
			else if(ts.menubar_index>=0)
			{
				Win32Window* top=top_level_of(ts.owner);
				if(top && top->menu)
				{
					int const n=(int)top->menu->items.size();
					g_menubar_switch_to=(ts.menubar_index-1+n)%n;
					ts.done=true;
				}
			}
			break;

		case XK_Return:
		case XK_KP_Enter:
		case XK_space:
			if(lv.hot>=0)
			{
				MenuItem& it=lv.menu->items[lv.hot];
				if(it.submenu)
					open_submenu_of(ts,li);
				else if(item_selectable(it))
				{
					ts.done=true;
					ts.result=it.id;
				}
			}
			break;

		case XK_Alt_L:
		case XK_Alt_R:
			ts.done=true;
			ts.result=0;
			break;

		default:
			{
				// mnemonic
				char buf[8];
				KeySym k2;
				int const n=XLookupString(&ev.xkey,buf,sizeof(buf),&k2,nullptr);
				if(n==1)
				{
					char c=buf[0];
					if(c>='a' && c<='z')
						c-=32;
					for(size_t i=0;i<lv.menu->items.size();i++)
					{
						MenuItem& it=lv.menu->items[i];
						if(mnemonic_of(it.text)==c && item_selectable(it))
						{
							if(it.submenu)
							{
								set_hot(ts,li,(int)i);
								open_submenu_of(ts,li);
							}
							else
							{
								ts.done=true;
								ts.result=it.id;
							}
							break;
						}
					}
				}
			}
			break;
		}
		return true;
	}

	// swallow key releases while a menu is open
	return true;
}

UINT track_popup(HMENU hmenu,int x,int y,HWND owner,int menubar_index)
{
	if(!valid_menu(hmenu))
		return 0;

	TrackState ts;
	ts.owner=owner;
	ts.menubar_index=menubar_index;
	ts.done=false;
	ts.result=0;
	ts.moved=false;

	open_level(ts,hmenu,x,y,-1,-1);
	set_input_hook(menu_input_hook,&ts);

	// modal loop: keep dispatching paint and timer messages to the application while the menu is open
	while(!ts.done)
	{
		MSG m;
		if(PeekMessage(&m,nullptr,0,0,PM_REMOVE))
		{
			if(m.message==WM_QUIT)
			{
				PostQuitMessage((int)m.wParam);
				break;
			}
			if(m.message==WM_PAINT || m.message==WM_TIMER || m.message==WM_SIZE)
				DispatchMessage(&m);
			continue;
		}
		pump_one(true);
	}

	set_input_hook(nullptr,nullptr);
	ungrab_input();
	close_levels_above(ts,0);
	ungrab_input();
	flush_all_surfaces();

	return ts.result;
}

BOOL TrackPopupMenu(HMENU hmenu,UINT flags,int x,int y,int,HWND hwnd,const RECT*)
{
	UINT const cmd=track_popup(hmenu,x,y,hwnd,-1);

	if(flags & TPM_RETURNCMD)
		return cmd;

	if(cmd && is_valid_window(hwnd))
		post_internal(hwnd,WM_COMMAND,MAKEWPARAM(cmd,0),0);

	return TRUE;
}
