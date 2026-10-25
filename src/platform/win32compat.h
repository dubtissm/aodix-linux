/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - Win32 compatibility layer
//
// Aodix was written directly against the Win32 API (GDI drawing, popup menus, dialogs, window messages).
// To keep the original program logic and look as untouched as possible, this header provides the small
// subset of the Win32 API that Aodix uses, implemented natively on top of X11 (see win32_*.cpp).
// It is NOT a general purpose Windows emulation: only what Aodix needs is implemented.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <ctime>
#include <strings.h>
#include <pthread.h>
#include <type_traits>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// basic types (LONG/DWORD are 32-bit like on Windows, pointer-sized types are 64-bit)
typedef int					BOOL;
typedef unsigned char		BYTE;
typedef unsigned short		WORD;
typedef uint32_t			DWORD;
typedef unsigned int		UINT;
typedef int32_t				LONG;
typedef intptr_t			LONG_PTR;
typedef uintptr_t			UINT_PTR;
typedef uintptr_t			ULONG_PTR;
typedef uintptr_t			WPARAM;
typedef intptr_t			LPARAM;
typedef intptr_t			LRESULT;
typedef intptr_t			INT_PTR;
typedef uint32_t			COLORREF;
typedef char				TCHAR;
typedef char*				LPSTR;
typedef char*				LPTSTR;
typedef const char*			LPCSTR;
typedef const char*			LPCTSTR;
typedef void*				LPVOID;
typedef long				HRESULT;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define CALLBACK
#define WINAPI
#define APIENTRY
#define _T(x) x

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// handles
struct Win32Window;
struct Win32Menu;
struct Win32Cursor;
struct GdiObject;
struct GdiBitmap;
struct GdiBrush;
struct GdiPen;
struct GdiFont;
struct GdiRegion;
struct DeviceContext;

typedef Win32Window*	HWND;
typedef Win32Menu*		HMENU;
typedef Win32Cursor*	HCURSOR;
typedef void*			HICON;
typedef void*			HINSTANCE;
typedef void*			HMODULE;
typedef void*			HANDLE;
typedef GdiObject*		HGDIOBJ;
typedef GdiBitmap*		HBITMAP;
typedef GdiBrush*		HBRUSH;
typedef GdiPen*			HPEN;
typedef GdiFont*		HFONT;
typedef GdiRegion*		HRGN;
typedef DeviceContext*	HDC;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// helpers / macros
#define LOWORD(l)			((WORD)(((ULONG_PTR)(l))&0xFFFF))
#define HIWORD(l)			((WORD)((((ULONG_PTR)(l))>>16)&0xFFFF))
#define MAKELONG(a,b)		((LONG)(((WORD)(a))|((DWORD)((WORD)(b)))<<16))
#define MAKEWPARAM(l,h)		((WPARAM)(DWORD)MAKELONG(l,h))
#define MAKELPARAM(l,h)		((LPARAM)(DWORD)MAKELONG(l,h))
#define MAKEINTRESOURCE(i)	((LPCSTR)((ULONG_PTR)((WORD)(i))))
#define RGB(r,g,b)			((COLORREF)(((BYTE)(r)|((WORD)((BYTE)(g))<<8))|(((DWORD)(BYTE)(b))<<16)))
#define GetRValue(c)		((BYTE)(c))
#define GetGValue(c)		((BYTE)(((WORD)(c))>>8))
#define GetBValue(c)		((BYTE)((c)>>16))
#define SUCCEEDED(hr)		(((HRESULT)(hr))>=0)

#define _MAX_PATH			260
#define MAX_PATH			260

#ifndef _countof
#define _countof(a)			(sizeof(a)/sizeof((a)[0]))
#endif

#define _stricmp			strcasecmp
#define _strnicmp			strncasecmp

inline char* _strlwr(char* s){for(char* p=s;*p;p++) if(*p>='A'&&*p<='Z') *p+=32; return s;}

// windows style min/max (Aodix uses them with mixed int/float arguments in the same type)
template<class A,class B> inline typename std::common_type<A,B>::type min(A a,B b){return (b<a)?b:a;}
template<class A,class B> inline typename std::common_type<A,B>::type max(A a,B b){return (a<b)?b:a;}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// structs
struct POINT { LONG x; LONG y; };
struct SIZE { LONG cx; LONG cy; };
struct RECT { LONG left; LONG top; LONG right; LONG bottom; };

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// gdi objects (defined here so that the handle types convert to HGDIOBJ like on windows)
enum { GDI_BITMAP=1, GDI_BRUSH, GDI_PEN, GDI_FONT, GDI_REGION };

struct GdiObject
{
	int		type;
	bool	stock;
	GdiObject(int t):type(t),stock(false){}
	virtual ~GdiObject(){}
};

struct GdiBitmap : GdiObject
{
	int			w;
	int			h;
	int			stride;		// in pixels
	uint32_t*	px;
	bool		own_pixels;
	GdiBitmap():GdiObject(GDI_BITMAP),w(0),h(0),stride(0),px(nullptr),own_pixels(false){}
	~GdiBitmap(){if(own_pixels) free(px);}
};

struct GdiBrush : GdiObject
{
	COLORREF	color;
	bool		null_brush;
	GdiBrush():GdiObject(GDI_BRUSH),color(0),null_brush(false){}
};

struct GdiPen : GdiObject
{
	COLORREF	color;
	int			style;
	int			width;
	bool		null_pen;
	GdiPen():GdiObject(GDI_PEN),color(0),style(0),width(1),null_pen(false){}
};

enum { FONT_TERMINAL=0, FONT_UI, FONT_UI_BOLD, FONT_UI_SMALL };

struct GdiFont : GdiObject
{
	int		kind;
	GdiFont():GdiObject(GDI_FONT),kind(FONT_UI){}
};

struct GdiRegion : GdiObject
{
	RECT	r;
	GdiRegion():GdiObject(GDI_REGION){r.left=r.top=r.right=r.bottom=0;}
};

struct MSG
{
	HWND	hwnd;
	UINT	message;
	WPARAM	wParam;
	LPARAM	lParam;
	DWORD	time;
	POINT	pt;
	DWORD	ch;			// (compat layer) translated character for WM_CHAR generation
};

struct PAINTSTRUCT
{
	HDC		hdc;
	BOOL	fErase;
	RECT	rcPaint;
};

struct BLENDFUNCTION
{
	BYTE BlendOp;
	BYTE BlendFlags;
	BYTE SourceConstantAlpha;
	BYTE AlphaFormat;
};

typedef LRESULT (*WNDPROC)(HWND,UINT,WPARAM,LPARAM);
typedef INT_PTR (*DLGPROC)(HWND,UINT,WPARAM,LPARAM);
typedef void (*TIMERPROC)(HWND,UINT,UINT_PTR,DWORD);

struct WNDCLASS
{
	UINT		style;
	WNDPROC		lpfnWndProc;
	int			cbClsExtra;
	int			cbWndExtra;
	HINSTANCE	hInstance;
	HICON		hIcon;
	HCURSOR		hCursor;
	HBRUSH		hbrBackground;
	LPCSTR		lpszMenuName;
	LPCSTR		lpszClassName;
};

struct WNDCLASSEX
{
	UINT		cbSize;
	UINT		style;
	WNDPROC		lpfnWndProc;
	int			cbClsExtra;
	int			cbWndExtra;
	HINSTANCE	hInstance;
	HICON		hIcon;
	HCURSOR		hCursor;
	HBRUSH		hbrBackground;
	LPCSTR		lpszMenuName;
	LPCSTR		lpszClassName;
	HICON		hIconSm;
};

struct MENUITEMINFO
{
	UINT		cbSize;
	UINT		fMask;
	UINT		fType;
	UINT		fState;
	UINT		wID;
	HMENU		hSubMenu;
	void*		hbmpChecked;
	void*		hbmpUnchecked;
	ULONG_PTR	dwItemData;
	LPSTR		dwTypeData;
	UINT		cch;
	void*		hbmpItem;
};

struct WIN32_FIND_DATA
{
	DWORD	dwFileAttributes;
	char	cFileName[MAX_PATH];
};

struct CRITICAL_SECTION
{
	pthread_mutex_t mutex;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// window messages
#define WM_NULL				0x0000
#define WM_CREATE			0x0001
#define WM_DESTROY			0x0002
#define WM_MOVE				0x0003
#define WM_SIZE				0x0005
#define WM_ACTIVATE			0x0006
#define WM_SETFOCUS			0x0007
#define WM_KILLFOCUS		0x0008
#define WM_ENABLE			0x000A
#define WM_SETTEXT			0x000C
#define WM_GETTEXT			0x000D
#define WM_GETTEXTLENGTH	0x000E
#define WM_PAINT			0x000F
#define WM_CLOSE			0x0010
#define WM_QUIT				0x0012
#define WM_ERASEBKGND		0x0014
#define WM_SETCURSOR		0x0020
#define WM_SETFONT			0x0030
#define WM_GETFONT			0x0031
#define WM_KEYDOWN			0x0100
#define WM_KEYUP			0x0101
#define WM_CHAR				0x0102
#define WM_SYSKEYDOWN		0x0104
#define WM_SYSKEYUP			0x0105
#define WM_INITDIALOG		0x0110
#define WM_COMMAND			0x0111
#define WM_SYSCOMMAND		0x0112
#define WM_TIMER			0x0113
#define WM_INITMENU			0x0116
#define WM_INITMENUPOPUP	0x0117
#define WM_CTLCOLOREDIT		0x0133
#define WM_CTLCOLORSTATIC	0x0138
#define WM_MOUSEMOVE		0x0200
#define WM_LBUTTONDOWN		0x0201
#define WM_LBUTTONUP		0x0202
#define WM_LBUTTONDBLCLK	0x0203
#define WM_RBUTTONDOWN		0x0204
#define WM_RBUTTONUP		0x0205
#define WM_RBUTTONDBLCLK	0x0206
#define WM_MBUTTONDOWN		0x0207
#define WM_MBUTTONUP		0x0208
#define WM_MOUSEWHEEL		0x020A
#define WM_USER				0x0400

// notification codes
#define BN_CLICKED			0
#define EN_CHANGE			0x0300
#define CBN_SELCHANGE		1
#define LBN_SELCHANGE		1
#define LBN_DBLCLK			2

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// window styles
#define WS_OVERLAPPED		0x00000000L
#define WS_POPUP			0x80000000L
#define WS_CHILD			0x40000000L
#define WS_MINIMIZE			0x20000000L
#define WS_VISIBLE			0x10000000L
#define WS_DISABLED			0x08000000L
#define WS_CLIPSIBLINGS		0x04000000L
#define WS_CLIPCHILDREN		0x02000000L
#define WS_MAXIMIZE			0x01000000L
#define WS_CAPTION			0x00C00000L
#define WS_BORDER			0x00800000L
#define WS_DLGFRAME			0x00400000L
#define WS_VSCROLL			0x00200000L
#define WS_HSCROLL			0x00100000L
#define WS_SYSMENU			0x00080000L
#define WS_THICKFRAME		0x00040000L
#define WS_GROUP			0x00020000L
#define WS_TABSTOP			0x00010000L
#define WS_MINIMIZEBOX		0x00020000L
#define WS_MAXIMIZEBOX		0x00010000L
#define WS_OVERLAPPEDWINDOW	(WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX)

#define WS_EX_TOOLWINDOW	0x00000080L
#define WS_EX_STATICEDGE	0x00020000L

#define CS_VREDRAW			0x0001
#define CS_HREDRAW			0x0002
#define CS_DBLCLKS			0x0008

#define CW_USEDEFAULT		((int)0x80000000)

#define SW_HIDE				0
#define SW_SHOWNORMAL		1
#define SW_SHOW				5
#define SW_MAXIMIZE			3

#define SWP_NOSIZE			0x0001
#define SWP_NOMOVE			0x0002
#define SWP_NOZORDER		0x0004
#define HWND_TOP			((HWND)0)

#define GWL_WNDPROC			(-4)
#define GWL_HINSTANCE		(-6)
#define GWL_ID				(-12)
#define GWL_USERDATA		(-21)
#define GWLP_WNDPROC		GWL_WNDPROC
#define GWLP_HINSTANCE		GWL_HINSTANCE
#define GWLP_ID				GWL_ID
#define GWLP_USERDATA		GWL_USERDATA

// edit control styles / messages
#define ES_LEFT				0x0000L
#define ES_CENTER			0x0001L
#define ES_AUTOHSCROLL		0x0080L
#define EM_SETSEL			0x00B1

// button styles / messages
#define BS_PUSHBUTTON		0x00000000L
#define BS_DEFPUSHBUTTON	0x00000001L
#define BS_AUTOCHECKBOX		0x00000003L
#define BS_GROUPBOX			0x00000007L
#define BM_GETCHECK			0x00F0
#define BM_SETCHECK			0x00F1
#define BM_GETSTATE			0x00F2
#define BST_UNCHECKED		0x0000
#define BST_CHECKED			0x0001

// static styles
#define SS_LEFT				0x00000000L
#define SS_CENTER			0x00000001L
#define SS_ICON				0x00000003L
#define SS_BLACKFRAME		0x00000007L
#define SS_CENTERIMAGE		0x00000200L

// combo box
#define CBS_DROPDOWNLIST	0x0003L
#define CB_ADDSTRING		0x0143
#define CB_GETCURSEL		0x0147
#define CB_RESETCONTENT		0x014B
#define CB_SETCURSEL		0x014E
#define CB_GETCOUNT			0x0146
#define CB_GETLBTEXT		0x0148
#define CB_ERR				(-1)

// list box
#define LBS_NOINTEGRALHEIGHT 0x0100L
#define LB_ADDSTRING		0x0180
#define LB_RESETCONTENT		0x0184
#define LB_SETCURSEL		0x0186
#define LB_GETCURSEL		0x0188
#define LB_GETTEXT			0x0189
#define LB_GETCOUNT			0x018B
#define LB_ERR				(-1)

// progress bar
#define PBM_SETRANGE		(WM_USER+1)
#define PBM_SETPOS			(WM_USER+2)
#define PBM_SETRANGE32		(WM_USER+6)
#define PBM_SETBARCOLOR		(WM_USER+9)
#define PBM_SETBKCOLOR		0x2001

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// dialog / message box
#define IDOK				1
#define IDCANCEL			2
#define IDABORT				3
#define IDRETRY				4
#define IDIGNORE			5
#define IDYES				6
#define IDNO				7

#define MB_OK				0x00000000L
#define MB_OKCANCEL			0x00000001L
#define MB_YESNOCANCEL		0x00000003L
#define MB_YESNO			0x00000004L
#define MB_ICONERROR		0x00000010L
#define MB_ICONQUESTION		0x00000020L
#define MB_ICONWARNING		0x00000030L
#define MB_ICONINFORMATION	0x00000040L

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// menus
#define MF_BYCOMMAND		0x00000000L
#define MF_STRING			0x00000000L
#define MF_ENABLED			0x00000000L
#define MF_UNCHECKED		0x00000000L
#define MF_GRAYED			0x00000001L
#define MF_DISABLED			0x00000002L
#define MF_CHECKED			0x00000008L
#define MF_POPUP			0x00000010L
#define MF_MENUBARBREAK		0x00000020L
#define MF_MENUBREAK		0x00000040L
#define MF_BYPOSITION		0x00000400L
#define MF_SEPARATOR		0x00000800L

#define MFT_STRING			MF_STRING
#define MFT_MENUBARBREAK	MF_MENUBARBREAK
#define MFT_SEPARATOR		MF_SEPARATOR
#define MFT_RADIOCHECK		0x00000200L

#define MFS_ENABLED			0x00000000L
#define MFS_GRAYED			0x00000003L
#define MFS_CHECKED			0x00000008L
#define MFS_HILITE			0x00000080L
#define MFS_DEFAULT			0x00001000L

#define MIIM_STATE			0x00000001
#define MIIM_ID				0x00000002
#define MIIM_SUBMENU		0x00000004
#define MIIM_TYPE			0x00000010
#define MIIM_STRING			0x00000040
#define MIIM_FTYPE			0x00000100

#define TPM_LEFTALIGN		0x0000L
#define TPM_RETURNCMD		0x0100L

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// gdi
#define SRCCOPY				0x00CC0020
#define PS_SOLID			0
#define PS_DOT				2
#define TRANSPARENT			1
#define OPAQUE				2
#define ETO_OPAQUE			0x0002
#define AC_SRC_OVER			0x00

#define FW_NORMAL			400
#define FW_BOLD				700
#define OEM_CHARSET			255
#define ANSI_CHARSET		0
#define DEFAULT_CHARSET		1
#define OUT_DEFAULT_PRECIS	0
#define CLIP_DEFAULT_PRECIS	0
#define DEFAULT_QUALITY		0
#define DEFAULT_PITCH		0
#define FIXED_PITCH			1
#define FF_MODERN			(3<<4)

#define IMAGE_BITMAP		0
#define LR_LOADFROMFILE		0x00000010

#define COLOR_BTNFACE		15

// standard cursors
#define IDC_ARROW			MAKEINTRESOURCE(32512)
#define IDC_IBEAM			MAKEINTRESOURCE(32513)
#define IDC_WAIT			MAKEINTRESOURCE(32514)
#define IDC_SIZENWSE		MAKEINTRESOURCE(32642)
#define IDC_SIZENESW		MAKEINTRESOURCE(32643)
#define IDC_SIZEWE			MAKEINTRESOURCE(32644)
#define IDC_SIZENS			MAKEINTRESOURCE(32645)
#define IDC_SIZEALL			MAKEINTRESOURCE(32646)
#define IDC_HAND			MAKEINTRESOURCE(32649)

#define FILE_ATTRIBUTE_DIRECTORY	0x00000010
#define INVALID_HANDLE_VALUE		((void*)(intptr_t)-1)

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// virtual key codes
#define VK_LBUTTON			0x01
#define VK_BACK				0x08
#define VK_TAB				0x09
#define VK_RETURN			0x0D
#define VK_SHIFT			0x10
#define VK_CONTROL			0x11
#define VK_MENU				0x12
#define VK_PAUSE			0x13
#define VK_CAPITAL			0x14
#define VK_ESCAPE			0x1B
#define VK_SPACE			0x20
#define VK_PRIOR			0x21
#define VK_NEXT				0x22
#define VK_END				0x23
#define VK_HOME				0x24
#define VK_LEFT				0x25
#define VK_UP				0x26
#define VK_RIGHT			0x27
#define VK_DOWN				0x28
#define VK_INSERT			0x2D
#define VK_DELETE			0x2E
#define VK_NUMPAD0			0x60
#define VK_NUMPAD1			0x61
#define VK_NUMPAD2			0x62
#define VK_NUMPAD3			0x63
#define VK_NUMPAD4			0x64
#define VK_NUMPAD5			0x65
#define VK_NUMPAD6			0x66
#define VK_NUMPAD7			0x67
#define VK_NUMPAD8			0x68
#define VK_NUMPAD9			0x69
#define VK_MULTIPLY			0x6A
#define VK_ADD				0x6B
#define VK_SEPARATOR		0x6C
#define VK_SUBTRACT			0x6D
#define VK_DECIMAL			0x6E
#define VK_DIVIDE			0x6F
#define VK_F1				0x70
#define VK_F2				0x71
#define VK_F3				0x72
#define VK_F4				0x73
#define VK_F5				0x74
#define VK_F6				0x75
#define VK_F7				0x76
#define VK_F8				0x77
#define VK_F9				0x78
#define VK_F10				0x79
#define VK_F11				0x7A
#define VK_F12				0x7B
#define VK_NUMLOCK			0x90
#define VK_SCROLL			0x91
#define VK_OEM_1			0xBA
#define VK_OEM_PLUS			0xBB
#define VK_OEM_COMMA		0xBC
#define VK_OEM_MINUS		0xBD
#define VK_OEM_PERIOD		0xBE
#define VK_OEM_2			0xBF
#define VK_OEM_3			0xC0
#define VK_OEM_4			0xDB
#define VK_OEM_5			0xDC
#define VK_OEM_6			0xDD
#define VK_OEM_7			0xDE
#define VK_OEM_102			0xE2

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// window functions
BOOL		RegisterClass(const WNDCLASS* wc);
BOOL		RegisterClassEx(const WNDCLASSEX* wc);
BOOL		UnregisterClass(LPCSTR class_name,HINSTANCE hinstance);
HWND		CreateWindowEx(DWORD ex_style,LPCSTR class_name,LPCSTR window_name,DWORD style,int x,int y,int w,int h,HWND parent,HMENU menu,HINSTANCE hinstance,LPVOID param);
#define		CreateWindow(c,n,s,x,y,w,h,p,m,i,a) CreateWindowEx(0,c,n,s,x,y,w,h,p,m,i,a)
BOOL		DestroyWindow(HWND hwnd);
BOOL		ShowWindow(HWND hwnd,int cmd);
BOOL		UpdateWindow(HWND hwnd);
BOOL		IsWindow(HWND hwnd);
BOOL		EnableWindow(HWND hwnd,BOOL enable);
BOOL		IsWindowEnabled(HWND hwnd);
BOOL		GetClientRect(HWND hwnd,RECT* r);
BOOL		ClientToScreen(HWND hwnd,POINT* p);
BOOL		ScreenToClient(HWND hwnd,POINT* p);
BOOL		SetWindowPos(HWND hwnd,HWND insert_after,int x,int y,int cx,int cy,UINT flags);
BOOL		SetWindowText(HWND hwnd,LPCSTR text);
int			GetWindowText(HWND hwnd,LPSTR text,int max_count);
LONG_PTR	SetWindowLongPtr(HWND hwnd,int index,LONG_PTR value);
LONG_PTR	GetWindowLongPtr(HWND hwnd,int index);
LRESULT		DefWindowProc(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam);
LRESULT		CallWindowProc(WNDPROC proc,HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam);
BOOL		InvalidateRect(HWND hwnd,const RECT* r,BOOL erase);
HWND		SetFocus(HWND hwnd);
HWND		GetFocus(void);
HWND		SetCapture(HWND hwnd);
BOOL		ReleaseCapture(void);
BOOL		GetCursorPos(POINT* p);
short		GetKeyState(int vkey);
short		GetAsyncKeyState(int vkey);
UINT_PTR	SetTimer(HWND hwnd,UINT_PTR id,UINT elapse_ms,TIMERPROC proc);
BOOL		KillTimer(HWND hwnd,UINT_PTR id);
BOOL		GetMessage(MSG* msg,HWND hwnd,UINT min,UINT max);
BOOL		PeekMessage(MSG* msg,HWND hwnd,UINT min,UINT max,UINT remove);
BOOL		TranslateMessage(const MSG* msg);
LRESULT		DispatchMessage(const MSG* msg);
void		PostQuitMessage(int exit_code);
BOOL		PostMessage(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam);
LRESULT		SendMessage(HWND hwnd,UINT msg,WPARAM wparam,LPARAM lparam);
HWND		GetDesktopWindow(void);
HWND		GetParent(HWND hwnd);
HMENU		GetMenu(HWND hwnd);
DWORD		GetTickCount(void);
DWORD		timeGetTime(void);
void		Sleep(DWORD ms);
int			MessageBox(HWND hwnd,LPCSTR text,LPCSTR caption,UINT type);
HCURSOR		LoadCursor(HINSTANCE hinstance,LPCSTR name);
HCURSOR		SetCursor(HCURSOR cursor);
BOOL		DestroyCursor(HCURSOR cursor);
HICON		LoadIcon(HINSTANCE hinstance,LPCSTR name);
#define		PM_NOREMOVE	0x0000
#define		PM_REMOVE	0x0001

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// gdi functions
HDC			GetDC(HWND hwnd);
int			ReleaseDC(HWND hwnd,HDC hdc);
HDC			BeginPaint(HWND hwnd,PAINTSTRUCT* ps);
BOOL		EndPaint(HWND hwnd,const PAINTSTRUCT* ps);
HDC			CreateCompatibleDC(HDC hdc);
BOOL		DeleteDC(HDC hdc);
HBITMAP		CreateCompatibleBitmap(HDC hdc,int w,int h);
void*		LoadImage(HINSTANCE hinstance,LPCSTR name,UINT type,int cx,int cy,UINT flags);
HGDIOBJ		SelectObject(HDC hdc,HGDIOBJ obj);
BOOL		DeleteObject(HGDIOBJ obj);
BOOL		BitBlt(HDC dst,int x,int y,int w,int h,HDC src,int sx,int sy,DWORD rop);
BOOL		AlphaBlend(HDC dst,int x,int y,int w,int h,HDC src,int sx,int sy,int sw,int sh,BLENDFUNCTION bf);
COLORREF	GetPixel(HDC hdc,int x,int y);
COLORREF	SetPixel(HDC hdc,int x,int y,COLORREF color);
HBRUSH		CreateSolidBrush(COLORREF color);
HPEN		CreatePen(int style,int width,COLORREF color);
BOOL		MoveToEx(HDC hdc,int x,int y,POINT* old);
BOOL		LineTo(HDC hdc,int x,int y);
BOOL		Polygon(HDC hdc,const POINT* pts,int count);
int			FillRect(HDC hdc,const RECT* r,HBRUSH brush);
BOOL		InvertRect(HDC hdc,const RECT* r);
BOOL		ExtTextOut(HDC hdc,int x,int y,UINT options,const RECT* r,LPCSTR text,UINT count,const int* dx);
BOOL		TextOut(HDC hdc,int x,int y,LPCSTR text,int count);
COLORREF	SetTextColor(HDC hdc,COLORREF color);
COLORREF	SetBkColor(HDC hdc,COLORREF color);
int			SetBkMode(HDC hdc,int mode);
HFONT		CreateFont(int h,int w,int esc,int orient,int weight,DWORD italic,DWORD underline,DWORD strike,DWORD charset,DWORD out_prec,DWORD clip_prec,DWORD quality,DWORD pitch,LPCSTR face);
HRGN		CreateRectRgn(int x1,int y1,int x2,int y2);
int			SelectClipRgn(HDC hdc,HRGN rgn);
HBRUSH		GetSysColorBrush(int index);
DWORD		GetSysColor(int index);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// menu functions
HMENU		CreateMenu(void);
HMENU		CreatePopupMenu(void);
BOOL		DestroyMenu(HMENU hmenu);
BOOL		InsertMenuItem(HMENU hmenu,UINT item,BOOL by_position,const MENUITEMINFO* mii);
BOOL		AppendMenu(HMENU hmenu,UINT flags,UINT_PTR id_or_submenu,LPCSTR text);
BOOL		ModifyMenu(HMENU hmenu,UINT position,UINT flags,UINT_PTR id_or_submenu,LPCSTR text);
DWORD		CheckMenuItem(HMENU hmenu,UINT item,UINT check);
BOOL		CheckMenuRadioItem(HMENU hmenu,UINT first,UINT last,UINT check,UINT flags);
BOOL		EnableMenuItem(HMENU hmenu,UINT item,UINT enable);
BOOL		SetMenuItemInfo(HMENU hmenu,UINT item,BOOL by_position,const MENUITEMINFO* mii);
HMENU		GetSubMenu(HMENU hmenu,int pos);
int			GetMenuItemCount(HMENU hmenu);
BOOL		TrackPopupMenu(HMENU hmenu,UINT flags,int x,int y,int reserved,HWND hwnd,const RECT* rect);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// dialog functions (dialog templates are compiled in, see win32_dialog_templates.cpp)
INT_PTR		DialogBox(HINSTANCE hinstance,LPCSTR template_name,HWND parent,DLGPROC proc);
BOOL		EndDialog(HWND hdlg,INT_PTR result);
HWND		GetDlgItem(HWND hdlg,int id);
BOOL		SetDlgItemText(HWND hdlg,int id,LPCSTR text);
UINT		GetDlgItemText(HWND hdlg,int id,LPSTR text,int max_count);
BOOL		CheckDlgButton(HWND hdlg,int id,UINT check);
UINT		IsDlgButtonChecked(HWND hdlg,int id);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// files / system
void*		FindFirstFile(LPCSTR pattern,WIN32_FIND_DATA* data);
BOOL		FindNextFile(void* hfind,WIN32_FIND_DATA* data);
BOOL		FindClose(void* hfind);
short		GetFileTitle(LPCSTR file,LPSTR title,WORD max);
DWORD		GetModuleFileName(HMODULE hmodule,LPSTR filename,DWORD size);
BOOL		CreateDirectory(LPCSTR path,void* security);
HRESULT		CoInitialize(LPVOID reserved);
void		CoUninitialize(void);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// critical sections (recursive mutex, like on windows)
void		InitializeCriticalSection(CRITICAL_SECTION* cs);
void		DeleteCriticalSection(CRITICAL_SECTION* cs);
void		EnterCriticalSection(CRITICAL_SECTION* cs);
void		LeaveCriticalSection(CRITICAL_SECTION* cs);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// linux additions used by the port
namespace adxplat
{
	// initialize / shutdown the X11 connection (call once at startup)
	bool		init(int argc,char** argv);
	void		shutdown(void);

	// directory of the running executable (no trailing slash)
	const char*	exe_dir(void);

	// user config directory (~/.config/aodix), created on demand
	const char*	config_dir(void);

	// open a file or url with the desktop default application
	void		open_url(const char* url);

	// native X11 handles (for embedding plugin editor views)
	void*		x11_display(void);
	unsigned long x11_window(HWND hwnd);

	// register a file descriptor that the message loop should watch (plugin run loops)
	typedef void (*fd_callback)(int fd,void* user);
	void		add_fd_watch(int fd,fd_callback cb,void* user);
	void		remove_fd_watch(int fd,void* user);
	void		remove_fd_watches(void* user);

	// built-in file / folder chooser (falls back from zenity / kdialog)
	bool		choose_file(HWND owner,const char* title,char* path,int path_size,const char* filter,bool save,const char* default_ext,const char* initial_dir);
	bool		choose_folder(HWND owner,const char* title,char* path,int path_size);

	// set the cursor for a window (persists until changed)
	void		set_window_cursor(HWND hwnd,HCURSOR cursor);

	// fullscreen toggle for top level windows
	void		set_fullscreen(HWND hwnd,bool fullscreen);
}
