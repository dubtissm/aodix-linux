/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - plugin formats: dispatch by file type (".vst3" bundles, ".clap" files, ".lv2" bundles, LADSPA ".so" libraries)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AdxPlugin.h"

#include <cstring>
#include <strings.h>

bool adx_plugin_is_clap(const char* path)
{
	size_t const n=strlen(path);
	return n>=5 && strcasecmp(path+n-5,".clap")==0;
}

int adx_plugin_scan(const char* path,std::vector<AdxPluginDesc>& out)
{
	if(adx_plugin_is_lv2(path))
		return lv2_plugin_scan(path,out);
	if(adx_plugin_is_clap(path))
		return clap_plugin_scan(path,out);
	if(adx_plugin_is_ladspa(path))
		return ladspa_plugin_scan(path,out);
	return vst3_plugin_scan(path,out);
}

AdxPlugin* adx_plugin_load(const char* path,int32_t wanted_id,AdxPluginHost* host,char* error,int error_size)
{
	if(adx_plugin_is_lv2(path))
		return lv2_plugin_load(path,wanted_id,host,error,error_size);
	if(adx_plugin_is_clap(path))
		return clap_plugin_load(path,wanted_id,host,error,error_size);
	if(adx_plugin_is_ladspa(path))
		return ladspa_plugin_load(path,wanted_id,host,error,error_size);
	return vst3_plugin_load(path,wanted_id,host,error,error_size);
}
