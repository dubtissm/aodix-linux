/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - plugin library (format dispatch)
//
// Port status: stage 1 provides the plugin library interface (bundle detection for the plugin menu).
// The VST3 host implementation that actually loads plugins follows in stage 4.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AdxPlugin.h"

#include <cstdio>
#include <cstring>
#include <strings.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static bool ends_with_nocase(const char* s,const char* suffix)
{
	size_t const n=strlen(s);
	size_t const m=strlen(suffix);
	return n>=m && strcasecmp(s+n-m,suffix)==0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool adx_plugin_is_candidate(const char* name,bool is_dir)
{
	// vst3 plugins on linux are bundles: Name.vst3/Contents/x86_64-linux/Name.so
	(void)is_dir;
	return ends_with_nocase(name,".vst3");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int adx_plugin_scan(const char* path,std::vector<AdxPluginDesc>& out)
{
	// stage 1: one entry per bundle, named after the bundle
	const char* base=strrchr(path,'/');
	base=base?base+1:path;

	AdxPluginDesc d;
	d.path=path;
	d.name=base;
	size_t const dot=d.name.rfind('.');
	if(dot!=std::string::npos)
		d.name.erase(dot);
	d.uniqueID=0;
	d.isSynth=true;
	out.push_back(d);
	return 1;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
AdxPlugin* adx_plugin_load(const char* path,int32_t wanted_id,AdxPluginHost* host,char* error,int error_size)
{
	(void)path;
	(void)wanted_id;
	(void)host;
	snprintf(error,error_size,"VST3 plugin hosting is not available in this build yet.");
	return nullptr;
}
