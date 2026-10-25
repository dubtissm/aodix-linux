/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Main Implementation
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AodixCore.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include <time.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CAodixCore*	gl_padx=NULL;
HWND		gl_hwnd_main=NULL;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// the root of all evil
int main(int argc,char** argv)
{
	// linux port: connect to the x server (the win32 compatibility layer)
	if(!adxplat::init(argc,argv))
		return 1;

	// application instance handle (unused on linux)
	HINSTANCE const hInstance=NULL;

	// random time sed
	srand((unsigned int)time(NULL));

	// call coinitialize (wave-shell plugins will not work otherwise)
	CoInitialize(NULL);

	// create main aodix core object
	gl_padx=new CAodixCore(hInstance); 

	// get desktop window handler
	HWND const hwnd_desktop=GetDesktopWindow();

	// get desktop device context
	HDC const hdc_desktop=GetDC(hwnd_desktop);

	// linux port: relative skin paths are resolved against the program directory
	char skin_dir[_MAX_PATH];
	adx_resolve_resource_path(gl_padx->cfg.skin_path,skin_dir);

	char gfx_path[_MAX_PATH*2], knb_path[_MAX_PATH*2], gui_path[_MAX_PATH*2];
	sprintf(gfx_path, "%s/gfx.bmp", skin_dir);
	sprintf(knb_path, "%s/knb.bmp", skin_dir);
	sprintf(gui_path, "%s/gui.bmp", skin_dir);

	// create gui/gfx device context(s)
	gl_padx->hdc_gui=CreateCompatibleDC(hdc_desktop);
	gl_padx->hdc_gfx=CreateCompatibleDC(hdc_desktop);
	gl_padx->hdc_knb=CreateCompatibleDC(hdc_desktop);

	// load gui/gfx bitmap(s)
	HBITMAP const hbitmap_gui=CreateCompatibleBitmap(hdc_desktop,2048,2049);
	HBITMAP const hbitmap_gfx=(HBITMAP)LoadImage(hInstance,gfx_path,IMAGE_BITMAP,0,0,LR_LOADFROMFILE);
	HBITMAP const hbitmap_knb=(HBITMAP)LoadImage(hInstance,knb_path,IMAGE_BITMAP,0,0,LR_LOADFROMFILE);

	// linux port: without skin bitmaps aodix cannot draw anything
	if(hbitmap_gfx==NULL || hbitmap_knb==NULL)
	{
		char buf[_MAX_PATH*2+64];
		snprintf(buf,sizeof(buf),"Error: Skin Bitmaps Not Found\n\n%s",skin_dir);
		MessageBox(NULL,buf,"Aodix",MB_OK | MB_ICONERROR);
		return 1;
	}

	// select gui/gfx bitmap(s) in gui/gfx device context(s)
	HBITMAP const hbitmap_gui_obm=(HBITMAP)SelectObject(gl_padx->hdc_gui,(HBITMAP)hbitmap_gui);
	HBITMAP const hbitmap_gfx_obm=(HBITMAP)SelectObject(gl_padx->hdc_gfx,(HBITMAP)hbitmap_gfx);
	HBITMAP const hbitmap_knb_obm=(HBITMAP)SelectObject(gl_padx->hdc_knb,(HBITMAP)hbitmap_knb);

	// blit GUI gfx into gui bitmap
	HDC const hdc_frm=CreateCompatibleDC(hdc_desktop);
	HBITMAP const hbitmap_frm=(HBITMAP)LoadImage(hInstance,gui_path,IMAGE_BITMAP,0,0,LR_LOADFROMFILE);
	HBITMAP const hbitmap_frm_obm=(HBITMAP)SelectObject(hdc_frm,hbitmap_frm);
	BitBlt(gl_padx->hdc_gui,0,0,2048,276,hdc_frm,0,0,SRCCOPY);
	SelectObject(hdc_frm,hbitmap_frm_obm);
	DeleteObject((HBITMAP)hbitmap_frm);
	DeleteDC(hdc_frm);

	// release desktop device context
	ReleaseDC(hwnd_desktop,hdc_desktop);
	
	// create edit brush
	gl_padx->hbrush_edit=CreateSolidBrush(GetPixel(gl_padx->hdc_gfx,321,321));

	// register vst plugin editor window class
	WNDCLASS wndcl_vst;	
	wndcl_vst.style=0;
	wndcl_vst.lpfnWndProc=plug_wnd_proc;
	wndcl_vst.cbClsExtra=0;
	wndcl_vst.cbWndExtra=0;
	wndcl_vst.hInstance=hInstance;
	wndcl_vst.hIcon=0;
	wndcl_vst.hCursor=0;
	wndcl_vst.hbrBackground=0;
	wndcl_vst.lpszMenuName=0;
	wndcl_vst.lpszClassName="VSTAodixHost";
	RegisterClass(&wndcl_vst);

	// register aodix window class
	WNDCLASSEX wndcl_adx;
	wndcl_adx.cbSize=sizeof(WNDCLASSEX); 
	wndcl_adx.style=CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
	wndcl_adx.lpfnWndProc=(WNDPROC)main_wnd_proc;
	wndcl_adx.cbClsExtra=0;
	wndcl_adx.cbWndExtra=0;
	wndcl_adx.hInstance=hInstance;
	wndcl_adx.hIcon=LoadIcon(hInstance,(LPCTSTR)IDI_AODIX);
	wndcl_adx.hCursor=LoadCursor(NULL,IDC_ARROW);
	wndcl_adx.hbrBackground=0;
	wndcl_adx.lpszMenuName=(LPCTSTR)IDR_AODIX;
	wndcl_adx.lpszClassName="MWndAodix";
	wndcl_adx.hIconSm=LoadIcon(hInstance,(LPCTSTR)IDI_AODIX);
	RegisterClassEx(&wndcl_adx);

	// format aodix version label string
	char adx_ver_buf[256];
	sprintf(adx_ver_buf,"%d",gl_padx->aodix_version);

	// get main window label
	char buf[256];

	// check version
	sprintf(buf,"Aodix %c.%c.%c.%c (Enhanced Version, Linux)",adx_ver_buf[0],adx_ver_buf[1],adx_ver_buf[2],adx_ver_buf[3]);

	// create window
	if (gl_padx->cfg.fullscreen)
		gl_hwnd_main=CreateWindow("MWndAodix",buf,WS_POPUP,CW_USEDEFAULT,0,CW_USEDEFAULT,0,NULL,NULL,hInstance,NULL);
	else
		gl_hwnd_main=CreateWindow("MWndAodix",buf,WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,0,CW_USEDEFAULT,0,NULL,NULL,hInstance,NULL);

	// return false if window creation failed
	if(gl_hwnd_main==NULL)
		return FALSE;

	// show and update window
	ShowWindow(gl_hwnd_main,SW_MAXIMIZE);
	UpdateWindow(gl_hwnd_main);

	// init midi input
	gl_padx->midi_in_init();

	// init asio
	gl_padx->audio_init(gl_hwnd_main);

	// post refresh
	gl_padx->gui_is_dirty=1;

	// if no vst folders configured, show config dialog
	if(gl_padx->cfg.vst_path[0][0]==0)
		gl_padx->gui_command(gl_hwnd_main,ID_FILE_CONFIGURATION);

	// set timer
	SetTimer(gl_hwnd_main,1,16,NULL);

	// open project file passed on the command line
	if (argc >= 2)
	{
		char path[MAX_PATH];
		snprintf(path, sizeof(path), "%s", argv[1]);

		gl_padx->audio_enter_cs();
		gl_padx->import_adx_file(gl_hwnd_main, path);
		gl_padx->audio_leave_cs();
	}

	// msg
	MSG msg;

	// main message loop
	while(GetMessage(&msg,NULL,0,0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	// unregister windows classes
	UnregisterClass("VSTAodixHost",hInstance);
	UnregisterClass("MWndAodix",hInstance);

	// bye bye COM ;-)
	CoUninitialize();

	// select default bitmap(s) in gui/gfx device context(s)
	SelectObject(gl_padx->hdc_gui,hbitmap_gui_obm);
	SelectObject(gl_padx->hdc_gfx,hbitmap_gfx_obm);
	SelectObject(gl_padx->hdc_knb,hbitmap_knb_obm);

	// delete gui/gfx device context(s)
	DeleteDC(gl_padx->hdc_gui);
	DeleteDC(gl_padx->hdc_gfx);
	DeleteDC(gl_padx->hdc_knb);

	// delete gui/gfx bitmap(s)
	DeleteObject((HBITMAP)hbitmap_gui);
	DeleteObject((HBITMAP)hbitmap_gfx);
	DeleteObject((HBITMAP)hbitmap_knb);

	// destroy aodix core instance
	delete gl_padx;

	// linux port: close the x server connection
	adxplat::shutdown();

	// exit application
	return (int)msg.wParam;
}
