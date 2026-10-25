// Minimal VST3 test instrument with a raw Xlib editor (for testing the Aodix host: X11 embedding, IRunLoop, resize, automation)
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/common/pluginview.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/base/ibstream.h"
#include <X11/Xlib.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

using namespace Steinberg;
using namespace Steinberg::Vst;

static FUID ProcUID(0x41445854,0x45535431,0x50524F43,0x00000001);
static FUID CtrlUID(0x41445854,0x45535431,0x4354524C,0x00000001);

static void tlog(const char* fmt,...)
{
	const char* p=getenv("ADXTEST_LOG");
	if(!p) return;
	FILE* f=fopen(p,"a");
	if(!f) return;
	va_list ap; va_start(ap,fmt); vfprintf(f,fmt,ap); va_end(ap);
	fputc('\n',f); fclose(f);
}

//------------------------------------------------------------------------------------------------
class Proc : public AudioEffect
{
public:
	Proc(){setControllerClass(CtrlUID);}
	static FUnknown* create(void*){return (IAudioProcessor*)new Proc;}
	tresult PLUGIN_API initialize(FUnknown* ctx) SMTG_OVERRIDE
	{
		tresult r=AudioEffect::initialize(ctx);
		if(r!=kResultOk) return r;
		addAudioOutput(STR16("Out"),SpeakerArr::kStereo);
		addEventInput(STR16("Events"),16);
		return kResultOk;
	}
	tresult PLUGIN_API setupProcessing(ProcessSetup& s) SMTG_OVERRIDE{sr=s.sampleRate;return AudioEffect::setupProcessing(s);}
	tresult PLUGIN_API process(ProcessData& d) SMTG_OVERRIDE
	{
		if(d.inputParameterChanges)
			for(int32 i=0;i<d.inputParameterChanges->getParameterCount();i++)
			{
				IParamValueQueue* q=d.inputParameterChanges->getParameterData(i);
				ParamValue v; int32 off;
				if(q && q->getParameterId()==0 && q->getPointCount()>0 && q->getPoint(q->getPointCount()-1,off,v)==kResultOk) gain=v;
			}
		if(d.inputEvents)
			for(int32 i=0;i<d.inputEvents->getEventCount();i++)
			{
				Event e;
				if(d.inputEvents->getEvent(i,e)!=kResultOk) continue;
				if(e.type==Event::kNoteOnEvent && e.noteOn.velocity>0){note=e.noteOn.pitch;on=true;}
				else if(e.type==Event::kNoteOffEvent && e.noteOff.pitch==note) on=false;
			}
		// optional artificial cpu load for host performance tests: ADXTEST_LOAD_US=<microseconds per process call>
		if(load_us<0) { const char* e=getenv("ADXTEST_LOAD_US"); load_us=e?atoi(e):0; }
		if(load_us>0)
		{
			struct timespec a,b; clock_gettime(CLOCK_MONOTONIC,&a);
			volatile double x=1.0;
			do { for(int k=0;k<200;k++) x=x*1.0000001+1e-9; clock_gettime(CLOCK_MONOTONIC,&b); }
			while((b.tv_sec-a.tv_sec)*1000000L+(b.tv_nsec-a.tv_nsec)/1000<load_us);
		}
		if(d.numOutputs<1 || d.numSamples<=0) return kResultOk;
		float* l=d.outputs[0].channelBuffers32[0];
		float* r=d.outputs[0].channelBuffers32[1];
		double const inc=440.0*pow(2.0,(note-69)/12.0)/sr;
		for(int32 s=0;s<d.numSamples;s++)
		{
			float v=on?(float)(sin(phase*6.283185307)*gain*0.5):0.f;
			phase+=inc; if(phase>=1) phase-=1;
			l[s]=r[s]=v;
		}
		d.outputs[0].silenceFlags=on?0:3;
		return kResultOk;
	}
	tresult PLUGIN_API setState(IBStream* st) SMTG_OVERRIDE{float g;int32 n;if(st->read(&g,4,&n)==kResultOk&&n==4)gain=g;return kResultOk;}
	tresult PLUGIN_API getState(IBStream* st) SMTG_OVERRIDE{float g=(float)gain;return st->write(&g,4,nullptr);}
	double sr=48000,phase=0,gain=0.8; int note=60; bool on=false; int load_us=-1;
};

//------------------------------------------------------------------------------------------------
class Ctrl;
class View : public CPluginView, public Linux::IEventHandler, public Linux::ITimerHandler
{
public:
	View(Ctrl* c):CPluginView(new ViewRect(0,0,300,200)),ctrl(c){}
	tresult PLUGIN_API isPlatformTypeSupported(FIDString t) SMTG_OVERRIDE{return strcmp(t,kPlatformTypeX11EmbedWindowID)==0?kResultTrue:kResultFalse;}
	tresult PLUGIN_API attached(void* parent,FIDString type) SMTG_OVERRIDE;
	tresult PLUGIN_API removed() SMTG_OVERRIDE;
	tresult PLUGIN_API onSize(ViewRect* r) SMTG_OVERRIDE
	{
		CPluginView::onSize(r);
		tlog("onSize %d %d",r->getWidth(),r->getHeight());
		if(dpy && win){XResizeWindow(dpy,win,r->getWidth(),r->getHeight());draw();XFlush(dpy);}
		return kResultOk;
	}
	tresult PLUGIN_API canResize() SMTG_OVERRIDE{return kResultTrue;}
	tresult PLUGIN_API checkSizeConstraint(ViewRect* r) SMTG_OVERRIDE{if(r->getWidth()<200)r->right=r->left+200;if(r->getHeight()<100)r->bottom=r->top+100;return kResultTrue;}
	void PLUGIN_API onFDIsSet(Linux::FileDescriptor) SMTG_OVERRIDE;
	void PLUGIN_API onTimer() SMTG_OVERRIDE{ticks++;if(ticks==1||ticks%20==0)tlog("timer %d",ticks);draw();}
	void draw();
	DECLARE_FUNKNOWN_METHODS
	Ctrl* ctrl; Display* dpy=nullptr; Window win=0; GC gc=0; int ticks=0; Linux::IRunLoop* loop=nullptr;
};
IMPLEMENT_REFCOUNT(View)
tresult PLUGIN_API View::queryInterface(const TUID iid,void** obj)
{
	QUERY_INTERFACE(iid,obj,Linux::IEventHandler::iid,Linux::IEventHandler)
	QUERY_INTERFACE(iid,obj,Linux::ITimerHandler::iid,Linux::ITimerHandler)
	return CPluginView::queryInterface(iid,obj);
}

class Ctrl : public EditController
{
public:
	static FUnknown* create(void*){return (IEditController*)new Ctrl;}
	tresult PLUGIN_API initialize(FUnknown* ctx) SMTG_OVERRIDE
	{
		tresult r=EditController::initialize(ctx);
		parameters.addParameter(STR16("Gain"),nullptr,0,0.8,ParameterInfo::kCanAutomate,0);
		return r;
	}
	tresult PLUGIN_API setComponentState(IBStream* st) SMTG_OVERRIDE{float g;int32 n;if(st&&st->read(&g,4,&n)==kResultOk&&n==4)setParamNormalized(0,g);return kResultOk;}
	IPlugView* PLUGIN_API createView(FIDString name) SMTG_OVERRIDE{if(name&&strcmp(name,ViewType::kEditor)==0)return new View(this);return nullptr;}
};

tresult PLUGIN_API View::attached(void* parent,FIDString type)
{
	tlog("attached type=%s parent=%lu",type,(unsigned long)(uintptr_t)parent);
	if(!plugFrame || plugFrame->queryInterface(Linux::IRunLoop::iid,(void**)&loop)!=kResultOk){tlog("no runloop");return kResultFalse;}
	dpy=XOpenDisplay(nullptr);
	if(!dpy) return kResultFalse;
	win=XCreateSimpleWindow(dpy,(Window)(uintptr_t)parent,0,0,rect.getWidth(),rect.getHeight(),0,0,0x203040);
	XSelectInput(dpy,win,ExposureMask|ButtonPressMask);
	gc=XCreateGC(dpy,win,0,nullptr);
	XMapWindow(dpy,win);
	XFlush(dpy);
	loop->registerEventHandler(this,ConnectionNumber(dpy));
	loop->registerTimer(this,50);
	return CPluginView::attached(parent,type);
}
tresult PLUGIN_API View::removed()
{
	tlog("removed ticks=%d",ticks);
	if(loop){loop->unregisterTimer(this);loop->unregisterEventHandler(this);loop->release();loop=nullptr;}
	if(dpy){XFreeGC(dpy,gc);XDestroyWindow(dpy,win);XCloseDisplay(dpy);dpy=nullptr;win=0;}
	return CPluginView::removed();
}
void View::draw()
{
	if(!dpy) return;
	int const w=rect.getWidth(),h=rect.getHeight();
	XSetForeground(dpy,gc,0x203040); XFillRectangle(dpy,win,gc,0,0,w,h);
	double const g=ctrl->getParamNormalized(0);
	XSetForeground(dpy,gc,0x40c060); XFillRectangle(dpy,win,gc,10,h-30,(int)((w-20)*g),20);
	XSetForeground(dpy,gc,0xffffff);
	char buf[64]; snprintf(buf,64,"ADX Test  gain %.2f  tick %d",g,ticks);
	XDrawString(dpy,win,gc,10,20,buf,strlen(buf));
	XDrawString(dpy,win,gc,10,40,"left: set gain   right: grow",28);
	XFlush(dpy);
}
void PLUGIN_API View::onFDIsSet(Linux::FileDescriptor)
{
	while(dpy && XPending(dpy))
	{
		XEvent e; XNextEvent(dpy,&e);
		if(e.type==Expose) draw();
		else if(e.type==ButtonPress)
		{
			tlog("click %d %d button %d",e.xbutton.x,e.xbutton.y,e.xbutton.button);
			if(e.xbutton.button==1)
			{
				double v=e.xbutton.x/(double)rect.getWidth();
				ctrl->beginEdit(0); ctrl->setParamNormalized(0,v); ctrl->performEdit(0,v); ctrl->endEdit(0);
				draw();
			}
			else if(e.xbutton.button==3 && plugFrame)
			{
				ViewRect nr(0,0,rect.getWidth()+100,rect.getHeight()+50);
				tresult r=plugFrame->resizeView(this,&nr);
				tlog("resizeView -> %d",(int)r);
			}
		}
	}
}

//------------------------------------------------------------------------------------------------
BEGIN_FACTORY_DEF("Aodix Test","https://example.invalid","mailto:none@example.invalid")
	DEF_CLASS2(INLINE_UID_FROM_FUID(ProcUID),PClassInfo::kManyInstances,kVstAudioEffectClass,"ADX Test Synth",Vst::kDistributable,"Instrument|Synth","1.0.0",kVstVersionString,Proc::create)
	DEF_CLASS2(INLINE_UID_FROM_FUID(CtrlUID),PClassInfo::kManyInstances,kVstComponentControllerClass,"ADX Test Synth Controller",0,"","1.0.0",kVstVersionString,Ctrl::create)
END_FACTORY
