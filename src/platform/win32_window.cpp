/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - Win32 compatibility layer: windows, message queue, timers and the X11 backend
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "win32_internal.h"
#include "resources_data.h"
#include "adxplat_runloop.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <clocale>

#include <algorithm>
#include <unordered_set>
#include <map>

#define WINDOW_MAGIC 0x41445857

HMENU load_menu_resource(int id);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// globals
static Display*		g_dpy=nullptr;
static int			g_screen=0;
static ::Window		g_root=0;
static Visual*		g_visual=nullptr;
static int			g_depth=24;
static bool			g_use_shm=false;
static XIM			g_xim=nullptr;

static Atom			a_wm_protocols,a_wm_delete,a_net_wm_name,a_utf8,a_net_wm_state,a_net_wm_state_max_v,a_net_wm_state_max_h,a_net_wm_state_fullscreen;
static Atom			a_net_wm_icon,a_net_wm_type,a_net_wm_type_dialog,a_net_wm_type_menu,a_net_wm_type_normal,a_net_wm_type_utility,a_net_wm_pid;

static std::unordered_set<Win32Window*>	g_windows;
static std::vector<Win32Window*>		g_top_levels;
static std::map<std::string,WindowClass*> g_classes;
static std::deque<MSG>					g_queue;
static bool								g_quit=false;
static int								g_quit_code=0;

static Win32Window*	g_desktop=nullptr;
static Win32Window*	g_focus=nullptr;
static Win32Window*	g_capture=nullptr;
static Win32Window*	g_active_top=nullptr;
static Win32Window*	g_pointer_top=nullptr;
static int			g_last_root_x=0;
static int			g_last_root_y=0;
static bool			g_have_pointer=false;
static bool			g_key_down[256];
static unsigned int	g_mod_state=0;

// ui scaling: logical coordinates (seen by the application) * num/den = physical pixels
static int			g_scale_num=1;
static int			g_scale_den=1;

static int			g_event_origin_x=0;	// logical root origin of the window of the current pointer event
static int			g_event_origin_y=0;

static input_hook_fn g_input_hook=nullptr;
static void*		g_input_hook_user=nullptr;

static bool			g_cursor_set_in_dispatch=false;

// double click tracking
static DWORD		g_last_click_time=0;
static int			g_last_click_x=-100;
static int			g_last_click_y=-100;
static Win32Window*	g_last_click_win=nullptr;

struct TimerEntry
{
	HWND		hwnd;
	UINT_PTR	id;
	UINT		interval;
	DWORD		due;
	TIMERPROC	proc;
	bool		pending;
};
static std::vector<TimerEntry> g_timers;

struct FdWatch
{
	int						fd;
	adxplat::fd_callback	cb;
	void*					user;
};
static std::vector<FdWatch> g_fd_watches;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
DWORD now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC,&ts);
	return (DWORD)(ts.tv_sec*1000ull+ts.tv_nsec/1000000ull);
}

DWORD GetTickCount(void)
{
	return now_ms();
}

DWORD timeGetTime(void)
{
	return now_ms();
}

void Sleep(DWORD ms)
{
	usleep(ms*1000);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// x error handler (don't abort on BadWindow etc. from destroyed plugin windows)
static int x_error_handler(Display* d,XErrorEvent* e)
{
	char buf[256];
	XGetErrorText(d,e->error_code,buf,sizeof(buf));
	fprintf(stderr,"aodix: X error: %s (request %d)\n",buf,e->request_code);
	return 0;
}

static bool g_shm_error=false;
static int x_shm_error_handler(Display*,XErrorEvent*)
{
	g_shm_error=true;
	return 0;
}

static Win32Window* new_window_struct(void);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool adxplat::init(int,char**)
{
	setlocale(LC_CTYPE,"");
	XInitThreads();

	g_dpy=XOpenDisplay(nullptr);
	if(!g_dpy)
	{
		fprintf(stderr,"aodix: cannot open X display (is DISPLAY set?)\n");
		return false;
	}

	XSetErrorHandler(x_error_handler);

	g_screen=DefaultScreen(g_dpy);
	g_root=RootWindow(g_dpy,g_screen);
	g_visual=DefaultVisual(g_dpy,g_screen);
	g_depth=DefaultDepth(g_dpy,g_screen);

	if(g_visual->c_class!=TrueColor || (g_depth!=24 && g_depth!=32) || g_visual->red_mask!=0xFF0000 || g_visual->blue_mask!=0xFF)
	{
		// try to find a matching visual
		XVisualInfo vi;
		if(XMatchVisualInfo(g_dpy,g_screen,24,TrueColor,&vi) && vi.red_mask==0xFF0000 && vi.blue_mask==0xFF)
		{
			g_visual=vi.visual;
			g_depth=24;
		}
		else
		{
			fprintf(stderr,"aodix: a 24-bit TrueColor X visual is required\n");
			return false;
		}
	}

	// shared memory images (local displays only)
	int shm_major=0,shm_minor=0;
	Bool shm_pixmaps=False;
	g_use_shm=XShmQueryExtension(g_dpy) && XShmQueryVersion(g_dpy,&shm_major,&shm_minor,&shm_pixmaps);
	if(getenv("AODIX_NO_SHM"))
		g_use_shm=false;

	a_wm_protocols=XInternAtom(g_dpy,"WM_PROTOCOLS",False);
	a_wm_delete=XInternAtom(g_dpy,"WM_DELETE_WINDOW",False);
	a_net_wm_name=XInternAtom(g_dpy,"_NET_WM_NAME",False);
	a_utf8=XInternAtom(g_dpy,"UTF8_STRING",False);
	a_net_wm_state=XInternAtom(g_dpy,"_NET_WM_STATE",False);
	a_net_wm_state_max_v=XInternAtom(g_dpy,"_NET_WM_STATE_MAXIMIZED_VERT",False);
	a_net_wm_state_max_h=XInternAtom(g_dpy,"_NET_WM_STATE_MAXIMIZED_HORZ",False);
	a_net_wm_state_fullscreen=XInternAtom(g_dpy,"_NET_WM_STATE_FULLSCREEN",False);
	a_net_wm_icon=XInternAtom(g_dpy,"_NET_WM_ICON",False);
	a_net_wm_type=XInternAtom(g_dpy,"_NET_WM_WINDOW_TYPE",False);
	a_net_wm_type_dialog=XInternAtom(g_dpy,"_NET_WM_WINDOW_TYPE_DIALOG",False);
	a_net_wm_type_menu=XInternAtom(g_dpy,"_NET_WM_WINDOW_TYPE_POPUP_MENU",False);
	a_net_wm_type_normal=XInternAtom(g_dpy,"_NET_WM_WINDOW_TYPE_NORMAL",False);
	a_net_wm_type_utility=XInternAtom(g_dpy,"_NET_WM_WINDOW_TYPE_UTILITY",False);
	a_net_wm_pid=XInternAtom(g_dpy,"_NET_WM_PID",False);

	// keyboard: no fake key release events on auto repeat
	Bool supported=False;
	XkbSetDetectableAutoRepeat(g_dpy,True,&supported);

	// input method (for dead keys / compose in text fields)
	XSetLocaleModifiers("");
	g_xim=XOpenIM(g_dpy,nullptr,nullptr,nullptr);

	memset(g_key_down,0,sizeof(g_key_down));

	// desktop pseudo window
	g_desktop=new_window_struct();
	g_windows.erase(g_desktop);
	screen_size((int*)&g_desktop->rect.right,(int*)&g_desktop->rect.bottom);

	register_builtin_classes();
	return true;
}

void adxplat::shutdown(void)
{
	if(g_dpy)
	{
		if(g_xim)
			XCloseIM(g_xim);
		XCloseDisplay(g_dpy);
		g_dpy=nullptr;
	}
}

void* adxplat::x11_display(void)
{
	return g_dpy;
}

unsigned long adxplat::x11_window(HWND hwnd)
{
	Win32Window* top=top_level_of(hwnd);
	return top?top->xwin:0;
}

void adxplat::add_fd_watch(int fd,fd_callback cb,void* user)
{
	g_fd_watches.push_back({fd,cb,user});
}

void adxplat::remove_fd_watch(int fd,void* user)
{
	g_fd_watches.erase(std::remove_if(g_fd_watches.begin(),g_fd_watches.end(),[&](const FdWatch& w){return w.fd==fd && w.user==user;}),g_fd_watches.end());
}

void adxplat::remove_fd_watches(void* user)
{
	g_fd_watches.erase(std::remove_if(g_fd_watches.begin(),g_fd_watches.end(),[&](const FdWatch& w){return w.user==user;}),g_fd_watches.end());
}

// timers for plugin run loops: windows style timers without window, identified by id
struct PlatformTimer
{
	adxplat::timer_callback	cb;
	void*					user;
};
static std::map<UINT_PTR,PlatformTimer> g_platform_timers;
static UINT_PTR g_next_platform_timer=0x7F000000;

static void platform_timer_proc(HWND,UINT,UINT_PTR id,DWORD)
{
	auto it=g_platform_timers.find(id);
	if(it!=g_platform_timers.end())
		it->second.cb(it->second.user);
}

unsigned long adxplat::timer_start(unsigned int interval_ms,timer_callback cb,void* user)
{
	UINT_PTR const id=g_next_platform_timer++;
	g_platform_timers[id]={cb,user};
	SetTimer(nullptr,id,interval_ms,platform_timer_proc);
	return id;
}

void adxplat::timer_stop(unsigned long id)
{
	KillTimer(nullptr,id);
	g_platform_timers.erase(id);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ui scaling helpers

static inline int floor_div(int a,int b)
{
	return a>=0?a/b:-((-a+b-1)/b);
}

static inline bool is_scaled(const Win32Window* w)
{
	return g_scale_num!=g_scale_den && !(w && (w->ex_style & WS_EX_ADX_NATIVE_PIXELS));
}

// screen positions (shared by all windows)
static inline int phys_pos(int v)	{ return floor_div(v*g_scale_num,g_scale_den); }
static inline int log_pos(int v)	{ return floor_div(v*g_scale_den,g_scale_num); }

// window sizes / window local coordinates
static inline int phys_len(const Win32Window* w,int v)	{ return is_scaled(w)?(v*g_scale_num+g_scale_den-1)/g_scale_den:v; }
static inline int log_len(const Win32Window* w,int v)	{ return is_scaled(w)?floor_div(v*g_scale_den,g_scale_num):v; }

void adxplat::set_ui_scale(int percent)
{
	// environment override for testing: AODIX_UI_SCALE=100|150|200
	if(const char* e=getenv("AODIX_UI_SCALE"))
		percent=atoi(e);

	if(percent==150) { g_scale_num=3; g_scale_den=2; }
	else if(percent==200) { g_scale_num=2; g_scale_den=1; }
	else { g_scale_num=1; g_scale_den=1; }

	if(g_desktop)
		screen_size((int*)&g_desktop->rect.right,(int*)&g_desktop->rect.bottom);
}

int adxplat::ui_scale(void)
{
	return g_scale_num*100/g_scale_den;
}

int adxplat::ui_scale_to_logical(int px)
{
	return floor_div(px*g_scale_den,g_scale_num);
}

static void screen_size_phys(int* w,int* h)
{
	if(!g_dpy)
	{
		*w=1024;
		*h=768;
		return;
	}
	*w=DisplayWidth(g_dpy,g_screen);
	*h=DisplayHeight(g_dpy,g_screen);
}

// logical screen size
void screen_size(int* w,int* h)
{
	screen_size_phys(w,h);
	*w=log_pos(*w);
	*h=log_pos(*h);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// window classes
BOOL RegisterClass(const WNDCLASS* wc)
{
	WindowClass* c=new WindowClass();
	c->name=wc->lpszClassName;
	c->proc=wc->lpfnWndProc;
	c->style=wc->style;
	c->cursor=wc->hCursor;
	c->background=wc->hbrBackground;
	c->menu_id=(int)(uintptr_t)wc->lpszMenuName;
	c->hinstance=wc->hInstance;
	g_classes[c->name]=c;
	return TRUE;
}

BOOL RegisterClassEx(const WNDCLASSEX* wc)
{
	WNDCLASS w;
	w.style=wc->style;
	w.lpfnWndProc=wc->lpfnWndProc;
	w.cbClsExtra=0;
	w.cbWndExtra=0;
	w.hInstance=wc->hInstance;
	w.hIcon=wc->hIcon;
	w.hCursor=wc->hCursor;
	w.hbrBackground=wc->hbrBackground;
	w.lpszMenuName=wc->lpszMenuName;
	w.lpszClassName=wc->lpszClassName;
	return RegisterClass(&w);
}

BOOL UnregisterClass(LPCSTR class_name,HINSTANCE)
{
	auto it=g_classes.find(class_name);
	if(it==g_classes.end())
		return FALSE;
	delete it->second;
	g_classes.erase(it);
	return TRUE;
}

WindowClass* find_class(const char* name)
{
	auto it=g_classes.find(name);
	if(it!=g_classes.end())
		return it->second;

	// case insensitive lookup (win32 class names are case insensitive)
	for(auto& kv:g_classes)
		if(strcasecmp(kv.first.c_str(),name)==0)
			return kv.second;

	return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// window helpers
bool is_valid_window(HWND hwnd)
{
	return hwnd && g_windows.count(hwnd) && hwnd->magic==WINDOW_MAGIC && !hwnd->destroyed;
}

Win32Window* top_level_of(Win32Window* w)
{
	if(!w)
		return nullptr;
	while(w->parent)
		w=w->parent;
	return w->xwin?w:nullptr;
}

void client_origin_in_surface(Win32Window* w,int* x,int* y)
{
	int ox=0,oy=0;
	while(w && w->parent)
	{
		ox+=w->rect.left;
		oy+=w->rect.top;
		w=w->parent;
	}
	if(w)
		oy+=w->menubar_h;
	*x=ox;
	*y=oy;
}

static void client_size(Win32Window* w,int* cw,int* ch)
{
	if(w->parent)
	{
		*cw=w->rect.right-w->rect.left;
		*ch=w->rect.bottom-w->rect.top;
	}
	else
	{
		*cw=w->rect.right;
		*ch=w->rect.bottom;
	}
}

void post_internal(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam)
{
	MSG m;
	memset(&m,0,sizeof(m));
	m.hwnd=hwnd;
	m.message=msg;
	m.wParam=wparam;
	m.lParam=lparam;
	m.time=now_ms();
	m.pt.x=g_last_root_x;
	m.pt.y=g_last_root_y;
	g_queue.push_back(m);
}

void invalidate_children(Win32Window* w)
{
	for(Win32Window* c:w->children)
	{
		if(c->visible && !c->destroyed)
		{
			c->invalid=true;
			invalidate_children(c);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// surfaces (backbuffers of top level windows)
static void free_surface(Win32Window* w)
{
	if(w->ximage)
	{
		XImage* img=(XImage*)w->ximage;
		if(w->shminfo)
		{
			XShmSegmentInfo* si=(XShmSegmentInfo*)w->shminfo;
			XShmDetach(g_dpy,si);
			XSync(g_dpy,False);
			shmdt(si->shmaddr);
			delete si;
			w->shminfo=nullptr;
			img->data=nullptr;
		}
		else
		{
			free(img->data);
			img->data=nullptr;
		}
		XDestroyImage(img);
		w->ximage=nullptr;
	}

	if(w->surface)
	{
		// scaled windows draw into their own logical buffer, unscaled ones directly into the ximage
		if(!w->surface->own_pixels)
			w->surface->px=nullptr;
		delete w->surface;
		w->surface=nullptr;
	}
}

// width / height: logical surface size. the ximage has the physical (scaled) size.
static void create_surface(Win32Window* w,int lwidth,int lheight)
{
	free_surface(w);

	lwidth=std::max(lwidth,1);
	lheight=std::max(lheight,1);
	int const width=phys_len(w,lwidth);
	int const height=phys_len(w,lheight);

	XImage* img=nullptr;

	if(g_use_shm)
	{
		XShmSegmentInfo* si=new XShmSegmentInfo();
		img=XShmCreateImage(g_dpy,g_visual,g_depth,ZPixmap,nullptr,si,width,height);
		if(img)
		{
			si->shmid=shmget(IPC_PRIVATE,(size_t)img->bytes_per_line*img->height,IPC_CREAT|0600);
			if(si->shmid>=0)
			{
				si->shmaddr=img->data=(char*)shmat(si->shmid,nullptr,0);
				si->readOnly=False;
				g_shm_error=false;
				XErrorHandler old=XSetErrorHandler(x_shm_error_handler);
				XShmAttach(g_dpy,si);
				XSync(g_dpy,False);
				XSetErrorHandler(old);
				shmctl(si->shmid,IPC_RMID,nullptr);
				if(g_shm_error)
				{
					shmdt(si->shmaddr);
					img->data=nullptr;
					XDestroyImage(img);
					img=nullptr;
					delete si;
					g_use_shm=false;
				}
				else
				{
					w->shminfo=si;
				}
			}
			else
			{
				XDestroyImage(img);
				img=nullptr;
				delete si;
				g_use_shm=false;
			}
		}
		else
		{
			delete si;
			g_use_shm=false;
		}
	}

	if(!img)
	{
		char* data=(char*)calloc((size_t)width*height,4);
		img=XCreateImage(g_dpy,g_visual,g_depth,ZPixmap,0,data,width,height,32,width*4);
	}

	w->ximage=img;
	w->surface=new GdiBitmap();
	w->surface->w=lwidth;
	w->surface->h=lheight;
	if(is_scaled(w))
	{
		w->surface->stride=lwidth;
		w->surface->px=(uint32_t*)calloc((size_t)lwidth*lheight,4);
		w->surface->own_pixels=true;
	}
	else
	{
		w->surface->stride=img->bytes_per_line/4;
		w->surface->px=(uint32_t*)img->data;
		w->surface->own_pixels=false;
	}
	w->has_dirty=false;
}

// exact average of two pixels (per channel, rounded down)
static inline uint32_t px_avg(uint32_t a,uint32_t b)
{
	return (a&b)+(((a^b)&0xFEFEFEFEu)>>1);
}

// copy a logical rectangle of the surface scaled into the ximage. returns the physical rectangle.
static void scale_to_image(Win32Window* w,int x,int y,int x2,int y2,int* px,int* py,int* px2,int* py2)
{
	GdiBitmap* const s=w->surface;
	XImage* const img=(XImage*)w->ximage;
	int const dw=img->width;
	int const dh=img->height;
	int const dstride=img->bytes_per_line/4;
	uint32_t* const d=(uint32_t*)img->data;
	uint32_t const* const src=s->px;
	int const ss=s->stride;

	if(g_scale_num==2 && g_scale_den==1)
	{
		// 200%: every pixel becomes a 2x2 block (crisp)
		for(int sy=y;sy<y2;sy++)
		{
			uint32_t const* sr=src+(size_t)sy*ss;
			uint32_t* d0=d+(size_t)(sy*2)*dstride;
			uint32_t* d1=d0+dstride;
			for(int sx=x;sx<x2;sx++)
			{
				uint32_t const c=sr[sx];
				d0[sx*2]=d0[sx*2+1]=c;
				d1[sx*2]=d1[sx*2+1]=c;
			}
		}
		*px=x*2; *py=y*2; *px2=x2*2; *py2=y2*2;
		return;
	}

	// 150%: every 2x2 block becomes 3x3. corners keep the source pixels, the middle row / column is the average
	x&=~1; y&=~1;
	x2=std::min(s->w,(x2+1)&~1);
	y2=std::min(s->h,(y2+1)&~1);
	for(int sy=y;sy<y2;sy+=2)
	{
		uint32_t const* r0=src+(size_t)sy*ss;
		uint32_t const* r1=(sy+1<s->h)?r0+ss:r0;
		int const dy=sy/2*3;
		for(int sx=x;sx<x2;sx+=2)
		{
			int const sx1=(sx+1<s->w)?sx+1:sx;
			uint32_t const a=r0[sx],b=r0[sx1],c=r1[sx],e=r1[sx1];
			uint32_t const ab=px_avg(a,b),ce=px_avg(c,e);
			uint32_t const blk[3][3]={{a,ab,b},{px_avg(a,c),px_avg(ab,ce),px_avg(b,e)},{c,ce,e}};
			int const dx=sx/2*3;
			for(int j=0;j<3;j++)
			{
				if(dy+j>=dh)
					break;
				uint32_t* dr=d+(size_t)(dy+j)*dstride+dx;
				for(int i=0;i<3 && dx+i<dw;i++)
					dr[i]=blk[j][i];
			}
		}
	}
	*px=x/2*3; *py=y/2*3;
	*px2=std::min(dw,(x2+1)/2*3); *py2=std::min(dh,(y2+1)/2*3);
}

static GC g_gc=0;

static void flush_surface(Win32Window* w)
{
	if(!w->has_dirty || !w->ximage || !w->xwin)
		return;

	w->has_dirty=false;

	if(!w->mapped)
		return;

	int const x=std::max<int>(0,w->dirty.left);
	int const y=std::max<int>(0,w->dirty.top);
	int const x2=std::min<int>(w->surface->w,w->dirty.right);
	int const y2=std::min<int>(w->surface->h,w->dirty.bottom);
	if(x2<=x || y2<=y)
		return;

	if(!g_gc)
		g_gc=XCreateGC(g_dpy,w->xwin,0,nullptr);

	int px=x,py=y,px2=x2,py2=y2;
	if(is_scaled(w))
		scale_to_image(w,x,y,x2,y2,&px,&py,&px2,&py2);
	if(px2<=px || py2<=py)
		return;

	if(w->shminfo)
		XShmPutImage(g_dpy,w->xwin,g_gc,(XImage*)w->ximage,px,py,px,py,px2-px,py2-py,False);
	else
		XPutImage(g_dpy,w->xwin,g_gc,(XImage*)w->ximage,px,py,px,py,px2-px,py2-py);
}

void flush_all_surfaces(void)
{
	bool any=false;
	for(Win32Window* w:g_top_levels)
	{
		if(w->has_dirty)
		{
			flush_surface(w);
			any=true;
		}
	}
	if(any)
	{
		if(g_use_shm)
			XSync(g_dpy,False);
		else
			XFlush(g_dpy);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// window icon from the embedded aodix.ico (24-bit + and-mask)
static void set_window_icon(::Window xw)
{
	const unsigned char* d=res_aodix_ico;
	if(sizeof(res_aodix_ico)<22)
		return;
	int const w=d[6]?d[6]:256;
	int const h=d[7]?d[7]:256;
	uint32_t const ofs=d[18]|(d[19]<<8)|(d[20]<<16)|(d[21]<<24);
	const unsigned char* bmp=d+ofs;
	int const bpp=bmp[14]|(bmp[15]<<8);
	uint32_t const hdr=bmp[0]|(bmp[1]<<8)|(bmp[2]<<16)|(bmp[3]<<24);
	if(bpp!=24 && bpp!=32)
		return;

	std::vector<unsigned long> data(2+w*h);
	data[0]=w;
	data[1]=h;
	const unsigned char* pix=bmp+hdr;
	int const row=((w*bpp+31)/32)*4;
	const unsigned char* mask=pix+row*h;
	int const mrow=((w+31)/32)*4;

	for(int y=0;y<h;y++)
	{
		const unsigned char* s=pix+(h-1-y)*row;
		const unsigned char* m=mask+(h-1-y)*mrow;
		for(int x=0;x<w;x++)
		{
			int const bpx=bpp/8;
			unsigned long const b=s[x*bpx],g=s[x*bpx+1],r=s[x*bpx+2];
			bool const transparent=(m[x>>3]>>(7-(x&7)))&1;
			unsigned long const a=(bpp==32)?s[x*4+3]:(transparent?0:255);
			data[2+y*w+x]=(a<<24)|(r<<16)|(g<<8)|b;
		}
	}

	XChangeProperty(g_dpy,xw,a_net_wm_icon,XA_CARDINAL,32,PropModeReplace,(unsigned char*)data.data(),(int)data.size());
}

// latin-1 / utf-8 tolerant conversion of window titles to utf-8
static std::string to_utf8(const std::string& s)
{
	std::string o;
	size_t i=0;
	while(i<s.size())
	{
		unsigned char const c=s[i];
		if(c<0x80)
		{
			o+=(char)c;
			i++;
			continue;
		}
		// already valid utf-8 sequence?
		int len=0;
		if((c&0xE0)==0xC0) len=2;
		else if((c&0xF0)==0xE0) len=3;
		else if((c&0xF8)==0xF0) len=4;
		bool valid=len>0 && i+len<=s.size();
		for(int k=1;valid && k<len;k++)
			if(((unsigned char)s[i+k]&0xC0)!=0x80)
				valid=false;
		if(valid)
		{
			o.append(s,i,len);
			i+=len;
			continue;
		}
		o+=(char)(0xC0|(c>>6));
		o+=(char)(0x80|(c&0x3F));
		i++;
	}
	return o;
}

static void set_x_title(Win32Window* w)
{
	if(!w->xwin)
		return;
	std::string const u=to_utf8(w->text);
	XChangeProperty(g_dpy,w->xwin,a_net_wm_name,a_utf8,8,PropModeReplace,(const unsigned char*)u.c_str(),(int)u.size());
	XStoreName(g_dpy,w->xwin,w->text.c_str());
}

static void create_x_window(Win32Window* w,int width,int height,bool popup)
{
	XSetWindowAttributes attr;
	memset(&attr,0,sizeof(attr));
	attr.background_pixmap=None;
	attr.border_pixel=0;
	attr.colormap=XCreateColormap(g_dpy,g_root,g_visual,AllocNone);
	attr.override_redirect=popup?True:False;
	attr.save_under=popup?True:False;
	attr.event_mask=ExposureMask|StructureNotifyMask|KeyPressMask|KeyReleaseMask|ButtonPressMask|ButtonReleaseMask|PointerMotionMask|FocusChangeMask|EnterWindowMask|LeaveWindowMask;

	w->xwin=XCreateWindow(g_dpy,g_root,0,0,phys_len(w,std::max(width,1)),phys_len(w,std::max(height,1)),0,g_depth,InputOutput,g_visual,CWBackPixmap|CWBorderPixel|CWColormap|CWOverrideRedirect|CWSaveUnder|CWEventMask,&attr);
	w->is_popup=popup;

	if(!popup)
	{
		XSetWMProtocols(g_dpy,w->xwin,&a_wm_delete,1);

		XClassHint ch;
		ch.res_name=(char*)"aodix";
		ch.res_class=(char*)"Aodix";
		XSetClassHint(g_dpy,w->xwin,&ch);

		long const pid=getpid();
		XChangeProperty(g_dpy,w->xwin,a_net_wm_pid,XA_CARDINAL,32,PropModeReplace,(const unsigned char*)&pid,1);

		set_window_icon(w->xwin);

		if(g_xim)
			w->xic=XCreateIC(g_xim,XNInputStyle,XIMPreeditNothing|XIMStatusNothing,XNClientWindow,w->xwin,XNFocusWindow,w->xwin,nullptr);
	}
	else
	{
		XChangeProperty(g_dpy,w->xwin,a_net_wm_type,XA_ATOM,32,PropModeReplace,(const unsigned char*)&a_net_wm_type_menu,1);
	}

	create_surface(w,width,height);
	g_top_levels.push_back(w);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// window manager size hints: top level windows without WS_THICKFRAME (e.g. plugin editor frames) get a fixed size
static void set_wm_normal_hints(Win32Window* wnd,bool has_pos,int x,int y)
{
	if(!wnd || !wnd->xwin || wnd->parent || wnd->is_popup || wnd->dlg)
		return;
	XSizeHints* hints=XAllocSizeHints();
	hints->flags=0;
	if(has_pos)
	{
		hints->flags|=USPosition|PPosition;
		hints->x=phys_pos(x);
		hints->y=phys_pos(y);
	}
	if(!(wnd->style & WS_THICKFRAME))
	{
		hints->flags|=PMinSize|PMaxSize;
		hints->min_width=hints->max_width=phys_len(wnd,std::max<int>(wnd->rect.right,1));
		hints->min_height=hints->max_height=phys_len(wnd,std::max<int>(wnd->rect.bottom,1)+wnd->menubar_h);
	}
	XSetWMNormalHints(g_dpy,wnd->xwin,hints);
	XFree(hints);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static Win32Window* new_window_struct(void)
{
	Win32Window* w=new Win32Window();
	w->magic=WINDOW_MAGIC;
	w->cls=nullptr;
	w->proc=nullptr;
	w->style=0;
	w->ex_style=0;
	w->parent=nullptr;
	w->owner=nullptr;
	w->id=0;
	w->userdata=0;
	w->hinstance=nullptr;
	w->rect.left=w->rect.top=w->rect.right=w->rect.bottom=0;
	w->visible=false;
	w->enabled=true;
	w->destroyed=false;
	w->menu=nullptr;
	w->menubar_h=0;
	w->menubar_hot=-1;
	w->font=nullptr;
	w->cursor=nullptr;
	w->invalid=true;
	w->xwin=0;
	w->xic=nullptr;
	w->surface=nullptr;
	w->ximage=nullptr;
	w->shminfo=nullptr;
	w->has_dirty=false;
	w->root_x=0;
	w->root_y=0;
	w->root_valid=false;
	w->is_popup=false;
	w->mapped=false;
	w->want_fullscreen=false;
	w->want_maximized=false;
	w->track_placement=false;
	w->have_normal=false;
	w->normal_x=w->normal_y=w->normal_w=w->normal_h=0;
	w->xcursor_current=0;
	w->ctl=nullptr;
	w->dlg=nullptr;
	g_windows.insert(w);
	return w;
}

HWND CreateWindowEx(DWORD ex_style,LPCSTR class_name,LPCSTR window_name,DWORD style,int x,int y,int w,int h,HWND parent,HMENU menu,HINSTANCE hinstance,LPVOID)
{
	WindowClass* cls=find_class(class_name);
	if(!cls)
	{
		fprintf(stderr,"aodix: CreateWindow: unknown class '%s'\n",class_name);
		return nullptr;
	}

	Win32Window* wnd=new_window_struct();
	wnd->cls=cls;
	wnd->proc=cls->proc;
	wnd->style=style;
	wnd->ex_style=ex_style;
	wnd->text=window_name?window_name:"";
	wnd->hinstance=hinstance;
	wnd->cursor=cls->cursor;
	wnd->enabled=!(style & WS_DISABLED);

	bool const is_child=(style & WS_CHILD) && parent;

	if(is_child)
	{
		wnd->parent=parent;
		wnd->id=(int)(uintptr_t)menu;
		wnd->rect.left=x;
		wnd->rect.top=y;
		wnd->rect.right=x+std::max(w,0);
		wnd->rect.bottom=y+std::max(h,0);
		parent->children.push_back(wnd);
		wnd->visible=(style & WS_VISIBLE)!=0;
		wnd->font=parent->font;
	}
	else
	{
		wnd->owner=is_valid_window(parent)?top_level_of(parent):nullptr;

		// menu bar
		if(menu)
			wnd->menu=menu;
		else if(cls->menu_id)
			wnd->menu=load_menu_resource(cls->menu_id);
		wnd->menubar_h=wnd->menu?menubar_height(wnd->menu):0;

		int sw=0,sh=0;
		screen_size(&sw,&sh);
		int cw=(w==CW_USEDEFAULT || w<=0)?std::min(1280,sw):w;
		int chh=(w==CW_USEDEFAULT || h<=0)?std::min(800,sh-64):h;
		wnd->rect.right=cw;
		wnd->rect.bottom=chh;

		create_x_window(wnd,cw,chh+wnd->menubar_h,false);
		set_x_title(wnd);

		if(wnd->owner && wnd->owner->xwin)
			XSetTransientForHint(g_dpy,wnd->xwin,wnd->owner->xwin);

		if(ex_style & WS_EX_TOOLWINDOW)
			XChangeProperty(g_dpy,wnd->xwin,a_net_wm_type,XA_ATOM,32,PropModeReplace,(const unsigned char*)&a_net_wm_type_normal,1);

		// position and (for windows without sizing border) fixed size
		set_wm_normal_hints(wnd,x!=CW_USEDEFAULT,x,y);
		if(x!=CW_USEDEFAULT)
			XMoveWindow(g_dpy,wnd->xwin,phys_pos(x),phys_pos(y));

		wnd->visible=false;
	}

	SendMessage(wnd,WM_CREATE,0,0);

	if(!is_child && (style & WS_VISIBLE))
		ShowWindow(wnd,SW_SHOW);

	if(is_child && wnd->visible)
		wnd->invalid=true;

	return wnd;
}

static void destroy_recursive(Win32Window* w)
{
	if(w->destroyed)
		return;

	SendMessage(w,WM_DESTROY,0,0);

	std::vector<Win32Window*> kids=w->children;
	for(Win32Window* c:kids)
		destroy_recursive(c);

	w->destroyed=true;

	if(g_focus==w) g_focus=nullptr;
	if(g_capture==w) g_capture=nullptr;
	if(g_active_top==w) g_active_top=nullptr;
	if(g_pointer_top==w) g_pointer_top=nullptr;
	if(g_last_click_win==w) g_last_click_win=nullptr;

	// remove timers
	g_timers.erase(std::remove_if(g_timers.begin(),g_timers.end(),[&](const TimerEntry& t){return t.hwnd==w;}),g_timers.end());

	// remove pending messages
	g_queue.erase(std::remove_if(g_queue.begin(),g_queue.end(),[&](const MSG& m){return m.hwnd==w;}),g_queue.end());

	if(w->parent)
	{
		auto& v=w->parent->children;
		v.erase(std::remove(v.begin(),v.end(),w),v.end());
		w->parent->invalid=true;
	}

	if(w->xwin)
	{
		if(w->xic)
			XDestroyIC((XIC)w->xic);
		free_surface(w);
		XDestroyWindow(g_dpy,w->xwin);
		XFlush(g_dpy);
		w->xwin=0;
		g_top_levels.erase(std::remove(g_top_levels.begin(),g_top_levels.end(),w),g_top_levels.end());

		// reactivate owner
		if(w->owner && is_valid_window(w->owner) && w->owner->xwin && w->owner->mapped && !w->is_popup)
		{
			XSetInputFocus(g_dpy,w->owner->xwin,RevertToParent,CurrentTime);
		}
	}

	delete w->ctl;
	w->ctl=nullptr;
	delete w->dlg;
	w->dlg=nullptr;

	g_windows.erase(w);
	w->magic=0;
	// note: the struct itself is intentionally leaked (a few hundred bytes per destroyed window) so that
	// stale handles kept by the application never point to reused memory
}

BOOL DestroyWindow(HWND hwnd)
{
	if(!is_valid_window(hwnd))
		return FALSE;
	destroy_recursive(hwnd);
	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// window placement (main window)
static bool x_window_maximized(::Window xw)
{
	Atom type;
	int format;
	unsigned long n=0,rest;
	unsigned char* data=nullptr;
	bool mv=false,mh=false;
	if(XGetWindowProperty(g_dpy,xw,a_net_wm_state,0,64,False,XA_ATOM,&type,&format,&n,&rest,&data)==Success && data)
	{
		Atom const* a=(Atom const*)data;
		for(unsigned long i=0;i<n;i++)
		{
			if(a[i]==a_net_wm_state_max_v) mv=true;
			if(a[i]==a_net_wm_state_max_h) mh=true;
			if(a[i]==a_net_wm_state_fullscreen) mv=mh=true;
		}
	}
	if(data)
		XFree(data);
	return mv && mh;
}

static void record_normal_placement(Win32Window* w)
{
	if(!w->track_placement || !w->xwin || !w->mapped || x_window_maximized(w->xwin))
		return;
	int rx=0,ry=0;
	::Window child;
	if(!XTranslateCoordinates(g_dpy,w->xwin,g_root,0,0,&rx,&ry,&child))
		return;
	// frame position = client position minus the frame extents of the window manager
	long left=0,top=0;
	Atom type;
	int format;
	unsigned long n=0,rest;
	unsigned char* data=nullptr;
	static Atom a_extents=XInternAtom(g_dpy,"_NET_FRAME_EXTENTS",False);
	if(XGetWindowProperty(g_dpy,w->xwin,a_extents,0,4,False,XA_CARDINAL,&type,&format,&n,&rest,&data)==Success && data && n==4)
	{
		left=((long*)data)[0];
		top=((long*)data)[2];
	}
	if(data)
		XFree(data);
	XWindowAttributes wa;
	if(!XGetWindowAttributes(g_dpy,w->xwin,&wa))
		return;
	w->normal_x=rx-(int)left;
	w->normal_y=ry-(int)top;
	w->normal_w=wa.width;
	w->normal_h=wa.height-phys_len(w,w->menubar_h);
	w->have_normal=true;
}

void adxplat::track_window_placement(HWND hwnd)
{
	if(is_valid_window(hwnd))
		hwnd->track_placement=true;
}

bool adxplat::get_window_placement(HWND hwnd,int* x,int* y,int* w,int* h,bool* maximized)
{
	if(!is_valid_window(hwnd) || !hwnd->xwin)
		return false;
	bool const maxed=x_window_maximized(hwnd->xwin);
	if(!maxed)
		record_normal_placement(hwnd);
	if(!hwnd->have_normal)
		return false;
	*x=hwnd->normal_x;
	*y=hwnd->normal_y;
	*w=hwnd->normal_w;
	*h=hwnd->normal_h;
	*maximized=maxed;
	return true;
}

void adxplat::screen_size_physical(int* w,int* h)
{
	screen_size_phys(w,h);
}

static void set_net_wm_state(Win32Window* w)
{
	std::vector<Atom> st;
	if(w->want_maximized)
	{
		st.push_back(a_net_wm_state_max_v);
		st.push_back(a_net_wm_state_max_h);
	}
	if(w->want_fullscreen)
		st.push_back(a_net_wm_state_fullscreen);
	XChangeProperty(g_dpy,w->xwin,a_net_wm_state,XA_ATOM,32,PropModeReplace,(const unsigned char*)st.data(),(int)st.size());
}

BOOL ShowWindow(HWND hwnd,int cmd)
{
	if(!is_valid_window(hwnd))
		return FALSE;

	bool const was=hwnd->visible;

	if(hwnd->parent)
	{
		hwnd->visible=(cmd!=SW_HIDE);
		hwnd->invalid=true;
		hwnd->parent->invalid=true;
		return was;
	}

	if(cmd==SW_HIDE)
	{
		if(hwnd->xwin)
			XUnmapWindow(g_dpy,hwnd->xwin);
		hwnd->visible=false;
		hwnd->mapped=false;
		return was;
	}

	if(cmd==SW_MAXIMIZE)
	{
		// fullscreen aodix creates a WS_POPUP main window
		if(hwnd->style & WS_POPUP)
			hwnd->want_fullscreen=true;
		else
			hwnd->want_maximized=true;
	}

	if(!hwnd->mapped && hwnd->xwin)
	{
		if(hwnd->want_maximized || hwnd->want_fullscreen)
		{
			set_net_wm_state(hwnd);
			int sw=0,sh=0;
			screen_size_phys(&sw,&sh);
			XResizeWindow(g_dpy,hwnd->xwin,sw,sh);
		}
		XMapRaised(g_dpy,hwnd->xwin);
		hwnd->mapped=true;
		hwnd->has_dirty=true;
		hwnd->dirty.left=hwnd->dirty.top=0;
		hwnd->dirty.right=hwnd->surface->w;
		hwnd->dirty.bottom=hwnd->surface->h;
		XFlush(g_dpy);
	}

	hwnd->visible=true;
	hwnd->invalid=true;
	return was;
}

void adxplat::set_fullscreen(HWND hwnd,bool fullscreen)
{
	if(!is_valid_window(hwnd) || !hwnd->xwin)
		return;

	hwnd->want_fullscreen=fullscreen;
	if(!hwnd->mapped)
	{
		set_net_wm_state(hwnd);
		return;
	}

	XEvent e;
	memset(&e,0,sizeof(e));
	e.xclient.type=ClientMessage;
	e.xclient.window=hwnd->xwin;
	e.xclient.message_type=a_net_wm_state;
	e.xclient.format=32;
	e.xclient.data.l[0]=fullscreen?1:0;
	e.xclient.data.l[1]=a_net_wm_state_fullscreen;
	e.xclient.data.l[3]=1;
	XSendEvent(g_dpy,g_root,False,SubstructureRedirectMask|SubstructureNotifyMask,&e);
	XFlush(g_dpy);
}

BOOL UpdateWindow(HWND hwnd)
{
	if(!is_valid_window(hwnd))
		return FALSE;

	if(hwnd->invalid)
	{
		hwnd->invalid=false;
		SendMessage(hwnd,WM_PAINT,0,0);
		flush_all_surfaces();
	}
	return TRUE;
}

BOOL IsWindow(HWND hwnd)
{
	return is_valid_window(hwnd)?TRUE:FALSE;
}

BOOL EnableWindow(HWND hwnd,BOOL enable)
{
	if(!is_valid_window(hwnd))
		return FALSE;
	bool const was_disabled=!hwnd->enabled;
	hwnd->enabled=enable!=0;
	hwnd->invalid=true;
	if(!enable && g_focus==hwnd)
		g_focus=nullptr;
	return was_disabled;
}

BOOL IsWindowEnabled(HWND hwnd)
{
	return is_valid_window(hwnd) && hwnd->enabled;
}

static bool window_input_enabled(Win32Window* w)
{
	for(;w;w=w->parent)
		if(!w->enabled || !w->visible)
			return false;
	return true;
}

BOOL GetClientRect(HWND hwnd,RECT* r)
{
	if(!r)
		return FALSE;

	r->left=r->top=0;

	if(hwnd==g_desktop || !is_valid_window(hwnd))
	{
		if(hwnd==g_desktop)
		{
			screen_size((int*)&r->right,(int*)&r->bottom);
			return TRUE;
		}
		r->right=r->bottom=0;
		return FALSE;
	}

	int cw=0,ch=0;
	client_size(hwnd,&cw,&ch);
	r->right=cw;
	r->bottom=ch;
	return TRUE;
}

static void update_root_origin(Win32Window* top)
{
	if(!top || !top->xwin)
		return;
	if(top->root_valid)
		return;
	::Window child;
	int rx=0,ry=0;
	XTranslateCoordinates(g_dpy,top->xwin,g_root,0,0,&rx,&ry,&child);
	top->root_x=log_pos(rx);
	top->root_y=log_pos(ry);
	top->root_valid=true;
}

BOOL ClientToScreen(HWND hwnd,POINT* p)
{
	if(!p || !is_valid_window(hwnd))
		return FALSE;
	Win32Window* top=top_level_of(hwnd);
	if(!top)
		return FALSE;
	update_root_origin(top);
	int ox=0,oy=0;
	client_origin_in_surface(hwnd,&ox,&oy);
	p->x+=top->root_x+ox;
	p->y+=top->root_y+oy;
	return TRUE;
}

BOOL ScreenToClient(HWND hwnd,POINT* p)
{
	if(!p || !is_valid_window(hwnd))
		return FALSE;
	Win32Window* top=top_level_of(hwnd);
	if(!top)
		return FALSE;
	update_root_origin(top);
	int ox=0,oy=0;
	client_origin_in_surface(hwnd,&ox,&oy);
	p->x-=top->root_x+ox;
	p->y-=top->root_y+oy;
	return TRUE;
}

BOOL SetWindowPos(HWND hwnd,HWND,int x,int y,int cx,int cy,UINT flags)
{
	if(!is_valid_window(hwnd))
		return FALSE;

	if(hwnd->parent)
	{
		if(!(flags & SWP_NOMOVE))
		{
			int const w=hwnd->rect.right-hwnd->rect.left;
			int const h=hwnd->rect.bottom-hwnd->rect.top;
			hwnd->rect.left=x;
			hwnd->rect.top=y;
			hwnd->rect.right=x+w;
			hwnd->rect.bottom=y+h;
		}
		if(!(flags & SWP_NOSIZE))
		{
			hwnd->rect.right=hwnd->rect.left+cx;
			hwnd->rect.bottom=hwnd->rect.top+cy;
		}
		hwnd->invalid=true;
		hwnd->parent->invalid=true;
		return TRUE;
	}

	if(!hwnd->xwin)
		return FALSE;

	int const pw=phys_len(hwnd,std::max(cx,1));
	int const ph=phys_len(hwnd,std::max(cy,1)+hwnd->menubar_h);
	if(!(flags & SWP_NOMOVE) && !(flags & SWP_NOSIZE))
		XMoveResizeWindow(g_dpy,hwnd->xwin,phys_pos(x),phys_pos(y),pw,ph);
	else if(!(flags & SWP_NOMOVE))
		XMoveWindow(g_dpy,hwnd->xwin,phys_pos(x),phys_pos(y));
	else if(!(flags & SWP_NOSIZE))
		XResizeWindow(g_dpy,hwnd->xwin,pw,ph);

	if(!(flags & SWP_NOSIZE))
	{
		// keep the logical client size in sync immediately, the configure event will confirm it
		hwnd->rect.right=std::max(cx,1);
		hwnd->rect.bottom=std::max(cy,1);
		create_surface(hwnd,hwnd->rect.right,hwnd->rect.bottom+hwnd->menubar_h);
		hwnd->invalid=true;
		if(!(hwnd->style & WS_THICKFRAME))
			set_wm_normal_hints(hwnd,!(flags & SWP_NOMOVE),x,y);
	}

	if(!(flags & SWP_NOZORDER) || ((flags & SWP_NOMOVE) && (flags & SWP_NOSIZE)))
		XRaiseWindow(g_dpy,hwnd->xwin);

	hwnd->root_valid=false;
	XFlush(g_dpy);
	return TRUE;
}

BOOL SetWindowText(HWND hwnd,LPCSTR text)
{
	if(!is_valid_window(hwnd))
		return FALSE;
	SendMessage(hwnd,WM_SETTEXT,0,(LPARAM)(text?text:""));
	return TRUE;
}

int GetWindowText(HWND hwnd,LPSTR text,int max_count)
{
	if(!is_valid_window(hwnd) || !text || max_count<=0)
		return 0;
	return (int)SendMessage(hwnd,WM_GETTEXT,max_count,(LPARAM)text);
}

LONG_PTR SetWindowLongPtr(HWND hwnd,int index,LONG_PTR value)
{
	if(!is_valid_window(hwnd))
		return 0;

	LONG_PTR old=0;
	switch(index)
	{
	case GWL_WNDPROC:	old=(LONG_PTR)hwnd->proc; hwnd->proc=(WNDPROC)value; break;
	case GWL_USERDATA:	old=hwnd->userdata; hwnd->userdata=value; break;
	case GWL_ID:		old=hwnd->id; hwnd->id=(int)value; break;
	case GWL_HINSTANCE:	old=(LONG_PTR)hwnd->hinstance; hwnd->hinstance=(HINSTANCE)value; break;
	}
	return old;
}

LONG_PTR GetWindowLongPtr(HWND hwnd,int index)
{
	if(!is_valid_window(hwnd))
		return 0;

	switch(index)
	{
	case GWL_WNDPROC:	return (LONG_PTR)hwnd->proc;
	case GWL_USERDATA:	return hwnd->userdata;
	case GWL_ID:		return hwnd->id;
	case GWL_HINSTANCE:	return (LONG_PTR)hwnd->hinstance;
	}
	return 0;
}

HWND GetDesktopWindow(void)
{
	return g_desktop;
}

HWND GetParent(HWND hwnd)
{
	if(!is_valid_window(hwnd))
		return nullptr;
	return hwnd->parent?hwnd->parent:hwnd->owner;
}

HMENU GetMenu(HWND hwnd)
{
	if(!is_valid_window(hwnd))
		return nullptr;
	return hwnd->menu;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// painting
BOOL InvalidateRect(HWND hwnd,const RECT*,BOOL)
{
	if(!is_valid_window(hwnd))
	{
		// NULL hwnd: invalidate everything
		if(!hwnd)
			for(Win32Window* w:g_top_levels)
				w->invalid=true;
		return FALSE;
	}
	hwnd->invalid=true;
	return TRUE;
}

HDC GetDC(HWND hwnd)
{
	if(hwnd==nullptr || hwnd==g_desktop)
		return CreateCompatibleDC(nullptr);
	if(!is_valid_window(hwnd))
		return nullptr;
	return create_window_dc(hwnd);
}

int ReleaseDC(HWND,HDC hdc)
{
	destroy_dc(hdc);
	return 1;
}

HDC BeginPaint(HWND hwnd,PAINTSTRUCT* ps)
{
	if(!is_valid_window(hwnd))
		return nullptr;

	hwnd->invalid=false;

	// non client area (menu bar) of top level windows
	if(!hwnd->parent && hwnd->menu)
		menubar_paint(hwnd);

	HDC hdc=create_window_dc(hwnd);
	if(ps)
	{
		ps->hdc=hdc;
		ps->fErase=FALSE;
		ps->rcPaint.left=ps->rcPaint.top=0;
		int cw=0,ch=0;
		client_size(hwnd,&cw,&ch);
		ps->rcPaint.right=cw;
		ps->rcPaint.bottom=ch;
	}
	return hdc;
}

BOOL EndPaint(HWND hwnd,const PAINTSTRUCT* ps)
{
	if(ps)
		destroy_dc(ps->hdc);

	// children have been painted over by the parent, repaint them after
	if(is_valid_window(hwnd))
		invalidate_children(hwnd);

	return TRUE;
}

void repaint_menubar(Win32Window* w)
{
	if(w && w->menu)
	{
		menubar_paint(w);
		flush_all_surfaces();
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// default window procedure
LRESULT DefWindowProc(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam)
{
	if(!is_valid_window(hwnd))
		return 0;

	switch(msg)
	{
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;

	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc=BeginPaint(hwnd,&ps);
			if(hwnd->cls && hwnd->cls->background)
				FillRect(hdc,&ps.rcPaint,hwnd->cls->background);
			EndPaint(hwnd,&ps);
			return 0;
		}

	case WM_SETTEXT:
		hwnd->text=lparam?(const char*)lparam:"";
		if(!hwnd->parent)
			set_x_title(hwnd);
		hwnd->invalid=true;
		return TRUE;

	case WM_GETTEXT:
		{
			char* buf=(char*)lparam;
			int const max_count=(int)wparam;
			if(!buf || max_count<=0)
				return 0;
			int const n=std::min<int>((int)hwnd->text.size(),max_count-1);
			memcpy(buf,hwnd->text.c_str(),n);
			buf[n]=0;
			return n;
		}

	case WM_GETTEXTLENGTH:
		return (LRESULT)hwnd->text.size();

	case WM_SETFONT:
		hwnd->font=(HFONT)wparam;
		if(lparam)
			hwnd->invalid=true;
		return 0;

	case WM_GETFONT:
		return (LRESULT)hwnd->font;

	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORSTATIC:
		return 0;

	case WM_MOUSEWHEEL:
		// forward to parent like windows does
		if(hwnd->parent)
			return SendMessage(hwnd->parent,msg,wparam,lparam);
		return 0;
	}

	return 0;
}

LRESULT CallWindowProc(WNDPROC proc,HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam)
{
	if(!proc)
		return DefWindowProc(hwnd,msg,wparam,lparam);
	return proc(hwnd,msg,wparam,lparam);
}

LRESULT SendMessage(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam)
{
	if(!is_valid_window(hwnd))
		return 0;
	if(!hwnd->proc)
		return DefWindowProc(hwnd,msg,wparam,lparam);
	return hwnd->proc(hwnd,msg,wparam,lparam);
}

BOOL PostMessage(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam)
{
	post_internal(hwnd,msg,wparam,lparam);
	return TRUE;
}

void PostQuitMessage(int exit_code)
{
	g_quit=true;
	g_quit_code=exit_code;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// focus / capture / keyboard state
HWND SetFocus(HWND hwnd)
{
	Win32Window* old=g_focus;
	if(hwnd && !is_valid_window(hwnd))
		return old;
	if(old==hwnd)
		return old;

	g_focus=hwnd;

	if(is_valid_window(old))
	{
		SendMessage(old,WM_KILLFOCUS,(WPARAM)hwnd,0);
		old->invalid=true;
	}
	if(hwnd)
	{
		SendMessage(hwnd,WM_SETFOCUS,(WPARAM)old,0);
		hwnd->invalid=true;
	}
	return old;
}

HWND GetFocus(void)
{
	return is_valid_window(g_focus)?g_focus:nullptr;
}

HWND SetCapture(HWND hwnd)
{
	Win32Window* old=g_capture;
	g_capture=is_valid_window(hwnd)?hwnd:nullptr;
	return old;
}

BOOL ReleaseCapture(void)
{
	g_capture=nullptr;
	return TRUE;
}

BOOL GetCursorPos(POINT* p)
{
	if(!p)
		return FALSE;

	if(!g_have_pointer && g_dpy)
	{
		::Window r,c;
		int rx=0,ry=0,wx=0,wy=0;
		unsigned int mask=0;
		XQueryPointer(g_dpy,g_root,&r,&c,&rx,&ry,&wx,&wy,&mask);
		g_last_root_x=log_pos(rx);
		g_last_root_y=log_pos(ry);
	}

	p->x=g_last_root_x;
	p->y=g_last_root_y;
	return TRUE;
}

short GetKeyState(int vkey)
{
	vkey&=0xFF;
	bool down=g_key_down[vkey];

	// modifier state from the last x event is authoritative
	if(vkey==VK_SHIFT)
		down=(g_mod_state & ShiftMask)!=0 || g_key_down[VK_SHIFT];
	if(vkey==VK_CONTROL)
		down=(g_mod_state & ControlMask)!=0 || g_key_down[VK_CONTROL];
	if(vkey==VK_MENU)
		down=(g_mod_state & Mod1Mask)!=0 || g_key_down[VK_MENU];
	if(vkey==VK_LBUTTON)
		down=(g_mod_state & Button1Mask)!=0;

	return down?(short)0x8000:0;
}

short GetAsyncKeyState(int vkey)
{
	return GetKeyState(vkey);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// timers
UINT_PTR SetTimer(HWND hwnd,UINT_PTR id,UINT elapse_ms,TIMERPROC proc)
{
	for(TimerEntry& t:g_timers)
	{
		if(t.hwnd==hwnd && t.id==id)
		{
			t.interval=std::max<UINT>(elapse_ms,1);
			t.due=now_ms()+t.interval;
			t.proc=proc;
			return id;
		}
	}
	TimerEntry t;
	t.hwnd=hwnd;
	t.id=id;
	t.interval=std::max<UINT>(elapse_ms,1);
	t.due=now_ms()+t.interval;
	t.proc=proc;
	t.pending=false;
	g_timers.push_back(t);
	return id;
}

BOOL KillTimer(HWND hwnd,UINT_PTR id)
{
	size_t const n=g_timers.size();
	g_timers.erase(std::remove_if(g_timers.begin(),g_timers.end(),[&](const TimerEntry& t){return t.hwnd==hwnd && t.id==id;}),g_timers.end());
	g_queue.erase(std::remove_if(g_queue.begin(),g_queue.end(),[&](const MSG& m){return m.hwnd==hwnd && m.message==WM_TIMER && m.wParam==id;}),g_queue.end());
	return g_timers.size()!=n;
}

// post due timers, returns true if any was posted
static bool check_timers(void)
{
	DWORD const t=now_ms();
	bool any=false;
	for(TimerEntry& e:g_timers)
	{
		if((int32_t)(t-e.due)>=0 && !e.pending)
		{
			e.pending=true;
			e.due=t+e.interval;
			post_internal(e.hwnd,WM_TIMER,e.id,(LPARAM)e.proc);
			any=true;
		}
	}
	return any;
}

static int ms_to_next_timer(void)
{
	if(g_timers.empty())
		return 1000;
	DWORD const t=now_ms();
	int best=1000;
	for(const TimerEntry& e:g_timers)
	{
		if(e.pending)
			continue;
		int const d=(int)(int32_t)(e.due-t);
		best=std::min(best,std::max(d,0));
	}
	return best;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// cursors
static unsigned long load_cur_resource(const unsigned char* d,size_t size)
{
	if(size<22+40)
		return 0;
	int const hot_x=d[10]|(d[11]<<8);
	int const hot_y=d[12]|(d[13]<<8);
	uint32_t const ofs=d[18]|(d[19]<<8)|(d[20]<<16)|(d[21]<<24);
	const unsigned char* bmp=d+ofs;
	uint32_t const hdr=bmp[0]|(bmp[1]<<8)|(bmp[2]<<16)|(bmp[3]<<24);
	int const w=bmp[4]|(bmp[5]<<8);
	int const h2=bmp[8]|(bmp[9]<<8);
	int const bpp=bmp[14]|(bmp[15]<<8);
	if(bpp!=1)
		return 0;
	int const h=h2/2;
	const unsigned char* xorbits=bmp+hdr+8;		// skip 2-entry palette
	int const row=((w+31)/32)*4;
	const unsigned char* andbits=xorbits+row*h;

	int const xrow=(w+7)/8;
	std::vector<char> src(xrow*h,0),mask(xrow*h,0);
	for(int y=0;y<h;y++)
	{
		for(int x=0;x<w;x++)
		{
			int const sy=h-1-y;
			bool const xo=(xorbits[sy*row+(x>>3)]>>(7-(x&7)))&1;
			bool const an=(andbits[sy*row+(x>>3)]>>(7-(x&7)))&1;
			bool const visible=!an || xo;
			bool const white=xo && !an;
			if(visible) mask[y*xrow+(x>>3)]|=1<<(x&7);
			if(white) src[y*xrow+(x>>3)]|=1<<(x&7);
		}
	}

	Pixmap ps=XCreateBitmapFromData(g_dpy,g_root,src.data(),w,h);
	Pixmap pm=XCreateBitmapFromData(g_dpy,g_root,mask.data(),w,h);
	XColor fg,bg;
	fg.red=fg.green=fg.blue=0xFFFF;
	bg.red=bg.green=bg.blue=0;
	fg.flags=bg.flags=DoRed|DoGreen|DoBlue;
	Cursor c=XCreatePixmapCursor(g_dpy,ps,pm,&fg,&bg,hot_x,hot_y);
	XFreePixmap(g_dpy,ps);
	XFreePixmap(g_dpy,pm);
	return c;
}

HCURSOR LoadCursor(HINSTANCE hinstance,LPCSTR name)
{
	if(!g_dpy)
		return nullptr;

	int const id=(int)(uintptr_t)name;
	unsigned long xc=0;

	// aodix custom cursors (IDC_HO=110, IDC_HC=111); system cursor ids are >= 32512
	if(id<32512)
	{
		if(id==110)
			xc=load_cur_resource(res_ho_cur,sizeof(res_ho_cur));
		else if(id==111)
			xc=load_cur_resource(res_hc_cur,sizeof(res_hc_cur));
		if(!xc)
			xc=XCreateFontCursor(g_dpy,XC_hand2);
	}
	else
	{
		unsigned int shape=XC_left_ptr;
		switch(id)
		{
		case 32512:	shape=XC_left_ptr;				break;
		case 32513:	shape=XC_xterm;					break;
		case 32514:	shape=XC_watch;					break;
		case 32642:	shape=XC_bottom_right_corner;	break;
		case 32643:	shape=XC_bottom_left_corner;	break;
		case 32644:	shape=XC_sb_h_double_arrow;		break;
		case 32645:	shape=XC_sb_v_double_arrow;		break;
		case 32646:	shape=XC_fleur;					break;
		case 32649:	shape=XC_hand2;					break;
		}
		xc=XCreateFontCursor(g_dpy,shape);
	}
	(void)hinstance;

	Win32Cursor* c=new Win32Cursor();
	c->xcursor=xc;
	c->stock=false;
	return c;
}

static void apply_cursor(Win32Window* top,HCURSOR cursor)
{
	if(!top || !top->xwin || !cursor)
		return;
	if(top->xcursor_current==cursor->xcursor)
		return;
	top->xcursor_current=cursor->xcursor;
	XDefineCursor(g_dpy,top->xwin,cursor->xcursor);
	XFlush(g_dpy);
}

static HCURSOR g_current_cursor=nullptr;

HCURSOR SetCursor(HCURSOR cursor)
{
	HCURSOR const old=g_current_cursor;
	g_current_cursor=cursor;
	g_cursor_set_in_dispatch=true;

	Win32Window* top=g_pointer_top?g_pointer_top:g_active_top;
	if(!top && !g_top_levels.empty())
		top=g_top_levels.front();
	apply_cursor(top,cursor);
	return old;
}

void adxplat::set_window_cursor(HWND hwnd,HCURSOR cursor)
{
	apply_cursor(top_level_of(hwnd),cursor);
}

BOOL DestroyCursor(HCURSOR cursor)
{
	if(cursor && g_dpy && cursor->xcursor)
		XFreeCursor(g_dpy,cursor->xcursor);
	delete cursor;
	return TRUE;
}

HICON LoadIcon(HINSTANCE,LPCSTR)
{
	return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// popup surfaces (menus, combo box dropdowns)
Win32Window* create_popup_surface(Win32Window* owner,int x,int y,int w,int h)
{
	Win32Window* p=new_window_struct();
	static WindowClass popup_class={"#popup",nullptr,0,nullptr,nullptr,0,nullptr};
	p->cls=&popup_class;
	p->proc=nullptr;
	p->owner=owner;
	p->rect.right=w;
	p->rect.bottom=h;
	create_x_window(p,w,h,true);
	XMoveWindow(g_dpy,p->xwin,phys_pos(x),phys_pos(y));
	XMapRaised(g_dpy,p->xwin);
	p->mapped=true;
	p->visible=true;
	p->invalid=false;
	XFlush(g_dpy);
	return p;
}

void move_popup_surface(Win32Window* w,int x,int y,int width,int height)
{
	if(!w || !w->xwin)
		return;
	XMoveResizeWindow(g_dpy,w->xwin,phys_pos(x),phys_pos(y),phys_len(w,std::max(width,1)),phys_len(w,std::max(height,1)));
	if(width!=w->surface->w || height!=w->surface->h)
	{
		create_surface(w,width,height);
		w->rect.right=width;
		w->rect.bottom=height;
	}
}

void destroy_popup_surface(Win32Window* w)
{
	DestroyWindow(w);
}

bool grab_input(Win32Window* popup)
{
	if(!popup || !popup->xwin)
		return false;

	for(int tries=0;tries<20;tries++)
	{
		int const r=XGrabPointer(g_dpy,popup->xwin,True,ButtonPressMask|ButtonReleaseMask|PointerMotionMask,GrabModeAsync,GrabModeAsync,None,None,CurrentTime);
		if(r==GrabSuccess)
			break;
		usleep(5000);
	}
	XGrabKeyboard(g_dpy,popup->xwin,True,GrabModeAsync,GrabModeAsync,CurrentTime);
	XFlush(g_dpy);
	return true;
}

void ungrab_input(void)
{
	XUngrabPointer(g_dpy,CurrentTime);
	XUngrabKeyboard(g_dpy,CurrentTime);
	XFlush(g_dpy);
}

void set_input_hook(input_hook_fn fn,void* user)
{
	g_input_hook=fn;
	g_input_hook_user=user;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// keyboard translation
int x11_keysym_to_vk(unsigned long ks)
{
	if(ks>=XK_a && ks<=XK_z)
		return 'A'+(int)(ks-XK_a);
	if(ks>=XK_A && ks<=XK_Z)
		return 'A'+(int)(ks-XK_A);
	if(ks>=XK_0 && ks<=XK_9)
		return '0'+(int)(ks-XK_0);
	if(ks>=XK_F1 && ks<=XK_F12)
		return VK_F1+(int)(ks-XK_F1);
	if(ks>=XK_KP_0 && ks<=XK_KP_9)
		return VK_NUMPAD0+(int)(ks-XK_KP_0);

	switch(ks)
	{
	case XK_BackSpace:		return VK_BACK;
	case XK_Tab:
	case XK_ISO_Left_Tab:	return VK_TAB;
	case XK_Return:
	case XK_KP_Enter:		return VK_RETURN;
	case XK_Shift_L:
	case XK_Shift_R:		return VK_SHIFT;
	case XK_Control_L:
	case XK_Control_R:		return VK_CONTROL;
	case XK_Alt_L:
	case XK_Alt_R:
	case XK_ISO_Level3_Shift: return VK_MENU;
	case XK_Pause:			return VK_PAUSE;
	case XK_Caps_Lock:		return VK_CAPITAL;
	case XK_Escape:			return VK_ESCAPE;
	case XK_space:			return VK_SPACE;
	case XK_Prior:
	case XK_KP_Prior:		return VK_PRIOR;
	case XK_Next:
	case XK_KP_Next:		return VK_NEXT;
	case XK_End:
	case XK_KP_End:			return VK_END;
	case XK_Home:
	case XK_KP_Home:		return VK_HOME;
	case XK_Left:
	case XK_KP_Left:		return VK_LEFT;
	case XK_Up:
	case XK_KP_Up:			return VK_UP;
	case XK_Right:
	case XK_KP_Right:		return VK_RIGHT;
	case XK_Down:
	case XK_KP_Down:		return VK_DOWN;
	case XK_Insert:
	case XK_KP_Insert:		return VK_INSERT;
	case XK_Delete:
	case XK_KP_Delete:		return VK_DELETE;
	case XK_KP_Multiply:	return VK_MULTIPLY;
	case XK_KP_Add:			return VK_ADD;
	case XK_KP_Subtract:	return VK_SUBTRACT;
	case XK_KP_Decimal:
	case XK_KP_Separator:	return VK_DECIMAL;
	case XK_KP_Divide:		return VK_DIVIDE;
	case XK_Num_Lock:		return VK_NUMLOCK;
	case XK_Scroll_Lock:	return VK_SCROLL;
	// oem keys (windows maps them per layout, the common ones are matched by character here)
	case XK_plus:
	case XK_equal:			return VK_OEM_PLUS;
	case XK_comma:			return VK_OEM_COMMA;
	case XK_minus:			return VK_OEM_MINUS;
	case XK_period:			return VK_OEM_PERIOD;
	case XK_semicolon:
	case XK_udiaeresis:		return VK_OEM_1;
	case XK_slash:
	case XK_numbersign:		return VK_OEM_2;
	case XK_grave:
	case XK_odiaeresis:		return VK_OEM_3;
	case XK_bracketleft:
	case XK_ssharp:			return VK_OEM_4;
	case XK_backslash:
	case XK_asciicircum:
	case XK_dead_circumflex: return VK_OEM_5;
	case XK_bracketright:
	case XK_dead_acute:		return VK_OEM_6;
	case XK_apostrophe:
	case XK_adiaeresis:		return VK_OEM_7;
	case XK_less:			return VK_OEM_102;
	}
	return 0;
}

// the vk for a key event: unshifted keysym of the key (like windows virtual keys)
static int key_event_vk(XKeyEvent* ke)
{
	KeySym ks=XLookupKeysym(ke,0);
	int vk=x11_keysym_to_vk(ks);
	if(!vk)
	{
		// try the shifted symbol (some layouts put the base symbol on level 2)
		ks=XLookupKeysym(ke,1);
		vk=x11_keysym_to_vk(ks);
	}
	// keypad with numlock off delivers navigation keysyms at level 0, with numlock on the digits at level 1
	if(ks>=XK_KP_Space && ks<=XK_KP_9)
	{
		KeySym const k1=XLookupKeysym(ke,(ke->state & Mod2Mask)?1:0);
		int const v1=x11_keysym_to_vk(k1);
		if(v1)
			vk=v1;
	}
	return vk;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// x event translation
static Win32Window* find_top_by_xwin(::Window xw)
{
	for(Win32Window* w:g_top_levels)
		if(w->xwin==xw)
			return w;
	return nullptr;
}

static Win32Window* child_at(Win32Window* w,int cx,int cy)
{
	// last child is top-most
	for(int i=(int)w->children.size()-1;i>=0;i--)
	{
		Win32Window* c=w->children[i];
		if(!c->visible || c->destroyed)
			continue;
		if(cx>=c->rect.left && cx<c->rect.right && cy>=c->rect.top && cy<c->rect.bottom)
			return child_at(c,cx-c->rect.left,cy-c->rect.top);
	}
	return w;
}

static void surface_to_client(Win32Window* w,int sx,int sy,int* cx,int* cy)
{
	int ox=0,oy=0;
	client_origin_in_surface(w,&ox,&oy);
	*cx=sx-ox;
	*cy=sy-oy;
}

static WPARAM mouse_key_state(unsigned int state)
{
	WPARAM k=0;
	if(state & Button1Mask) k|=0x0001;
	if(state & Button3Mask) k|=0x0002;
	if(state & ShiftMask) k|=0x0004;
	if(state & ControlMask) k|=0x0008;
	if(state & Button2Mask) k|=0x0010;
	return k;
}

static void handle_pointer_event(XEvent& ev,Win32Window* top)
{
	int sx=0,sy=0;
	unsigned int state=0;
	if(ev.type==MotionNotify)
	{
		sx=ev.xmotion.x;
		sy=ev.xmotion.y;
		state=ev.xmotion.state;
		g_last_root_x=ev.xmotion.x_root;
		g_last_root_y=ev.xmotion.y_root;
		top->root_x=g_event_origin_x;
		top->root_y=g_event_origin_y;
	}
	else
	{
		sx=ev.xbutton.x;
		sy=ev.xbutton.y;
		state=ev.xbutton.state;
		g_last_root_x=ev.xbutton.x_root;
		g_last_root_y=ev.xbutton.y_root;
		top->root_x=g_event_origin_x;
		top->root_y=g_event_origin_y;
	}
	top->root_valid=true;
	g_have_pointer=true;
	g_pointer_top=top;

	// update button state for GetKeyState(VK_LBUTTON)
	g_mod_state=state;
	if(ev.type==ButtonPress && ev.xbutton.button==Button1)
		g_mod_state|=Button1Mask;
	if(ev.type==ButtonRelease && ev.xbutton.button==Button1)
		g_mod_state&=~Button1Mask;

	// menu bar
	if(top->menu && !g_capture && sy<top->menubar_h)
	{
		if(ev.type==ButtonPress && ev.xbutton.button==Button1 && window_input_enabled(top))
			menubar_click(top,sx,sy);
		else if(ev.type==MotionNotify)
		{
			int const hot=menubar_hit(top,sx,sy);
			if(hot!=top->menubar_hot)
			{
				top->menubar_hot=hot;
				repaint_menubar(top);
			}
		}
		return;
	}
	if(top->menu && top->menubar_hot>=0)
	{
		top->menubar_hot=-1;
		repaint_menubar(top);
	}

	// target window
	Win32Window* target=nullptr;
	if(g_capture && top_level_of(g_capture)==top)
		target=g_capture;
	else
	{
		int cx=0,cy=0;
		surface_to_client(top,sx,sy,&cx,&cy);
		target=child_at(top,cx,cy);
	}

	if(!target || !window_input_enabled(top_level_of(target)) || (!g_capture && !window_input_enabled(target)))
		return;

	int cx=0,cy=0;
	surface_to_client(target,sx,sy,&cx,&cy);
	LPARAM const lp=MAKELPARAM((short)cx,(short)cy);
	WPARAM const keys=mouse_key_state(g_mod_state);

	if(ev.type==MotionNotify)
	{
		// coalesce consecutive motion messages for the same window
		if(!g_queue.empty() && g_queue.back().message==WM_MOUSEMOVE && g_queue.back().hwnd==target)
			g_queue.pop_back();
		post_internal(target,WM_MOUSEMOVE,keys,lp);
		return;
	}

	unsigned int const button=ev.xbutton.button;

	if(ev.type==ButtonPress)
	{
		if(button==Button4 || button==Button5)
		{
			short const delta=(button==Button4)?120:-120;
			post_internal(target,WM_MOUSEWHEEL,MAKEWPARAM(keys,delta),MAKELPARAM(g_last_root_x,g_last_root_y));
			return;
		}
		if(button==Button1)
		{
			DWORD const t=ev.xbutton.time;
			bool const dbl=target->cls && (target->cls->style & CS_DBLCLKS) && target==g_last_click_win && (t-g_last_click_time)<500 && abs(sx-g_last_click_x)<4 && abs(sy-g_last_click_y)<4;
			if(dbl)
			{
				post_internal(target,WM_LBUTTONDBLCLK,keys,lp);
				g_last_click_win=nullptr;
			}
			else
			{
				post_internal(target,WM_LBUTTONDOWN,keys,lp);
				g_last_click_win=target;
				g_last_click_time=t;
				g_last_click_x=sx;
				g_last_click_y=sy;
			}
			return;
		}
		if(button==Button3)
			post_internal(target,WM_RBUTTONDOWN,keys,lp);
		if(button==Button2)
			post_internal(target,WM_MBUTTONDOWN,keys,lp);
		return;
	}

	if(ev.type==ButtonRelease)
	{
		if(button==Button1)
			post_internal(target,WM_LBUTTONUP,keys,lp);
		if(button==Button3)
			post_internal(target,WM_RBUTTONUP,keys,lp);
		if(button==Button2)
			post_internal(target,WM_MBUTTONUP,keys,lp);
	}
}

static void handle_key_event(XEvent& ev,Win32Window* top)
{
	XKeyEvent* ke=&ev.xkey;
	g_mod_state=ke->state;

	int const vk=key_event_vk(ke);
	bool const down=(ev.type==KeyPress);

	if(vk==VK_SHIFT) { if(down) g_mod_state|=ShiftMask; else g_mod_state&=~ShiftMask; }
	if(vk==VK_CONTROL) { if(down) g_mod_state|=ControlMask; else g_mod_state&=~ControlMask; }

	if(!window_input_enabled(top))
	{
		if(vk) g_key_down[vk&0xFF]=down;
		return;
	}

	// translated text
	DWORD ch=0;
	if(down)
	{
		char buf[16];
		KeySym ks=0;
		int n=0;
		if(top->xic)
		{
			Status st;
			n=Xutf8LookupString((XIC)top->xic,ke,buf,sizeof(buf)-1,&ks,&st);
			if(st!=XLookupChars && st!=XLookupBoth)
				n=0;
		}
		else
			n=XLookupString(ke,buf,sizeof(buf)-1,&ks,nullptr);
		if(n>0)
		{
			// decode utf-8 to a code point (only latin-1 range is used by the text fields)
			unsigned char const c0=buf[0];
			if(c0<0x80) ch=c0;
			else if((c0&0xE0)==0xC0 && n>=2) ch=((c0&0x1F)<<6)|(buf[1]&0x3F);
			else if((c0&0xF0)==0xE0 && n>=3) ch=((c0&0x0F)<<12)|((buf[1]&0x3F)<<6)|(buf[2]&0x3F);
		}
	}

	if(!vk && !ch)
		return;

	// windows: lparam bit 30 = previous key state (auto repeat), bit 31 = transition state
	LPARAM lp=1;
	if(vk && down && g_key_down[vk&0xFF])
		lp|=(1<<30);
	if(!down)
		lp|=(1<<30)|((LPARAM)1<<31);

	if(vk)
		g_key_down[vk&0xFF]=down;

	Win32Window* target=(g_focus && top_level_of(g_focus)==top && window_input_enabled(g_focus))?g_focus:top;

	UINT const msg=down?WM_KEYDOWN:WM_KEYUP;
	post_internal(target,msg,vk,lp);
	g_queue.back().ch=ch;
}

// convert the physical coordinates of an x event to logical ones (ui scaling)
static void scale_x_event(XEvent& ev)
{
	if(g_scale_num==g_scale_den)
	{
		if(ev.type==MotionNotify) { g_event_origin_x=ev.xmotion.x_root-ev.xmotion.x; g_event_origin_y=ev.xmotion.y_root-ev.xmotion.y; }
		else if(ev.type==ButtonPress || ev.type==ButtonRelease) { g_event_origin_x=ev.xbutton.x_root-ev.xbutton.x; g_event_origin_y=ev.xbutton.y_root-ev.xbutton.y; }
		return;
	}

	Win32Window* const top=find_top_by_xwin(ev.xany.window);

	switch(ev.type)
	{
	case MotionNotify:
		g_event_origin_x=log_pos(ev.xmotion.x_root-ev.xmotion.x);
		g_event_origin_y=log_pos(ev.xmotion.y_root-ev.xmotion.y);
		ev.xmotion.x=log_len(top,ev.xmotion.x);
		ev.xmotion.y=log_len(top,ev.xmotion.y);
		ev.xmotion.x_root=log_pos(ev.xmotion.x_root);
		ev.xmotion.y_root=log_pos(ev.xmotion.y_root);
		break;
	case ButtonPress:
	case ButtonRelease:
		g_event_origin_x=log_pos(ev.xbutton.x_root-ev.xbutton.x);
		g_event_origin_y=log_pos(ev.xbutton.y_root-ev.xbutton.y);
		ev.xbutton.x=log_len(top,ev.xbutton.x);
		ev.xbutton.y=log_len(top,ev.xbutton.y);
		ev.xbutton.x_root=log_pos(ev.xbutton.x_root);
		ev.xbutton.y_root=log_pos(ev.xbutton.y_root);
		break;
	case EnterNotify:
	case LeaveNotify:
		ev.xcrossing.x=log_len(top,ev.xcrossing.x);
		ev.xcrossing.y=log_len(top,ev.xcrossing.y);
		ev.xcrossing.x_root=log_pos(ev.xcrossing.x_root);
		ev.xcrossing.y_root=log_pos(ev.xcrossing.y_root);
		break;
	case Expose:
		if(is_scaled(top))
		{
			// grow the rectangle so that it covers all touched logical pixels
			int const x1=floor_div(ev.xexpose.x*g_scale_den,g_scale_num);
			int const y1=floor_div(ev.xexpose.y*g_scale_den,g_scale_num);
			int const x2=((ev.xexpose.x+ev.xexpose.width)*g_scale_den+g_scale_num-1)/g_scale_num;
			int const y2=((ev.xexpose.y+ev.xexpose.height)*g_scale_den+g_scale_num-1)/g_scale_num;
			ev.xexpose.x=x1;
			ev.xexpose.y=y1;
			ev.xexpose.width=x2-x1;
			ev.xexpose.height=y2-y1;
		}
		break;
	case ConfigureNotify:
		ev.xconfigure.width=log_len(top,ev.xconfigure.width);
		ev.xconfigure.height=log_len(top,ev.xconfigure.height);
		break;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// clipboard: aodix owns the selection with a hidden window and serves the text on request (SelectionRequest);
// reading converts the selection of another program to UTF8_STRING (or STRING) and waits for the answer
static ::Window		g_clip_win=0;
static Atom			a_clipboard=0,a_targets=0,a_text=0,a_clip_prop=0,a_incr=0;
static std::string	g_clip_text[2];		// [0] CLIPBOARD, [1] PRIMARY
static bool			g_clip_owned[2]={false,false};

static bool clip_init(void)
{
	if(!g_dpy)
		return false;
	if(!g_clip_win)
	{
		g_clip_win=XCreateSimpleWindow(g_dpy,g_root,-10,-10,1,1,0,0,0);
		XSelectInput(g_dpy,g_clip_win,PropertyChangeMask);
		a_clipboard=XInternAtom(g_dpy,"CLIPBOARD",False);
		a_targets=XInternAtom(g_dpy,"TARGETS",False);
		a_text=XInternAtom(g_dpy,"TEXT",False);
		a_clip_prop=XInternAtom(g_dpy,"AODIX_CLIPBOARD",False);
		a_incr=XInternAtom(g_dpy,"INCR",False);
	}
	return true;
}

static int clip_index(Atom sel)
{
	if(sel==a_clipboard)
		return 0;
	if(sel==XA_PRIMARY)
		return 1;
	return -1;
}

// returns true if the event was a selection event
static bool clip_handle_event(XEvent& ev)
{
	if(!g_clip_win || ev.xany.window!=g_clip_win)
		return false;

	if(ev.type==SelectionClear)
	{
		int const i=clip_index(ev.xselectionclear.selection);
		if(i>=0)
			g_clip_owned[i]=false;
		return true;
	}

	if(ev.type==SelectionRequest)
	{
		XSelectionRequestEvent const& rq=ev.xselectionrequest;
		XSelectionEvent ans;
		memset(&ans,0,sizeof(ans));
		ans.type=SelectionNotify;
		ans.display=rq.display;
		ans.requestor=rq.requestor;
		ans.selection=rq.selection;
		ans.target=rq.target;
		ans.time=rq.time;
		ans.property=None;

		int const i=clip_index(rq.selection);
		Atom const prop=(rq.property!=None)?rq.property:rq.target;	// obsolete clients pass no property
		if(i>=0 && g_clip_owned[i])
		{
			if(rq.target==a_targets)
			{
				Atom const targets[]={a_targets,a_utf8,XA_STRING,a_text};
				XChangeProperty(g_dpy,rq.requestor,prop,XA_ATOM,32,PropModeReplace,(const unsigned char*)targets,4);
				ans.property=prop;
			}
			else if(rq.target==a_utf8 || rq.target==a_text)
			{
				std::string const& t=g_clip_text[i];
				XChangeProperty(g_dpy,rq.requestor,prop,a_utf8,8,PropModeReplace,(const unsigned char*)t.data(),(int)t.size());
				ans.property=prop;
			}
			else if(rq.target==XA_STRING)
			{
				// latin-1 for old clients
				std::string l;
				std::string const& t=g_clip_text[i];
				for(size_t k=0;k<t.size();k++)
				{
					unsigned char const c=(unsigned char)t[k];
					if(c<0x80)
						l+=(char)c;
					else if((c&0xE0)==0xC0 && k+1<t.size())
					{
						unsigned const cp=((c&0x1F)<<6)|((unsigned char)t[k+1]&0x3F);
						l+=(char)(cp<0x100?cp:'?');
						k++;
					}
					else
					{
						l+='?';
						while(k+1<t.size() && ((unsigned char)t[k+1]&0xC0)==0x80)
							k++;
					}
				}
				XChangeProperty(g_dpy,rq.requestor,prop,XA_STRING,8,PropModeReplace,(const unsigned char*)l.data(),(int)l.size());
				ans.property=prop;
			}
		}
		XSendEvent(g_dpy,rq.requestor,False,0,(XEvent*)&ans);
		XFlush(g_dpy);
		return true;
	}

	// SelectionNotify / PropertyNotify are picked up by get_clipboard_text
	return ev.type==SelectionNotify || ev.type==PropertyNotify;
}

void adxplat::set_clipboard_text(const char* utf8,bool primary)
{
	if(!clip_init() || !utf8)
		return;
	int const i=primary?1:0;
	g_clip_text[i]=utf8;
	XSetSelectionOwner(g_dpy,primary?XA_PRIMARY:a_clipboard,g_clip_win,CurrentTime);
	g_clip_owned[i]=(XGetSelectionOwner(g_dpy,primary?XA_PRIMARY:a_clipboard)==g_clip_win);
	XFlush(g_dpy);
}

char* adxplat::get_clipboard_text(bool primary)
{
	if(!clip_init())
		return nullptr;
	int const i=primary?1:0;
	Atom const sel=primary?XA_PRIMARY:a_clipboard;

	// our own selection
	::Window const owner=XGetSelectionOwner(g_dpy,sel);
	if(owner==g_clip_win && g_clip_owned[i])
		return strdup(g_clip_text[i].c_str());
	if(owner==None)
		return nullptr;

	Atom const targets[]={a_utf8,XA_STRING};
	for(Atom target:targets)
	{
		XDeleteProperty(g_dpy,g_clip_win,a_clip_prop);
		XConvertSelection(g_dpy,sel,target,a_clip_prop,g_clip_win,CurrentTime);
		XFlush(g_dpy);

		// wait for the owner's answer (at most one second; other events stay queued)
		XEvent ev;
		bool got=false;
		for(int t=0;t<500 && !got;t++)
		{
			if(XCheckTypedWindowEvent(g_dpy,g_clip_win,SelectionNotify,&ev))
				got=true;
			else
				usleep(2000);
		}
		if(!got || ev.xselection.property==None)
			continue;

		Atom type;
		int format;
		unsigned long n,rest;
		unsigned char* data=nullptr;
		if(XGetWindowProperty(g_dpy,g_clip_win,a_clip_prop,0,1<<20,True,AnyPropertyType,&type,&format,&n,&rest,&data)!=Success)
			continue;
		char* r=nullptr;
		if(data && format==8 && type!=a_incr)
		{
			if(type==XA_STRING)
			{
				// latin-1 -> utf-8
				std::string u;
				for(unsigned long k=0;k<n;k++)
				{
					unsigned char const c=data[k];
					if(c<0x80)
						u+=(char)c;
					else
					{
						u+=(char)(0xC0|(c>>6));
						u+=(char)(0x80|(c&0x3F));
					}
				}
				r=strdup(u.c_str());
			}
			else
				r=strndup((const char*)data,n);
		}
		if(data)
			XFree(data);
		if(r)
			return r;
	}
	return nullptr;
}

static void handle_x_event(XEvent& ev)
{
	if(XFilterEvent(&ev,None))
		return;

	// clipboard requests from other programs
	if(clip_handle_event(ev))
		return;

	scale_x_event(ev);

	// modal popup menus intercept input
	if(g_input_hook && (ev.type==ButtonPress || ev.type==ButtonRelease || ev.type==MotionNotify || ev.type==KeyPress || ev.type==KeyRelease))
	{
		if(ev.type==MotionNotify)
		{
			g_last_root_x=ev.xmotion.x_root;
			g_last_root_y=ev.xmotion.y_root;
		}
		if(g_input_hook(&ev,g_input_hook_user))
			return;
	}

	Win32Window* top=find_top_by_xwin(ev.xany.window);
	if(!top)
		return;

	switch(ev.type)
	{
	case Expose:
		{
			// the backbuffer still holds the image: just re-send the exposed region
			if(top->surface)
			{
				int const x1=ev.xexpose.x,y1=ev.xexpose.y,x2=x1+ev.xexpose.width,y2=y1+ev.xexpose.height;
				if(!top->has_dirty)
				{
					top->dirty.left=x1;top->dirty.top=y1;top->dirty.right=x2;top->dirty.bottom=y2;
					top->has_dirty=true;
				}
				else
				{
					top->dirty.left=std::min<LONG>(top->dirty.left,x1);
					top->dirty.top=std::min<LONG>(top->dirty.top,y1);
					top->dirty.right=std::max<LONG>(top->dirty.right,x2);
					top->dirty.bottom=std::max<LONG>(top->dirty.bottom,y2);
				}
			}
			break;
		}

	case ConfigureNotify:
		{
			top->root_valid=false;
			int const w=ev.xconfigure.width;
			int const h=ev.xconfigure.height-top->menubar_h;
			if(!top->is_popup && (w!=top->rect.right || h!=top->rect.bottom || !top->surface || top->surface->w!=ev.xconfigure.width || top->surface->h!=ev.xconfigure.height))
			{
				top->rect.right=std::max(w,1);
				top->rect.bottom=std::max(h,1);
				create_surface(top,ev.xconfigure.width,ev.xconfigure.height);
				top->invalid=true;
				invalidate_children(top);
				post_internal(top,WM_SIZE,0,MAKELPARAM(top->rect.right,top->rect.bottom));
			}
			if(top->track_placement)
				record_normal_placement(top);
			break;
		}

	case MapNotify:
		top->mapped=true;
		top->invalid=true;
		// modal dialogs take the keyboard focus (also without a window manager)
		if(top->dlg && !top->is_popup)
			XSetInputFocus(g_dpy,top->xwin,RevertToParent,CurrentTime);
		break;

	case UnmapNotify:
		top->mapped=false;
		break;

	case ClientMessage:
		if((Atom)ev.xclient.data.l[0]==a_wm_delete)
		{
			if(top->dlg)
				post_internal(top,WM_COMMAND,MAKEWPARAM(IDCANCEL,BN_CLICKED),0);
			else if(window_input_enabled(top))
				post_internal(top,WM_CLOSE,0,0);
		}
		break;

	case FocusIn:
		g_active_top=top;
		if(top->xic)
			XSetICFocus((XIC)top->xic);
		break;

	case FocusOut:
		if(top->xic)
			XUnsetICFocus((XIC)top->xic);
		// forget held keys (avoid stuck modifiers / notes)
		memset(g_key_down,0,sizeof(g_key_down));
		g_mod_state&=~(ShiftMask|ControlMask|Mod1Mask);
		break;

	case EnterNotify:
		g_pointer_top=top;
		break;

	case MappingNotify:
		XRefreshKeyboardMapping(&ev.xmapping);
		break;

	case ButtonPress:
	case ButtonRelease:
	case MotionNotify:
		handle_pointer_event(ev,top);
		break;

	case KeyPress:
	case KeyRelease:
		handle_key_event(ev,top);
		break;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// find the next window needing a paint (top levels first, then children in z-order)
static Win32Window* find_invalid_in(Win32Window* w)
{
	for(Win32Window* c:w->children)
	{
		if(!c->visible || c->destroyed)
			continue;
		if(c->invalid)
			return c;
		Win32Window* r=find_invalid_in(c);
		if(r)
			return r;
	}
	return nullptr;
}

static Win32Window* find_invalid_window(void)
{
	for(Win32Window* t:g_top_levels)
	{
		if(t->is_popup || !t->visible || t->destroyed)
			continue;
		if(t->invalid)
			return t;
	}
	for(Win32Window* t:g_top_levels)
	{
		if(t->is_popup || !t->visible || t->destroyed)
			continue;
		Win32Window* c=find_invalid_in(t);
		if(c)
			return c;
	}
	return nullptr;
}

// process x events and generate timer / paint messages. returns true if something was queued.
bool pump_one(bool block)
{
	if(!g_dpy)
		return false;

	size_t const before=g_queue.size();

	while(XPending(g_dpy))
	{
		XEvent ev;
		XNextEvent(g_dpy,&ev);
		handle_x_event(ev);
	}

	if(g_queue.size()>before)
		return true;

	if(check_timers())
		return true;

	Win32Window* inv=find_invalid_window();
	if(inv)
	{
		inv->invalid=false;
		post_internal(inv,WM_PAINT,0,0);
		return true;
	}

	flush_all_surfaces();

	if(!block)
		return false;

	// wait for x events, watched fds or the next timer
	std::vector<pollfd> fds;
	pollfd p;
	p.fd=ConnectionNumber(g_dpy);
	p.events=POLLIN;
	p.revents=0;
	fds.push_back(p);
	for(const FdWatch& w:g_fd_watches)
	{
		p.fd=w.fd;
		p.events=POLLIN;
		p.revents=0;
		fds.push_back(p);
	}

	int const timeout=ms_to_next_timer();
	int const r=poll(fds.data(),fds.size(),timeout);

	if(r>0)
	{
		std::vector<FdWatch> watches=g_fd_watches;
		for(size_t i=1;i<fds.size();i++)
			if(fds[i].revents & (POLLIN|POLLHUP|POLLERR))
				watches[i-1].cb(watches[i-1].fd,watches[i-1].user);
	}

	return false;
}

BOOL GetMessage(MSG* msg,HWND,UINT,UINT)
{
	for(;;)
	{
		if(!g_queue.empty())
		{
			*msg=g_queue.front();
			g_queue.pop_front();
			return TRUE;
		}

		if(g_quit)
		{
			memset(msg,0,sizeof(MSG));
			msg->message=WM_QUIT;
			msg->wParam=g_quit_code;
			return FALSE;
		}

		pump_one(true);
	}
}

BOOL PeekMessage(MSG* msg,HWND,UINT,UINT,UINT remove)
{
	if(g_queue.empty())
		pump_one(false);

	if(g_queue.empty())
	{
		if(g_quit)
		{
			memset(msg,0,sizeof(MSG));
			msg->message=WM_QUIT;
			return TRUE;
		}
		return FALSE;
	}

	*msg=g_queue.front();
	if(remove & PM_REMOVE)
		g_queue.pop_front();
	return TRUE;
}

BOOL TranslateMessage(const MSG* msg)
{
	if(msg->message==WM_KEYDOWN && msg->ch>=32 && msg->ch!=127)
	{
		// insert WM_CHAR right after this message
		MSG c=*msg;
		c.message=WM_CHAR;
		c.wParam=msg->ch;
		g_queue.push_front(c);
		return TRUE;
	}
	if(msg->message==WM_KEYDOWN && (msg->wParam==VK_BACK || msg->wParam==VK_RETURN || msg->wParam==VK_ESCAPE || msg->wParam==VK_TAB))
	{
		MSG c=*msg;
		c.message=WM_CHAR;
		c.wParam=(msg->wParam==VK_BACK)?8:(msg->wParam==VK_RETURN)?13:(msg->wParam==VK_ESCAPE)?27:9;
		g_queue.push_front(c);
		return TRUE;
	}
	return FALSE;
}

LRESULT DispatchMessage(const MSG* msg)
{
	if(msg->message==WM_TIMER)
	{
		for(TimerEntry& t:g_timers)
			if(t.hwnd==msg->hwnd && t.id==msg->wParam)
				t.pending=false;

		if(msg->lParam)
		{
			((TIMERPROC)msg->lParam)(msg->hwnd,WM_TIMER,msg->wParam,now_ms());
			flush_all_surfaces();
			return 0;
		}
	}

	if(!is_valid_window(msg->hwnd))
		return 0;

	bool const is_move=(msg->message==WM_MOUSEMOVE);
	if(is_move)
		g_cursor_set_in_dispatch=false;

	LRESULT const r=SendMessage(msg->hwnd,msg->message,msg->wParam,msg->lParam);

	// emulate WM_SETCURSOR default handling: if the window didn't set a cursor, use its class cursor
	if(is_move && !g_cursor_set_in_dispatch && is_valid_window(msg->hwnd) && msg->hwnd->cursor)
		apply_cursor(top_level_of(msg->hwnd),msg->hwnd->cursor);

	flush_all_surfaces();
	return r;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// dialogs: fixed size, centered over the owner
void dialog_place_and_show(Win32Window* dlg)
{
	if(!dlg || !dlg->xwin)
		return;

	int const w=dlg->rect.right;
	int const h=dlg->rect.bottom;

	XSizeHints* hints=XAllocSizeHints();
	hints->flags=PMinSize|PMaxSize|PPosition|USPosition;
	hints->min_width=hints->max_width=phys_len(dlg,w);
	hints->min_height=hints->max_height=phys_len(dlg,h);

	int sw=0,sh=0;
	screen_size(&sw,&sh);
	int x=(sw-w)/2;
	int y=(sh-h)/2;

	Win32Window* owner=dlg->owner;
	if(owner && owner->xwin && owner->mapped)
	{
		owner->root_valid=false;
		update_root_origin(owner);
		x=owner->root_x+(owner->rect.right-w)/2;
		y=owner->root_y+(owner->rect.bottom+owner->menubar_h-h)/2;
	}
	x=std::max(0,std::min(x,sw-w));
	y=std::max(0,std::min(y,sh-h));
	hints->x=phys_pos(x);
	hints->y=phys_pos(y);
	XSetWMNormalHints(g_dpy,dlg->xwin,hints);
	XFree(hints);

	XChangeProperty(g_dpy,dlg->xwin,a_net_wm_type,XA_ATOM,32,PropModeReplace,(const unsigned char*)&a_net_wm_type_dialog,1);
	XMoveWindow(g_dpy,dlg->xwin,phys_pos(x),phys_pos(y));
	ShowWindow(dlg,SW_SHOW);
}
