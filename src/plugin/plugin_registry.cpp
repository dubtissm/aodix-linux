/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aodix Linux - plugin formats: dispatch by file type (".vst3" bundles, LADSPA ".so" libraries)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "AdxPlugin.h"

int adx_plugin_scan(const char* path,std::vector<AdxPluginDesc>& out)
{
	if(adx_plugin_is_ladspa(path))
		return ladspa_plugin_scan(path,out);
	return vst3_plugin_scan(path,out);
}

AdxPlugin* adx_plugin_load(const char* path,int32_t wanted_id,AdxPluginHost* host,char* error,int error_size)
{
	if(adx_plugin_is_ladspa(path))
		return ladspa_plugin_load(path,wanted_id,host,error,error_size);
	return vst3_plugin_load(path,wanted_id,host,error,error_size);
}
