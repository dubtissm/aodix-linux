/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - Win32 compatibility layer: built-in controls (EDIT, BUTTON, STATIC, COMBOBOX, LISTBOX, progress bar)
// drawn in the classic windows style used by the original Aodix dialogs
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "win32_internal.h"
#include "resources_data.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <algorithm>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static ControlData* ctl_of(HWND hwnd)
{
	if(!hwnd->ctl)
		hwnd->ctl=new ControlData();
	return hwnd->ctl;
}

static int ctl_font(HWND hwnd)
{
	return hwnd->font?hwnd->font->kind:FONT_UI;
}

static void notify_parent(HWND hwnd,int code)
{
	if(hwnd->parent)
		post_internal(hwnd->parent,WM_COMMAND,MAKEWPARAM(hwnd->id,code),(LPARAM)hwnd);
}

static void client_wh(HWND hwnd,int* w,int* h)
{
	RECT r;
	GetClientRect(hwnd,&r);
	*w=r.right;
	*h=r.bottom;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// EDIT
static int edit_text_x(HWND hwnd)
{
	return (hwnd->style & WS_BORDER)?4:1;
}

static int edit_hit(HWND hwnd,int x)
{
	ControlData* c=ctl_of(hwnd);
	int const kind=ctl_font(hwnd);
	int const tx=edit_text_x(hwnd)-c->scroll;
	std::string const& t=hwnd->text;
	int best=0;
	int prev=tx;
	for(int i=0;i<=(int)t.size();i++)
	{
		int const cx=tx+text_width(kind,t.c_str(),i);
		if(x<(prev+cx)/2+1 && i>0)
			return i-1;
		prev=cx;
		best=i;
	}
	return best;
}

static void edit_ensure_caret_visible(HWND hwnd)
{
	ControlData* c=ctl_of(hwnd);
	int w=0,h=0;
	client_wh(hwnd,&w,&h);
	int const kind=ctl_font(hwnd);
	int const avail=w-edit_text_x(hwnd)*2;
	int const cx=text_width(kind,hwnd->text.c_str(),c->caret);
	if(cx-c->scroll>avail)
		c->scroll=cx-avail;
	if(cx-c->scroll<0)
		c->scroll=cx;
	if(c->scroll<0)
		c->scroll=0;
}

static void edit_delete_selection(HWND hwnd)
{
	ControlData* c=ctl_of(hwnd);
	int const a=std::min(c->sel_start,c->sel_end);
	int const b=std::max(c->sel_start,c->sel_end);
	if(a!=b)
	{
		hwnd->text.erase(a,b-a);
		c->caret=a;
	}
	c->sel_start=c->sel_end=c->caret;
}

static int utf8_prev(const std::string& s,int i)
{
	if(i<=0)
		return 0;
	i--;
	while(i>0 && ((unsigned char)s[i]&0xC0)==0x80)
		i--;
	return i;
}

static int utf8_next(const std::string& s,int i)
{
	if(i>=(int)s.size())
		return (int)s.size();
	i++;
	while(i<(int)s.size() && ((unsigned char)s[i]&0xC0)==0x80)
		i++;
	return i;
}

static LRESULT edit_proc(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam)
{
	ControlData* c=ctl_of(hwnd);
	c->kind=CTL_EDIT;

	switch(msg)
	{
	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc=BeginPaint(hwnd,&ps);
			int w=0,h=0;
			client_wh(hwnd,&w,&h);

			COLORREF bk=hwnd->enabled?CLR_WINDOW:CLR_FACE;
			COLORREF tc=hwnd->enabled?CLR_TEXT:CLR_GRAYTEXT;

			// parent may customize colors (aodix does for its in-place edit boxes)
			if(hwnd->parent)
			{
				SetTextColor(hdc,tc);
				SetBkColor(hdc,bk);
				HBRUSH br=(HBRUSH)SendMessage(hwnd->parent,WM_CTLCOLOREDIT,(WPARAM)hdc,(LPARAM)hwnd);
				if(br)
				{
					bk=br->color;
					tc=hdc->text_color;
				}
			}

			fill_rect(hdc,0,0,w,h,bk);
			if(hwnd->style & WS_BORDER)
				frame_3d(hdc,0,0,w,h,true,true);

			int const kind=ctl_font(hwnd);
			int const tx=edit_text_x(hwnd)-c->scroll;
			int const ty=(h-font_height(kind))/2;

			// clip text to the inner area
			RECT clip;
			clip.left=edit_text_x(hwnd)-1;
			clip.top=(hwnd->style & WS_BORDER)?2:0;
			clip.right=w-clip.left;
			clip.bottom=h-clip.top;
			HRGN rgn=CreateRectRgn(clip.left,clip.top,clip.right,clip.bottom);
			SelectClipRgn(hdc,rgn);
			DeleteObject(rgn);

			bool const focused=(GetFocus()==hwnd);
			int const a=std::min(c->sel_start,c->sel_end);
			int const b=std::max(c->sel_start,c->sel_end);

			draw_text_surface(hdc,tx,ty,hwnd->text.c_str(),(int)hwnd->text.size(),kind,tc);

			if(focused && a!=b)
			{
				int const x1=tx+text_width(kind,hwnd->text.c_str(),a);
				int const x2=tx+text_width(kind,hwnd->text.c_str(),b);
				fill_rect(hdc,x1,ty-1,x2-x1,font_height(kind)+2,CLR_SEL);
				draw_text_surface(hdc,x1,ty,hwnd->text.c_str()+a,b-a,kind,CLR_SELTEXT);
			}

			if(focused && hwnd->enabled)
			{
				int const cx=tx+text_width(kind,hwnd->text.c_str(),c->caret);
				fill_rect(hdc,cx,ty-1,1,font_height(kind)+2,tc);
			}

			SelectClipRgn(hdc,nullptr);
			EndPaint(hwnd,&ps);
			return 0;
		}

	case WM_SETTEXT:
		hwnd->text=lparam?(const char*)lparam:"";
		c->caret=c->sel_start=c->sel_end=(int)hwnd->text.size();
		c->scroll=0;
		hwnd->invalid=true;
		return TRUE;

	case EM_SETSEL:
		{
			int a=(int)(intptr_t)wparam;
			int b=(int)(intptr_t)lparam;
			int const n=(int)hwnd->text.size();
			if(a<0)
			{
				a=b=n;
			}
			if(b<0 || b>n)
				b=n;
			if(a>n)
				a=n;
			c->sel_start=a;
			c->sel_end=b;
			c->caret=b;
			edit_ensure_caret_visible(hwnd);
			hwnd->invalid=true;
			return 0;
		}

	case WM_SETFOCUS:
	case WM_KILLFOCUS:
		hwnd->invalid=true;
		return 0;

	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
		{
			if(!hwnd->enabled)
				return 0;
			SetFocus(hwnd);
			int const x=(short)LOWORD(lparam);
			c->caret=edit_hit(hwnd,x);
			if(msg==WM_LBUTTONDBLCLK)
			{
				c->sel_start=0;
				c->sel_end=c->caret=(int)hwnd->text.size();
			}
			else
				c->sel_start=c->sel_end=c->caret;
			SetCapture(hwnd);
			c->pressed=true;
			hwnd->invalid=true;
			return 0;
		}

	case WM_MOUSEMOVE:
		if(c->pressed)
		{
			c->caret=c->sel_end=edit_hit(hwnd,(short)LOWORD(lparam));
			edit_ensure_caret_visible(hwnd);
			hwnd->invalid=true;
		}
		return 0;

	case WM_LBUTTONUP:
		if(c->pressed)
		{
			c->pressed=false;
			ReleaseCapture();
		}
		return 0;

	case WM_KEYDOWN:
		{
			bool const shift=GetKeyState(VK_SHIFT)<0;
			bool const ctrl=GetKeyState(VK_CONTROL)<0;
			int const n=(int)hwnd->text.size();
			int const old_caret=c->caret;
			bool move=false;

			switch(wparam)
			{
			case VK_LEFT:	c->caret=utf8_prev(hwnd->text,c->caret); move=true; break;
			case VK_RIGHT:	c->caret=utf8_next(hwnd->text,c->caret); move=true; break;
			case VK_HOME:	c->caret=0; move=true; break;
			case VK_END:	c->caret=n; move=true; break;
			case VK_DELETE:
				if(c->sel_start!=c->sel_end)
					edit_delete_selection(hwnd);
				else if(c->caret<n)
				{
					int const e=utf8_next(hwnd->text,c->caret);
					hwnd->text.erase(c->caret,e-c->caret);
				}
				c->sel_start=c->sel_end=c->caret;
				notify_parent(hwnd,EN_CHANGE);
				break;
			case 'A':
				if(ctrl)
				{
					c->sel_start=0;
					c->sel_end=c->caret=n;
				}
				break;
			}

			if(move)
			{
				if(shift)
				{
					if(c->sel_start==c->sel_end)
						c->sel_start=old_caret;
					c->sel_end=c->caret;
				}
				else
					c->sel_start=c->sel_end=c->caret;
			}

			edit_ensure_caret_visible(hwnd);
			hwnd->invalid=true;
			return 0;
		}

	case WM_CHAR:
		{
			if(!hwnd->enabled)
				return 0;
			DWORD const ch=(DWORD)wparam;

			if(GetKeyState(VK_CONTROL)<0 && ch<32)
				return 0;

			if(ch==8)
			{
				if(c->sel_start!=c->sel_end)
					edit_delete_selection(hwnd);
				else if(c->caret>0)
				{
					int const p=utf8_prev(hwnd->text,c->caret);
					hwnd->text.erase(p,c->caret-p);
					c->caret=p;
					c->sel_start=c->sel_end=c->caret;
				}
				notify_parent(hwnd,EN_CHANGE);
			}
			else if(ch>=32 && ch!=127)
			{
				edit_delete_selection(hwnd);
				std::string ins;
				if(ch<0x80)
					ins+=(char)ch;
				else if(ch<0x800)
				{
					ins+=(char)(0xC0|(ch>>6));
					ins+=(char)(0x80|(ch&0x3F));
				}
				else
				{
					ins+=(char)(0xE0|(ch>>12));
					ins+=(char)(0x80|((ch>>6)&0x3F));
					ins+=(char)(0x80|(ch&0x3F));
				}
				hwnd->text.insert(c->caret,ins);
				c->caret+=(int)ins.size();
				c->sel_start=c->sel_end=c->caret;
				notify_parent(hwnd,EN_CHANGE);
			}

			edit_ensure_caret_visible(hwnd);
			hwnd->invalid=true;
			return 0;
		}
	}

	return DefWindowProc(hwnd,msg,wparam,lparam);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BUTTON (push button, default push button, auto check box, group box)
static void draw_disabled_text(HDC hdc,RECT r,const char* text,int kind,int align)
{
	RECT r2=r;
	r2.left++;r2.top++;r2.right++;r2.bottom++;
	draw_text_in(hdc,r2,text,kind,CLR_HILIGHT,align,true);
	draw_text_in(hdc,r,text,kind,CLR_SHADOW,align,true);
}

static LRESULT button_proc(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam)
{
	ControlData* c=ctl_of(hwnd);
	int const type=hwnd->style & 0x0F;
	c->kind=(type==BS_AUTOCHECKBOX)?CTL_CHECKBOX:(type==BS_GROUPBOX)?CTL_GROUPBOX:CTL_BUTTON;

	switch(msg)
	{
	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc=BeginPaint(hwnd,&ps);
			int w=0,h=0;
			client_wh(hwnd,&w,&h);
			int const kind=ctl_font(hwnd);
			bool const focused=(GetFocus()==hwnd);

			if(c->kind==CTL_GROUPBOX)
			{
				// etched frame; text (if any) breaks the top line
				int const ty=font_height(kind)/2;
				fill_rect(hdc,0,ty,w-1,1,CLR_SHADOW);
				fill_rect(hdc,0,ty,1,h-ty-1,CLR_SHADOW);
				fill_rect(hdc,0,h-2,w-1,1,CLR_SHADOW);
				fill_rect(hdc,w-2,ty,1,h-ty-1,CLR_SHADOW);
				fill_rect(hdc,1,ty+1,w-3,1,CLR_HILIGHT);
				fill_rect(hdc,1,ty+1,1,h-ty-3,CLR_HILIGHT);
				fill_rect(hdc,0,h-1,w,1,CLR_HILIGHT);
				fill_rect(hdc,w-1,ty,1,h-ty,CLR_HILIGHT);
				if(!hwnd->text.empty())
				{
					int const tw=text_width(kind,hwnd->text.c_str(),-1);
					fill_rect(hdc,6,0,tw+4,font_height(kind),CLR_FACE);
					RECT r={8,0,8+tw,font_height(kind)};
					draw_text_in(hdc,r,hwnd->text.c_str(),kind,CLR_TEXT,0,false);
				}
			}
			else if(c->kind==CTL_CHECKBOX)
			{
				fill_rect(hdc,0,0,w,h,CLR_FACE);
				int const by=(h-13)/2;
				frame_3d(hdc,0,by,13,13,true,true);
				fill_rect(hdc,2,by+2,9,9,hwnd->enabled?(c->pressed?CLR_FACE:CLR_WINDOW):CLR_FACE);
				if(c->check)
				{
					COLORREF const cc=hwnd->enabled?CLR_TEXT:CLR_SHADOW;
					for(int k=0;k<3;k++)
						fill_rect(hdc,3+k,by+5+k,1,3,cc);
					for(int k=0;k<4;k++)
						fill_rect(hdc,6+k,by+7-k,1,3,cc);
				}
				RECT r={18,0,w,h};
				if(hwnd->enabled)
					draw_text_in(hdc,r,hwnd->text.c_str(),kind,CLR_TEXT,0,true);
				else
					draw_disabled_text(hdc,r,hwnd->text.c_str(),kind,0);
				if(focused)
				{
					int const tw=text_width(kind,hwnd->text.c_str(),-1);
					draw_focus_rect(hdc,16,(h-font_height(kind))/2-1,std::min(tw+4,w-16),font_height(kind)+2);
				}
			}
			else
			{
				bool const is_default=(type==BS_DEFPUSHBUTTON) || (hwnd->parent && hwnd->parent->dlg && hwnd->parent->dlg->default_id==hwnd->id && focused);
				fill_rect(hdc,0,0,w,h,CLR_FACE);
				int x=0,y=0,bw=w,bh=h;
				if(is_default || focused)
				{
					fill_rect(hdc,0,0,w,1,CLR_DKSHADOW);
					fill_rect(hdc,0,h-1,w,1,CLR_DKSHADOW);
					fill_rect(hdc,0,0,1,h,CLR_DKSHADOW);
					fill_rect(hdc,w-1,0,1,h,CLR_DKSHADOW);
					x=y=1;
					bw-=2;
					bh-=2;
				}
				if(c->pressed && c->hot)
				{
					fill_rect(hdc,x,y,bw,1,CLR_SHADOW);
					fill_rect(hdc,x,y+bh-1,bw,1,CLR_SHADOW);
					fill_rect(hdc,x,y,1,bh,CLR_SHADOW);
					fill_rect(hdc,x+bw-1,y,1,bh,CLR_SHADOW);
				}
				else
					frame_3d(hdc,x,y,bw,bh,false,true);

				int const off=(c->pressed && c->hot)?1:0;
				RECT r={off,off,w+off,h+off};
				if(hwnd->enabled)
					draw_text_in(hdc,r,hwnd->text.c_str(),kind,CLR_TEXT,1,true);
				else
					draw_disabled_text(hdc,r,hwnd->text.c_str(),kind,1);
				if(focused)
					draw_focus_rect(hdc,4,4,w-8,h-8);
			}

			EndPaint(hwnd,&ps);
			return 0;
		}

	case BM_GETCHECK:
		return c->check;

	case BM_SETCHECK:
		c->check=(int)wparam;
		hwnd->invalid=true;
		return 0;

	case BM_GETSTATE:
		return c->check|(c->pressed?0x0004:0)|(GetFocus()==hwnd?0x0008:0);

	case WM_SETFOCUS:
	case WM_KILLFOCUS:
		if(hwnd->parent)
			hwnd->parent->invalid=true;
		hwnd->invalid=true;
		return 0;

	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
		if(c->kind==CTL_GROUPBOX || !hwnd->enabled)
			return 0;
		SetFocus(hwnd);
		SetCapture(hwnd);
		c->pressed=true;
		c->hot=true;
		hwnd->invalid=true;
		return 0;

	case WM_MOUSEMOVE:
		if(c->pressed)
		{
			int w=0,h=0;
			client_wh(hwnd,&w,&h);
			int const x=(short)LOWORD(lparam);
			int const y=(short)HIWORD(lparam);
			bool const hot=(x>=0 && y>=0 && x<w && y<h);
			if(hot!=c->hot)
			{
				c->hot=hot;
				hwnd->invalid=true;
			}
		}
		return 0;

	case WM_LBUTTONUP:
		if(c->pressed)
		{
			ReleaseCapture();
			c->pressed=false;
			hwnd->invalid=true;
			if(c->hot)
			{
				if(c->kind==CTL_CHECKBOX)
					c->check=!c->check;
				notify_parent(hwnd,BN_CLICKED);
			}
		}
		return 0;

	case WM_KEYDOWN:
		if(wparam==VK_SPACE && hwnd->enabled && c->kind!=CTL_GROUPBOX)
		{
			if(c->kind==CTL_CHECKBOX)
				c->check=!c->check;
			hwnd->invalid=true;
			notify_parent(hwnd,BN_CLICKED);
		}
		return 0;
	}

	return DefWindowProc(hwnd,msg,wparam,lparam);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// STATIC
static void draw_app_icon(HDC hdc,int x,int y)
{
	const unsigned char* d=res_aodix_ico;
	int const w=d[6]?d[6]:256;
	int const h=d[7]?d[7]:256;
	uint32_t const ofs=d[18]|(d[19]<<8)|(d[20]<<16)|(d[21]<<24);
	const unsigned char* bmp=d+ofs;
	int const bpp=bmp[14]|(bmp[15]<<8);
	uint32_t const hdr=bmp[0]|(bmp[1]<<8)|(bmp[2]<<16)|(bmp[3]<<24);
	if(bpp!=24)
		return;
	const unsigned char* pix=bmp+hdr;
	int const row=((w*24+31)/32)*4;
	const unsigned char* mask=pix+row*h;
	int const mrow=((w+31)/32)*4;
	for(int yy=0;yy<h;yy++)
	{
		const unsigned char* s=pix+(h-1-yy)*row;
		const unsigned char* m=mask+(h-1-yy)*mrow;
		for(int xx=0;xx<w;xx++)
		{
			if((m[xx>>3]>>(7-(xx&7)))&1)
				continue;
			SetPixel(hdc,x+xx,y+yy,RGB(s[xx*3+2],s[xx*3+1],s[xx*3]));
		}
	}
}

static LRESULT static_proc(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam)
{
	ControlData* c=ctl_of(hwnd);
	c->kind=CTL_STATIC;

	if(msg==WM_PAINT)
	{
		PAINTSTRUCT ps;
		HDC hdc=BeginPaint(hwnd,&ps);
		int w=0,h=0;
		client_wh(hwnd,&w,&h);
		int const kind=ctl_font(hwnd);
		int const ss=hwnd->style & 0x1F;

		fill_rect(hdc,0,0,w,h,CLR_FACE);

		if(ss==SS_ICON)
		{
			draw_app_icon(hdc,0,0);
		}
		else if(ss==SS_BLACKFRAME)
		{
			fill_rect(hdc,0,0,w,1,CLR_DKSHADOW);
			fill_rect(hdc,0,h-1,w,1,CLR_DKSHADOW);
			fill_rect(hdc,0,0,1,h,CLR_DKSHADOW);
			fill_rect(hdc,w-1,0,1,h,CLR_DKSHADOW);
		}
		else
		{
			if(hwnd->ex_style & WS_EX_STATICEDGE)
			{
				fill_rect(hdc,0,0,w-1,1,CLR_SHADOW);
				fill_rect(hdc,0,0,1,h-1,CLR_SHADOW);
				fill_rect(hdc,0,h-1,w,1,CLR_HILIGHT);
				fill_rect(hdc,w-1,0,1,h,CLR_HILIGHT);
			}
			int const align=(ss==SS_CENTER)?1:(ss==2?2:0);
			RECT r={1,0,w-1,h};
			bool const vcenter=(hwnd->style & SS_CENTERIMAGE)!=0;
			if(!vcenter)
				r.top=0;
			draw_text_in(hdc,r,hwnd->text.c_str(),kind,hwnd->enabled?CLR_TEXT:CLR_GRAYTEXT,align,vcenter);
		}

		EndPaint(hwnd,&ps);
		return 0;
	}

	return DefWindowProc(hwnd,msg,wparam,lparam);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LISTBOX (also used for combo box dropdowns)
static int list_item_h(void)
{
	return font_height(FONT_UI);
}

static void paint_list_items(HDC hdc,ControlData* c,int x,int y,int w,int h,bool focused,bool enabled)
{
	int const ih=list_item_h();
	int const visible=std::max(1,h/ih);
	bool const need_scroll=(int)c->items.size()>visible;
	int const sbw=need_scroll?16:0;
	int const kind=FONT_UI;

	for(int i=0;i<visible;i++)
	{
		int const idx=c->top_index+i;
		if(idx>=(int)c->items.size())
			break;
		int const iy=y+i*ih;
		bool const sel=(idx==c->cur_sel);
		if(sel)
			fill_rect(hdc,x,iy,w-sbw,ih,enabled?CLR_SEL:CLR_FACE);
		RECT r={x+2,iy,x+w-sbw-2,iy+ih};
		draw_text_in(hdc,r,c->items[idx].c_str(),kind,!enabled?CLR_GRAYTEXT:(sel?CLR_SELTEXT:CLR_TEXT),0,true);
		if(sel && focused)
			draw_focus_rect(hdc,x,iy,w-sbw,ih);
	}

	if(need_scroll)
	{
		int const sx=x+w-sbw;
		fill_rect(hdc,sx,y,sbw,h,0x00E0E0E0);
		// arrows
		frame_3d(hdc,sx,y,sbw,16,false,true);
		frame_3d(hdc,sx,y+h-16,sbw,16,false,true);
		for(int k=0;k<4;k++)
		{
			fill_rect(hdc,sx+7-k,y+6+k,1+k*2,1,CLR_TEXT);
			fill_rect(hdc,sx+7-k,y+h-6-k-1,1+k*2,1,CLR_TEXT);
		}
		// thumb
		int const track=h-32;
		int const total=(int)c->items.size();
		int const th=std::max(8,track*visible/std::max(total,1));
		int const ty=y+16+(track-th)*c->top_index/std::max(1,total-visible);
		frame_3d(hdc,sx,ty,sbw,th,false,true);
		fill_rect(hdc,sx+2,ty+2,sbw-4,th-4,CLR_FACE);
	}
}

static void list_clamp_top(ControlData* c,int visible)
{
	int const total=(int)c->items.size();
	c->top_index=std::max(0,std::min(c->top_index,total-visible));
}

static void list_ensure_visible(ControlData* c,int visible)
{
	if(c->cur_sel<0)
		return;
	if(c->cur_sel<c->top_index)
		c->top_index=c->cur_sel;
	if(c->cur_sel>=c->top_index+visible)
		c->top_index=c->cur_sel-visible+1;
	list_clamp_top(c,visible);
}

// returns item index, -2 for scroll up arrow, -3 for scroll down arrow, -4 for track, -1 none
static int list_hit(ControlData* c,int x,int y,int w,int h)
{
	int const ih=list_item_h();
	int const visible=std::max(1,h/ih);
	bool const need_scroll=(int)c->items.size()>visible;
	if(need_scroll && x>=w-16)
	{
		if(y<16) return -2;
		if(y>=h-16) return -3;
		return -4;
	}
	int const idx=c->top_index+y/ih;
	if(y<0 || idx>=(int)c->items.size())
		return -1;
	return idx;
}

static LRESULT list_common_msg(HWND hwnd,ControlData* c,UINT msg,WPARAM wparam,LPARAM lparam,bool* handled)
{
	*handled=true;
	switch(msg)
	{
	case LB_ADDSTRING:
	case CB_ADDSTRING:
		c->items.push_back(lparam?(const char*)lparam:"");
		hwnd->invalid=true;
		return (LRESULT)c->items.size()-1;
	case LB_RESETCONTENT:
	case CB_RESETCONTENT:
		c->items.clear();
		c->cur_sel=-1;
		c->top_index=0;
		hwnd->invalid=true;
		return 0;
	case LB_SETCURSEL:
	case CB_SETCURSEL:
		{
			int const i=(int)(intptr_t)wparam;
			c->cur_sel=(i>=0 && i<(int)c->items.size())?i:-1;
			hwnd->invalid=true;
			return c->cur_sel>=0?c->cur_sel:-1;
		}
	case LB_GETCURSEL:
	case CB_GETCURSEL:
		return c->cur_sel;
	case LB_GETCOUNT:
	case CB_GETCOUNT:
		return (LRESULT)c->items.size();
	case LB_GETTEXT:
	case CB_GETLBTEXT:
		{
			int const i=(int)wparam;
			if(i<0 || i>=(int)c->items.size() || !lparam)
				return -1;
			strcpy((char*)lparam,c->items[i].c_str());
			return (LRESULT)c->items[i].size();
		}
	}
	*handled=false;
	return 0;
}

static LRESULT listbox_proc(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam)
{
	ControlData* c=ctl_of(hwnd);
	c->kind=CTL_LISTBOX;

	bool handled=false;
	LRESULT const r=list_common_msg(hwnd,c,msg,wparam,lparam,&handled);
	if(handled)
		return r;

	int w=0,h=0;
	client_wh(hwnd,&w,&h);
	int const visible=std::max(1,(h-4)/list_item_h());

	switch(msg)
	{
	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc=BeginPaint(hwnd,&ps);
			fill_rect(hdc,0,0,w,h,hwnd->enabled?CLR_WINDOW:CLR_FACE);
			paint_list_items(hdc,c,2,2,w-4,h-4,GetFocus()==hwnd,hwnd->enabled);
			frame_3d(hdc,0,0,w,h,true,true);
			EndPaint(hwnd,&ps);
			return 0;
		}

	case WM_SETFOCUS:
	case WM_KILLFOCUS:
		hwnd->invalid=true;
		return 0;

	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
		{
			if(!hwnd->enabled)
				return 0;
			SetFocus(hwnd);
			int const x=(short)LOWORD(lparam)-2;
			int const y=(short)HIWORD(lparam)-2;
			int const hit=list_hit(c,x,y,w-4,h-4);
			if(hit==-2) c->top_index--;
			else if(hit==-3) c->top_index++;
			else if(hit==-4)
			{
				if(y<(h-4)/2) c->top_index-=visible; else c->top_index+=visible;
			}
			else if(hit>=0)
			{
				bool const changed=(c->cur_sel!=hit);
				c->cur_sel=hit;
				if(changed)
					notify_parent(hwnd,LBN_SELCHANGE);
				if(msg==WM_LBUTTONDBLCLK)
					notify_parent(hwnd,LBN_DBLCLK);
			}
			list_clamp_top(c,visible);
			hwnd->invalid=true;
			return 0;
		}

	case WM_MOUSEWHEEL:
		c->top_index-=((short)HIWORD(wparam))/40;
		list_clamp_top(c,visible);
		hwnd->invalid=true;
		return 0;

	case WM_KEYDOWN:
		{
			int const n=(int)c->items.size();
			if(n==0)
				return 0;
			int sel=c->cur_sel;
			switch(wparam)
			{
			case VK_UP:		sel=std::max(0,sel-1); break;
			case VK_DOWN:	sel=std::min(n-1,sel+1); break;
			case VK_PRIOR:	sel=std::max(0,sel-visible); break;
			case VK_NEXT:	sel=std::min(n-1,sel+visible); break;
			case VK_HOME:	sel=0; break;
			case VK_END:	sel=n-1; break;
			default:		return 0;
			}
			if(sel!=c->cur_sel)
			{
				c->cur_sel=sel;
				list_ensure_visible(c,visible);
				notify_parent(hwnd,LBN_SELCHANGE);
				hwnd->invalid=true;
			}
			return 0;
		}
	}

	return DefWindowProc(hwnd,msg,wparam,lparam);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// COMBOBOX (drop down list)
struct DropState
{
	Win32Window*	popup;
	HWND			combo;
	int				x;
	int				y;
	int				w;
	int				h;
	int				hot;
	int				top;
	int				visible;
	bool			done;
	int				result;
};

static void paint_drop(DropState& ds)
{
	ControlData* c=ctl_of(ds.combo);
	HDC hdc=create_surface_dc(ds.popup);
	fill_rect(hdc,0,0,ds.w,ds.h,CLR_WINDOW);
	ControlData tmp=*c;
	tmp.cur_sel=ds.hot;
	tmp.top_index=ds.top;
	paint_list_items(hdc,&tmp,1,1,ds.w-2,ds.h-2,false,true);
	fill_rect(hdc,0,0,ds.w,1,CLR_TEXT);
	fill_rect(hdc,0,ds.h-1,ds.w,1,CLR_TEXT);
	fill_rect(hdc,0,0,1,ds.h,CLR_TEXT);
	fill_rect(hdc,ds.w-1,0,1,ds.h,CLR_TEXT);
	destroy_dc(hdc);
	flush_all_surfaces();
}

static bool drop_hook(void* xevent,void* user)
{
	XEvent& ev=*(XEvent*)xevent;
	DropState& ds=*(DropState*)user;
	ControlData* c=ctl_of(ds.combo);
	int const ih=list_item_h();
	int const n=(int)c->items.size();

	if(ev.type==MotionNotify || ev.type==ButtonPress || ev.type==ButtonRelease)
	{
		int rx=0,ry=0;
		if(ev.type==MotionNotify) { rx=ev.xmotion.x_root; ry=ev.xmotion.y_root; }
		else { rx=ev.xbutton.x_root; ry=ev.xbutton.y_root; }
		int const lx=rx-ds.x-1;
		int const ly=ry-ds.y-1;
		bool const inside=(lx>=0 && ly>=0 && lx<ds.w-2 && ly<ds.h-2);

		if((ev.type==ButtonPress) && (ev.xbutton.button==Button4 || ev.xbutton.button==Button5))
		{
			ds.top+=(ev.xbutton.button==Button4)?-3:3;
			ds.top=std::max(0,std::min(ds.top,n-ds.visible));
			paint_drop(ds);
			return true;
		}

		if(!inside)
		{
			if(ev.type==ButtonPress)
			{
				ds.done=true;
				ds.result=-1;
			}
			return true;
		}

		ControlData tmp=*c;
		tmp.top_index=ds.top;
		int const hit=list_hit(&tmp,lx,ly,ds.w-2,ds.h-2);

		if(hit==-2 || hit==-3 || hit==-4)
		{
			if(ev.type==ButtonPress)
			{
				if(hit==-2) ds.top--;
				else if(hit==-3) ds.top++;
				else ds.top+=(ly<(ds.h-2)/2)?-ds.visible:ds.visible;
				ds.top=std::max(0,std::min(ds.top,n-ds.visible));
				paint_drop(ds);
			}
			return true;
		}

		if(hit>=0 && hit!=ds.hot)
		{
			ds.hot=hit;
			paint_drop(ds);
		}
		if(ev.type==ButtonRelease && hit>=0)
		{
			ds.done=true;
			ds.result=hit;
		}
		(void)ih;
		return true;
	}

	if(ev.type==KeyPress)
	{
		KeySym const ks=XLookupKeysym(&ev.xkey,0);
		if(ks==XK_Escape) { ds.done=true; ds.result=-1; }
		else if(ks==XK_Return || ks==XK_KP_Enter) { ds.done=true; ds.result=ds.hot; }
		else if(ks==XK_Up && ds.hot>0) ds.hot--;
		else if(ks==XK_Down && ds.hot<n-1) ds.hot++;
		if(ds.hot<ds.top) ds.top=ds.hot;
		if(ds.hot>=ds.top+ds.visible) ds.top=ds.hot-ds.visible+1;
		if(!ds.done)
			paint_drop(ds);
		return true;
	}

	return true;
}

static void combo_dropdown(HWND hwnd)
{
	ControlData* c=ctl_of(hwnd);
	if(c->items.empty())
		return;

	int w=0,h=0;
	client_wh(hwnd,&w,&h);

	POINT p={0,h};
	ClientToScreen(hwnd,&p);

	DropState ds;
	ds.combo=hwnd;
	ds.visible=std::min<int>((int)c->items.size(),std::max(8,(int)(c->range_max)));
	ds.w=w;
	ds.h=ds.visible*list_item_h()+2;
	ds.x=p.x;
	ds.y=p.y;
	int sw=0,sh=0;
	screen_size(&sw,&sh);
	if(ds.y+ds.h>sh)
		ds.y=p.y-h-ds.h;
	ds.hot=c->cur_sel;
	ds.top=0;
	if(ds.hot>=ds.visible)
		ds.top=ds.hot-ds.visible+1;
	ds.done=false;
	ds.result=-1;

	ds.popup=create_popup_surface(top_level_of(hwnd),ds.x,ds.y,ds.w,ds.h);
	paint_drop(ds);
	grab_input(ds.popup);
	set_input_hook(drop_hook,&ds);

	while(!ds.done)
	{
		MSG m;
		if(PeekMessage(&m,nullptr,0,0,PM_REMOVE))
		{
			if(m.message==WM_PAINT || m.message==WM_TIMER)
				DispatchMessage(&m);
			continue;
		}
		pump_one(true);
	}

	set_input_hook(nullptr,nullptr);
	ungrab_input();
	destroy_popup_surface(ds.popup);

	if(ds.result>=0 && ds.result!=c->cur_sel)
	{
		c->cur_sel=ds.result;
		notify_parent(hwnd,CBN_SELCHANGE);
	}
	hwnd->invalid=true;
}

static LRESULT combo_proc(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam)
{
	ControlData* c=ctl_of(hwnd);
	c->kind=CTL_COMBOBOX;

	bool handled=false;
	LRESULT const r=list_common_msg(hwnd,c,msg,wparam,lparam,&handled);
	if(handled)
		return r;

	switch(msg)
	{
	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc=BeginPaint(hwnd,&ps);
			int w=0,h=0;
			client_wh(hwnd,&w,&h);
			bool const focused=(GetFocus()==hwnd);

			fill_rect(hdc,0,0,w,h,hwnd->enabled?CLR_WINDOW:CLR_FACE);
			frame_3d(hdc,0,0,w,h,true,true);

			// drop button
			int const bw=16;
			frame_3d(hdc,w-bw-2,2,bw,h-4,false,true);
			fill_rect(hdc,w-bw,4,bw-4,h-8,CLR_FACE);
			COLORREF const ac=hwnd->enabled?CLR_TEXT:CLR_GRAYTEXT;
			int const ax=w-bw-2+bw/2;
			int const ay=h/2-1;
			for(int k=0;k<4;k++)
				fill_rect(hdc,ax-3+k,ay+k,7-k*2,1,ac);

			if(c->cur_sel>=0 && c->cur_sel<(int)c->items.size())
			{
				RECT tr={3,2,w-bw-4,h-2};
				if(focused && hwnd->enabled)
				{
					fill_rect(hdc,3,3,w-bw-7,h-6,CLR_SEL);
					draw_text_in(hdc,tr,c->items[c->cur_sel].c_str(),FONT_UI,CLR_SELTEXT,0,true);
					draw_focus_rect(hdc,3,3,w-bw-7,h-6);
				}
				else
					draw_text_in(hdc,tr,c->items[c->cur_sel].c_str(),FONT_UI,hwnd->enabled?CLR_TEXT:CLR_GRAYTEXT,0,true);
			}

			EndPaint(hwnd,&ps);
			return 0;
		}

	case WM_SETFOCUS:
	case WM_KILLFOCUS:
		hwnd->invalid=true;
		return 0;

	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
		if(!hwnd->enabled)
			return 0;
		SetFocus(hwnd);
		combo_dropdown(hwnd);
		return 0;

	case WM_KEYDOWN:
		{
			int const n=(int)c->items.size();
			int sel=c->cur_sel;
			if(wparam==VK_UP) sel=std::max(0,sel-1);
			else if(wparam==VK_DOWN) sel=std::min(n-1,sel+1);
			else if(wparam==VK_F4 || wparam==VK_SPACE) { combo_dropdown(hwnd); return 0; }
			else return 0;
			if(sel!=c->cur_sel)
			{
				c->cur_sel=sel;
				notify_parent(hwnd,CBN_SELCHANGE);
				hwnd->invalid=true;
			}
			return 0;
		}
	}

	return DefWindowProc(hwnd,msg,wparam,lparam);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// progress bar
static LRESULT progress_proc(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam)
{
	ControlData* c=ctl_of(hwnd);
	c->kind=CTL_PROGRESS;

	switch(msg)
	{
	case WM_CREATE:
		c->bk_color=CLR_FACE;
		c->bar_color=CLR_SEL;
		return 0;

	case PBM_SETRANGE32:
		c->range_min=(int)wparam;
		c->range_max=(int)lparam;
		hwnd->invalid=true;
		return 0;

	case PBM_SETRANGE:
		c->range_min=LOWORD(lparam);
		c->range_max=HIWORD(lparam);
		hwnd->invalid=true;
		return 0;

	case PBM_SETPOS:
		{
			int const old=c->pos;
			c->pos=(int)wparam;
			hwnd->invalid=true;
			// render progress immediately (aodix pumps messages while bouncing)
			return old;
		}

	case PBM_SETBARCOLOR:
		c->bar_color=(COLORREF)lparam;
		hwnd->invalid=true;
		return 0;

	case PBM_SETBKCOLOR:
		c->bk_color=(COLORREF)lparam;
		hwnd->invalid=true;
		return 0;

	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc=BeginPaint(hwnd,&ps);
			int w=0,h=0;
			client_wh(hwnd,&w,&h);
			fill_rect(hdc,0,0,w,h,c->bk_color);
			int const range=std::max(1,c->range_max-c->range_min);
			int const p=std::max(0,std::min(range,c->pos-c->range_min));
			int const inner=w-4;
			int const fill=(int)((int64_t)inner*p/range);
			// classic segmented blocks
			int const seg=std::max(4,(h-4)*2/3);
			for(int x=0;x<fill;x+=seg+2)
				fill_rect(hdc,2+x,2,std::min(seg,fill-x),h-4,c->bar_color);
			fill_rect(hdc,0,0,w-1,1,CLR_SHADOW);
			fill_rect(hdc,0,0,1,h-1,CLR_SHADOW);
			fill_rect(hdc,0,h-1,w,1,CLR_HILIGHT);
			fill_rect(hdc,w-1,0,1,h,CLR_HILIGHT);
			EndPaint(hwnd,&ps);
			return 0;
		}
	}

	return DefWindowProc(hwnd,msg,wparam,lparam);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static LRESULT dialog_class_proc(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam)
{
	if(msg==WM_PAINT)
	{
		PAINTSTRUCT ps;
		HDC hdc=BeginPaint(hwnd,&ps);
		fill_rect(hdc,0,0,ps.rcPaint.right,ps.rcPaint.bottom,CLR_FACE);
		EndPaint(hwnd,&ps);
		return 0;
	}

	if(hwnd->dlg && hwnd->dlg->proc)
	{
		INT_PTR const r=hwnd->dlg->proc(hwnd,msg,wparam,lparam);
		if(r)
			return r;
	}

	if(msg==WM_CLOSE)
	{
		post_internal(hwnd,WM_COMMAND,MAKEWPARAM(IDCANCEL,0),0);
		return 0;
	}

	return DefWindowProc(hwnd,msg,wparam,lparam);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void register_builtin_classes(void)
{
	HCURSOR const arrow=LoadCursor(nullptr,IDC_ARROW);
	HCURSOR const ibeam=LoadCursor(nullptr,IDC_IBEAM);

	struct { const char* name; WNDPROC proc; HCURSOR cur; UINT style; } classes[]=
	{
		{"EDIT",edit_proc,ibeam,CS_DBLCLKS},
		{"BUTTON",button_proc,arrow,0},
		{"STATIC",static_proc,arrow,0},
		{"COMBOBOX",combo_proc,arrow,0},
		{"LISTBOX",listbox_proc,arrow,CS_DBLCLKS},
		{"msctls_progress32",progress_proc,arrow,0},
		{"#32770",dialog_class_proc,arrow,0},
	};

	for(auto& c:classes)
	{
		WNDCLASS wc;
		memset(&wc,0,sizeof(wc));
		wc.lpfnWndProc=c.proc;
		wc.lpszClassName=c.name;
		wc.hCursor=c.cur;
		wc.style=c.style;
		RegisterClass(&wc);
	}
}
