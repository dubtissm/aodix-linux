/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Plugin Window Procedure Implementation
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AodixCore.h"

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
LRESULT CALLBACK plug_wnd_proc(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam)
{
	// extern aodix core pointer
	extern CAodixCore* gl_padx;

	// main application window handler
	extern HWND	gl_hwnd_main;

	// get vst effect pointer
	AdxPlugin* peffect=(AdxPlugin*)GetWindowLongPtr(hwnd,GWLP_USERDATA);

	// paint message
	if(message==WM_PAINT)
	{
		// client rectangle
		RECT r;

		// get window rect
		GetClientRect(hwnd,&r);

		// paint structure
		PAINTSTRUCT ps;

		// get window hdc for paint
		HDC const hdc=BeginPaint(hwnd,&ps);

		// paint gray frame
		FillRect(hdc,&r,0);

		// finish paint
		EndPaint(hwnd,&ps);
		return 0;
	}

	// close window message
	if(message==WM_CLOSE)
	{
		// check plugin editor window
		if(peffect!=NULL && (peffect->hasEditor))
			peffect->editorClose();

		// destroy window
		DestroyWindow(hwnd);
		return 0;
	}

	// size message (linux port: the window manager resized the frame of a resizable editor)
	if(message==WM_SIZE)
	{
		if(peffect!=NULL && peffect->hasEditor)
			peffect->editorResized(LOWORD(lparam),HIWORD(lparam));
		return 0;
	}

	// key down message
	if(message==WM_KEYDOWN)
	{
		if(gl_padx->audio_is_inited())
			gl_padx->gui_key_down(gl_hwnd_main,wparam,lparam);
		return 0;
	}

	// key up message
	if(message==WM_KEYUP)
	{
		if(gl_padx->audio_is_inited())
			gl_padx->gui_key_up(gl_hwnd_main,wparam);
		return 0;
	}

	// else (return default window proc)
	return DefWindowProc(hwnd,message,wparam,lparam);
}
