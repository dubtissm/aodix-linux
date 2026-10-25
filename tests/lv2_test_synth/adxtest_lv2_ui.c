// X11 UI of the ADX Test LV2 Synth: embedded through ui:parent, driven by the idle interface (own X connection).
// Left click sets the gain (write_function), right click makes the editor larger (ui:resize), the bar shows the gain,
// the dot the level output. MIT licence, part of Aodix Linux.
#include <lv2/core/lv2.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <X11/Xlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void tlog(const char* fmt,...)
{
	const char* p=getenv("ADXTEST_LOG"); if(!p) return;
	FILE* f=fopen(p,"a"); if(!f) return;
	va_list ap; va_start(ap,fmt); fprintf(f,"[lv2ui] "); vfprintf(f,fmt,ap); va_end(ap); fputc('\n',f); fclose(f);
}

typedef struct
{
	Display* dpy; Window win; GC gc; int w,h;
	LV2UI_Write_Function write; LV2UI_Controller ctl; const LV2UI_Resize* resize;
	float gain,level; int idles;
} UI;

static void draw(UI* u)
{
	XSetForeground(u->dpy,u->gc,0x203040); XFillRectangle(u->dpy,u->win,u->gc,0,0,u->w,u->h);
	XSetForeground(u->dpy,u->gc,0x40c060); XFillRectangle(u->dpy,u->win,u->gc,10,u->h/2-10,(int)((u->w-20)*u->gain),20);
	XSetForeground(u->dpy,u->gc,0xffd040); XFillArc(u->dpy,u->win,u->gc,10+(int)((u->w-30)*(u->level>1?1:u->level)),u->h-30,16,16,0,360*64);
	XSetForeground(u->dpy,u->gc,0xffffff);
	char buf[64]; snprintf(buf,sizeof(buf),"ADX Test LV2  gain %.2f  %dx%d",u->gain,u->w,u->h);
	XDrawString(u->dpy,u->win,u->gc,10,20,buf,(int)strlen(buf));
	XFlush(u->dpy);
}

static LV2UI_Handle instantiate(const LV2UI_Descriptor* d,const char* plugin_uri,const char* bundle,LV2UI_Write_Function write,
	LV2UI_Controller ctl,LV2UI_Widget* widget,const LV2_Feature* const* f)
{
	(void)d; (void)plugin_uri;
	Window parent=0; const LV2UI_Resize* resize=NULL;
	for(int i=0;f[i];i++)
	{
		if(!strcmp(f[i]->URI,LV2_UI__parent)) parent=(Window)(uintptr_t)f[i]->data;
		if(!strcmp(f[i]->URI,LV2_UI__resize)) resize=(const LV2UI_Resize*)f[i]->data;
	}
	tlog("ui instantiate bundle=%s parent=%s resize=%s",bundle,parent?"yes":"no",resize?"yes":"no");
	if(!parent) return NULL;
	UI* u=(UI*)calloc(1,sizeof(UI));
	u->dpy=XOpenDisplay(NULL);
	if(!u->dpy) { free(u); return NULL; }
	u->w=320; u->h=180; u->write=write; u->ctl=ctl; u->resize=resize; u->gain=0.8f;
	u->win=XCreateSimpleWindow(u->dpy,parent,0,0,u->w,u->h,0,0,0x203040);
	XSelectInput(u->dpy,u->win,ExposureMask|ButtonPressMask|StructureNotifyMask);
	u->gc=XCreateGC(u->dpy,u->win,0,NULL);
	XMapWindow(u->dpy,u->win);
	XFlush(u->dpy);
	if(resize) resize->ui_resize(resize->handle,u->w,u->h);
	*widget=(LV2UI_Widget)(uintptr_t)u->win;
	return u;
}

static void cleanup(LV2UI_Handle h)
{
	UI* u=(UI*)h;
	tlog("ui cleanup (idles %d)",u->idles);
	XFreeGC(u->dpy,u->gc); XDestroyWindow(u->dpy,u->win); XCloseDisplay(u->dpy); free(u);
}

static void port_event(LV2UI_Handle h,uint32_t port,uint32_t size,uint32_t format,const void* buf)
{
	UI* u=(UI*)h;
	if(format!=0 || size!=sizeof(float)) return;
	float const v=*(const float*)buf;
	if(port==0) { u->gain=v; tlog("ui port_event gain %.3f",v); draw(u); }
	else if(port==4) { u->level=v; }
}

static int idle(LV2UI_Handle h)
{
	UI* u=(UI*)h;
	u->idles++;
	int redraw=(u->idles%10)==0;
	while(XPending(u->dpy))
	{
		XEvent e; XNextEvent(u->dpy,&e);
		if(e.type==Expose) redraw=1;
		else if(e.type==ConfigureNotify && (e.xconfigure.width!=u->w || e.xconfigure.height!=u->h))
		{
			u->w=e.xconfigure.width; u->h=e.xconfigure.height; redraw=1;
			tlog("ui configure %dx%d",u->w,u->h);
		}
		else if(e.type==ButtonPress && e.xbutton.button==1)
		{
			float g=(float)(e.xbutton.x-10)/(float)(u->w-20); g=g<0?0:(g>1?1:g);
			u->gain=g; u->write(u->ctl,0,sizeof(float),0,&g); redraw=1;
			tlog("ui write gain %.3f",g);
		}
		else if(e.type==ButtonPress && e.xbutton.button==3)
		{
			u->w+=80; u->h+=40;
			XResizeWindow(u->dpy,u->win,u->w,u->h);
			int r=u->resize?u->resize->ui_resize(u->resize->handle,u->w,u->h):-1;
			tlog("ui request resize %dx%d -> %d",u->w,u->h,r);
			redraw=1;
		}
	}
	if(redraw) draw(u);
	return 0;
}

static int ui_resize(LV2UI_Feature_Handle h,int w,int hh)
{
	UI* u=(UI*)h;
	tlog("ui resized by host %dx%d",w,hh);
	u->w=w; u->h=hh; XResizeWindow(u->dpy,u->win,w,hh); draw(u);
	return 0;
}

static const void* extension_data(const char* uri)
{
	static const LV2UI_Idle_Interface ii={idle};
	static LV2UI_Resize rs={NULL,ui_resize};
	if(!strcmp(uri,LV2_UI__idleInterface)) return &ii;
	if(!strcmp(uri,LV2_UI__resize)) return &rs;
	return NULL;
}

static const LV2UI_Descriptor desc={"urn:aodix:adxtest-lv2#ui",instantiate,cleanup,port_event,extension_data};
LV2_SYMBOL_EXPORT const LV2UI_Descriptor* lv2ui_descriptor(uint32_t i) { return i==0?&desc:NULL; }
